#pragma once

// EmbeddingGemma 2 text embeddings: an encoder-only model compiled as whole-sequence axmodels
// (one per padded length, e.g. 128 / 512 / 1024 tokens), not the per-layer decoder layout LLM uses.
//
// Host side: tokenizer -> [BOS] ids [EOS] -> rows of the bf16 embedding table * embed_scale,
// padded with pad_id rows. NPU: inputs_embeds [1, L, hidden] + valid [1, L] -> embedding [1, dim]
// (mean-pooled and projected, not normalised). Host: truncate to the requested Matryoshka size
// and L2-normalise.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct EmbeddingGemma2Config
{
    std::string model_name = "google/embeddinggemma-2";
    std::string tokenizer_type = "Gemma4";
    std::string tokenizer_path;              // tokenizer.axera .txt export of tokenizer.json
    std::string embed_path;                  // bf16 [vocab, hidden]
    int vocab_size = 262144;
    int hidden_size = 512;
    float embed_scale = 22.627417f;          // sqrt(hidden_size), applied in fp32
    std::vector<std::string> encoder_axmodels;
    int embedding_dim = 768;
    std::vector<int> matryoshka_dims = {768, 512, 256, 128};
    int bos_token_id = 2;
    int eos_token_id = 1;
    int pad_token_id = 0;
    std::map<std::string, std::string> prompts;  // prompt name -> task prefix
    std::string default_prompt;                  // used when a request names none ("" = no prefix)
    std::vector<int> dev_ids = {0};
};

class EmbeddingGemma2
{
public:
    EmbeddingGemma2();
    ~EmbeddingGemma2();

    bool Init(const EmbeddingGemma2Config &cfg, std::string &err);
    void Deinit();

    // One L2-normalised vector per text. prompt_name selects a task prefix from the config
    // ("" = config default); dims <= 0 means the full embedding_dim. prompt_tokens receives the
    // number of tokens fed to the encoder (after truncation) summed over all texts.
    bool Embed(const std::vector<std::string> &texts, const std::string &prompt_name, int dims,
               std::vector<std::vector<float>> &out, int &prompt_tokens, std::string &err);

    int max_tokens() const;
    const EmbeddingGemma2Config &config() const { return cfg_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    EmbeddingGemma2Config cfg_;
    std::mutex mutex_;
};
