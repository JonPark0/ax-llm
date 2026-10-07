#include "EmbeddingGemma2.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BaseTokenizer.hpp"
#include "LLMEmbedSelector.hpp"
#include "LLMLayer.hpp"  // ax_runner_t for the active backend (AXCL or on-chip)
#include "utils/sample_log.h"

namespace
{
float bf16_to_fp32(unsigned short v)
{
    uint32_t bits = static_cast<uint32_t>(v) << 16;
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
} // namespace

struct EmbeddingGemma2::Impl
{
    struct Encoder
    {
        int length = 0;
        std::unique_ptr<ax_runner_t> runner;
    };

    std::shared_ptr<BaseTokenizer> tokenizer;
    LLaMaEmbedSelector embeds;
    std::vector<Encoder> encoders;  // sorted by length
    std::vector<unsigned short> row;
    std::vector<float> pad_row;
    std::vector<int> axcl_devices;
};

EmbeddingGemma2::EmbeddingGemma2() = default;

EmbeddingGemma2::~EmbeddingGemma2() { Deinit(); }

int EmbeddingGemma2::max_tokens() const
{
    return impl_ && !impl_->encoders.empty() ? impl_->encoders.back().length : 0;
}

bool EmbeddingGemma2::Init(const EmbeddingGemma2Config &cfg, std::string &err)
{
    Deinit();
    cfg_ = cfg;
    impl_ = std::make_unique<Impl>();
    if (cfg.encoder_axmodels.empty())
    {
        err = "encoder_axmodels is empty";
        return false;
    }
    const int devid = cfg.dev_ids.empty() ? 0 : cfg.dev_ids.front();
#ifdef USE_AXCL
    if (axcl_Init(devid) != 0)
    {
        err = "axcl_Init(" + std::to_string(devid) + ") failed";
        return false;
    }
    impl_->axcl_devices.push_back(devid);
#endif

    impl_->tokenizer = create_tokenizer(cfg.tokenizer_type);
    if (!impl_->tokenizer || !impl_->tokenizer->load(cfg.tokenizer_path))
    {
        err = "tokenizer load failed: " + cfg.tokenizer_type + " " + cfg.tokenizer_path;
        return false;
    }
    if (!impl_->embeds.Init(cfg.embed_path, cfg.vocab_size, cfg.hidden_size, true))
    {
        err = "embedding table load failed: " + cfg.embed_path;
        return false;
    }
    impl_->row.resize(cfg.hidden_size);
    impl_->pad_row.resize(cfg.hidden_size);
    impl_->embeds.getByIndex(cfg.pad_token_id, impl_->row.data());
    for (int i = 0; i < cfg.hidden_size; ++i)
        impl_->pad_row[i] = bf16_to_fp32(impl_->row[i]) * cfg.embed_scale;

    for (const auto &path : cfg.encoder_axmodels)
    {
        Impl::Encoder enc;
        enc.runner = std::make_unique<ax_runner_t>();
        if (enc.runner->init(path.c_str(), devid) != 0)
        {
            err = "failed to load " + path;
            return false;
        }
        enc.runner->set_auto_sync_before_inference(true);
        enc.runner->set_auto_sync_after_inference(true);
        const auto &in = enc.runner->get_input("inputs_embeds");
        const auto &valid = enc.runner->get_input("valid");
        const auto &out = enc.runner->get_output("embedding");
        enc.length = in.vShape.size() >= 2 ? static_cast<int>(in.vShape[1]) : 0;
        const size_t want_in = static_cast<size_t>(enc.length) * cfg.hidden_size * sizeof(float);
        if (enc.length <= 0 || static_cast<size_t>(in.nSize) != want_in ||
            static_cast<size_t>(valid.nSize) != enc.length * sizeof(float) ||
            static_cast<size_t>(out.nSize) != cfg.embedding_dim * sizeof(float))
        {
            err = "unexpected io in " + path + " (need float32 inputs_embeds [1,L," + std::to_string(cfg.hidden_size) +
                  "], valid [1,L], embedding [1," + std::to_string(cfg.embedding_dim) + "])";
            return false;
        }
        ALOGI("EmbeddingGemma2 encoder L=%d: %s", enc.length, path.c_str());
        impl_->encoders.push_back(std::move(enc));
    }
    std::sort(impl_->encoders.begin(), impl_->encoders.end(),
              [](const Impl::Encoder &a, const Impl::Encoder &b) { return a.length < b.length; });
    return true;
}

void EmbeddingGemma2::Deinit()
{
    if (!impl_) return;
    for (auto &enc : impl_->encoders)
        if (enc.runner) enc.runner->deinit();  // ax_runner_axcl has no destructor that frees the model
    impl_->encoders.clear();
#ifdef USE_AXCL
    for (int devid : impl_->axcl_devices) axcl_Exit(devid);
#endif
    impl_.reset();
}

bool EmbeddingGemma2::Embed(const std::vector<std::string> &texts, const std::string &prompt_name, int dims,
                            std::vector<std::vector<float>> &out, int &prompt_tokens, std::string &err)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    prompt_tokens = 0;
    if (!impl_ || impl_->encoders.empty())
    {
        err = "model not initialised";
        return false;
    }
    if (dims <= 0) dims = cfg_.embedding_dim;
    if (std::find(cfg_.matryoshka_dims.begin(), cfg_.matryoshka_dims.end(), dims) == cfg_.matryoshka_dims.end())
    {
        err = "dimensions must be one of the trained sizes (768, 512, 256, 128)";
        return false;
    }
    const std::string name = prompt_name.empty() ? cfg_.default_prompt : prompt_name;
    std::string prefix;
    if (!name.empty())
    {
        auto it = cfg_.prompts.find(name);
        if (it == cfg_.prompts.end())
        {
            err = "unknown prompt name: " + name;
            return false;
        }
        prefix = it->second;
    }

    const int hidden = cfg_.hidden_size;
    for (const auto &text : texts)
    {
        std::vector<int> ids = impl_->tokenizer->encode(prefix + text);
        if (ids.empty() || ids.front() != cfg_.bos_token_id) ids.insert(ids.begin(), cfg_.bos_token_id);
        if (ids.back() != cfg_.eos_token_id) ids.push_back(cfg_.eos_token_id);

        auto &encoders = impl_->encoders;
        auto pick = std::find_if(encoders.begin(), encoders.end(),
                                 [&](const Impl::Encoder &e) { return static_cast<int>(ids.size()) <= e.length; });
        auto &enc = pick == encoders.end() ? encoders.back() : *pick;
        if (static_cast<int>(ids.size()) > enc.length)
        {  // truncate like the HF tokenizer: keep BOS ... and the final EOS
            ids.resize(enc.length - 1);
            ids.push_back(cfg_.eos_token_id);
        }
        const int n = static_cast<int>(ids.size());
        prompt_tokens += n;

        float *x = static_cast<float *>(enc.runner->get_input("inputs_embeds").pVirAddr);
        float *valid = static_cast<float *>(enc.runner->get_input("valid").pVirAddr);
        for (int t = 0; t < enc.length; ++t)
        {
            float *dst = x + static_cast<size_t>(t) * hidden;
            if (t < n)
            {
                const int id = ids[t];
                if (id < 0 || id >= cfg_.vocab_size)
                {
                    err = "token id out of range: " + std::to_string(id);
                    return false;
                }
                impl_->embeds.getByIndex(id, impl_->row.data());
                for (int i = 0; i < hidden; ++i) dst[i] = bf16_to_fp32(impl_->row[i]) * cfg_.embed_scale;
            }
            else
            {
                std::memcpy(dst, impl_->pad_row.data(), hidden * sizeof(float));
            }
            valid[t] = t < n ? 1.0f : 0.0f;
        }
        if (enc.runner->inference() != 0)
        {
            err = "encoder inference failed";
            return false;
        }
        const float *y = static_cast<const float *>(enc.runner->get_output("embedding").pVirAddr);
        std::vector<float> v(y, y + dims);
        double norm = 0.0;
        for (float f : v) norm += static_cast<double>(f) * f;
        norm = std::sqrt(norm);
        if (norm > 0)
            for (float &f : v) f = static_cast<float>(f / norm);
        out.push_back(std::move(v));
    }
    return true;
}
