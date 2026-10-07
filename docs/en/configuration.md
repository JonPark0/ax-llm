# Configuration file reference (model directory `config.json`)

**English** | [中文](../configuration.md) | [한국어](../ko/configuration.md)

> Translated from the Chinese [original](../configuration.md). If the two differ, the original is authoritative.

`axllm` reads `<model_dir>/config.json` at startup. All available fields are listed below, grouped by purpose; **any field not listed as required is optional**, and its default value is used when it is omitted.

> Path fields (`filename_*` / `template_filename_axmodel` / `post_config_path`, etc.) are resolved relative to the model directory.

## Required

| Field | Type | Description |
|---|---|---|
| `model_name` | string | Model name (shown in `/v1/models` and in logs) |
| `tokenizer_type` | string | Tokenizer type (e.g. `Qwen3` / `Qwen3VL` / `Gemma4VL` / `SmolLM2` …) |
| `url_tokenizer_model` | string | Path to a **local** tokenizer file (e.g. `qwen3_tokenizer.txt`). ⚠ The `url` in the field name and the `http` in the default value are both historical leftovers; **only local files are currently read, HTTP is not supported** |
| `template_filename_axmodel` | string | File name template for the per-layer axmodel, containing `%d` (e.g. `qwen3_p128_l%d_together.axmodel`) |
| `axmodel_num` | int | Number of transformer layers |
| `filename_post_axmodel` | string | The post axmodel that outputs the logits |
| `filename_tokens_embed` | string | Token embedding weights (bf16 bin) |
| `tokens_embed_num` | int | Vocabulary size |
| `tokens_embed_size` | int | Embedding dimension |

## General / tokenization

| Field | Default | Description |
|---|---|---|
| `system_prompt` | empty | Default system prompt. **Only `run` (interactive) mode** prepends it automatically when it is missing; **`serve` mode does not inject it automatically** (following OpenAI semantics, the system part is controlled by the requester: if the request carries no system message, there is no system section) |
| `post_config_path` | `post_config.json` | Sampling configuration file |
| `bos` / `eos` | `true` / `false` | Whether to add BOS/EOS |
| `pad_token_id` | 0 | pad token id |
| `enable_thinking` (bool) or `thinking_mode` (string) | model default | Thinking switch; see "Thinking mode" below |

### Thinking mode

Controls **whether the model thinks during the current generation** (thinking / reasoning). It is one switch with two equivalent forms:

- `enable_thinking` (bool): `true` = think, `false` = do not think;
- `thinking_mode` (string): `think` = think, `no_think` = do not think, `default`/`auto` = use the model default.

If both are given, `enable_thinking` takes precedence; if neither is given, the model default is used. It can also be **overridden per request** in the `/v1/chat/completions` request body (the same two keys are supported, either at the top level or under `chat_template_kwargs`); after the request ends, it falls back to the config default.

⚠ **Tokenizers on which it currently takes effect: the whole Qwen3 family (Qwen3 / Qwen3VL / Qwen3Omni / Qwen2.5 / Qwen3.5) and MiniCPM5.** Other tokenizers do not support this switch yet: the setting is **ignored and a warning is logged once** (it no longer fails silently). `no_think` is implemented by following each model's official template (e.g. for Qwen3, an empty `<think>\n\n</think>` block is injected after the generation prompt).

> Note: this is **not the same thing** as `think_in_prompt`. `think_in_prompt` means "whether the assistant's earlier thinking content is kept in the multi-turn history"; it is determined automatically by the tokenizer type (false only for Gemma4/Gemma4VL, kept for all others), is **not configurable**, and is unrelated to this switch.

## Loading and memory

| Field | Default | Description |
|---|---|---|
| `use_mmap_load_embed` (alias `b_use_mmap_load_embed`) | `false` | Load the embedding with mmap (saves memory) |
| `use_mmap_load_layer` (alias `b_use_mmap_load_layer`) | `true` (AX650 only) | Use mmap for layer weights |
| `dynamic_load_enable` | `false` | Dynamic layer loading (saves CMM, reduces speed); see README |
| `dynamic_load_pool_size` | `2` | Number of resident layers for dynamic loading (only when enabled) |
| **`mem_guard_enable`** | `true` | **Master switch for the pre-load memory check** (see "Memory safety pre-check" below) |
| **`mem_guard_floor_mb`** | `128` | Extra safety margin (MB) reserved on top of the estimated usage |
| **`mem_guard_on_unsafe`** | `prompt` | When unsafe: `prompt` (Y/N prompt when there is a TTY; without a TTY = abort) / `abort` / `warn` |

### Memory safety pre-check (prevents driver crashes caused by loading beyond CMM/DDR)

When enabled, the remaining memory is checked in **two stages**; if either stage finds it unsafe, it is handled according to `mem_guard_on_unsafe`.

**① Before loading (estimated from file sizes)** — stops models that "clearly do not fit":

- **CMM** (device memory; each layer / post / vision & audio encoders): AX650 reads `/proc/ax_proc/mem_cmm_info`, AXCL uses `axcl_GetCMMRemain`; with multiple cards, each card is accounted for separately.
- **DDR** (host memory; the token embedding when it is not mmap-loaded / Gemma per-layer weights): reads **MemAvailable** from `/proc/meminfo` (it already accounts for reclaimable buffer/cache and is the "truly available" amount, which avoids false alarms).

**② During loading (extrapolated from measurements)** — file sizes cannot capture the extra KV/IO buffers that the engine allocates when it loads each layer (roughly ~30% more than the weights alone, and growing with the context length). Therefore, while loading layers, it extrapolates "remaining layers + post tail" from the **measured per-layer CMM increase**, and as soon as the projection would go below the floor, it **aborts before allocating** (at that point only the first few layers have been loaded, so they can be reclaimed cleanly and the driver does not crash). When loading in parallel on multiple cards, a trigger on any card stops the other cards. Note: no interactive prompt is shown at this stage (① has already asked), so `prompt` is equivalent to `abort` here.

Decision rule: if `remaining - estimate < mem_guard_floor_mb` → `abort` stops immediately with an error; `warn` only warns and continues; `prompt` shows `[y/N]` in an interactive terminal (default N = do not load), and falls back to `abort` when there is no terminal (serve/docker).

- Disable: `mem_guard_enable=false`.
- `dynamic_load_enable=true`: layer weights are released right after loading, so ① does not count layer weights, and ② still intercepts layer IO based on measurements.
- With multiple slots (`kv_cache_slots>1`), the N×KV allocation is already budgeted precisely from the known real sizes, and `mem_guard_floor_mb` is used as the reserved margin (the larger of it and the built-in 256MB/512MB is used).

## Attention (hybrid attention / long-context models)

| Field | Default | Description |
|---|---|---|
| `full_attention_interval` | 0 | Every N-th layer (1-indexed) is full-attention, the rest are linear (e.g. Qwen3.5) |
| `layer_types` | empty | Explicit per-layer type array (`full_attention`/`linear_attention`/`sliding_attention`) |
| `sliding_window` | 0 | Sliding window size |
| `num_kv_shared_layers` | 0 | Number of trailing layers that share KV |

> These fields do not have to be written at the top level of `config.json`: `full_attention_interval` / `num_kv_shared_layers` fall back to reading `text_config.*`; when `sliding_window` / `layer_types` are not configured, they are read automatically from the tokenizer's sidecar config in the model directory.

## Multi-slot prefix KV cache (speeding up serve for multiple users / multiple prompts)

| Field | Default | Description |
|---|---|---|
| `kv_cache_slots` | 1 | Number of slots; 1 = disabled (same as the existing behavior) |
| `kv_cache_slot_location` | `device` | `device` (zero-copy pointer switching) / `host` (saves CMM, copies on switch) |

See [multi_slot_kv_cache.md](multi_slot_kv_cache.md) for details.

## VLM / vision / audio

| Field | Default | Description |
|---|---|---|
| `vlm_type` (alias `VLM_TYPE`) | `None` | `Qwen2_5VL`/`Qwen3VL`/`InternVL3`/`FastVLM`/`SmolVLM2`/`PaddleOCRVL`/`Gemma4VL`/`MiniCPMV46VL` |
| `filename_image_encoder_axmodel` | — | Vision encoder axmodel (required for VLM) |
| `filename_audio_encoder_axmodel_5s` / `_30s` | — | Gemma4 audio encoder (ASR) |
| `vision_cache_dir` | empty | Disk cache directory for vision embeddings |
| `vision_width` / `vision_height` | 448 | Vision input size (inferred automatically from the encoder input shape when not configured) |
| `vision_patch_size` / `vision_temporal_patch_size` / `vision_spatial_merge_size` | 14 / 2 / 2 | patchify parameters |
| `vision_fps` / `vision_tokens_per_second` | 1 / 1 | Video time scaling (Qwen2.5-VL mRoPE) |
| `vision_num_frames` / `vision_do_sample_frames` | 0 / true | Maximum number of sampled video frames / whether to sample frames uniformly |

> **Vision cache (vision_cache) environment variables:**
> - `AXLLM_VISION_CACHE=0`: disables the vision cache completely (disk + memory).
> - `AXLLM_VISION_MEM_CACHE_SIZE=<N>`: maximum number of entries in the memory cache (default 8, LRU eviction; protects long-running serve).
> - `AXLLM_VISION_DISK_CACHE_MAX_MB=<MB>`: maximum total size of the disk cache directory (default 1024); when it is exceeded, `.bin` files are evicted oldest mtime first.
> - `AXLLM_VISION_DISK_CACHE_MIN_FREE_MB=<MB>`: minimum free space required before writing to disk (default 300); below this threshold the disk write is skipped (the memory cache is still used), to avoid filling up the disk and bringing down system services.

## Embedding

| Field | Default | Description |
|---|---|---|
| `is_embedding` (alias `embedding`; the old `embedding_type`/`EMBEDDING_TYPE` are deprecated) | `false` | Start in Embedding mode and provide `/v1/embeddings` (`run` is not supported) |

## Gemma4 per-layer projection

| Field | Description |
|---|---|
| `hidden_size_per_layer_input` | per-layer projection dimension (enabled when >0) |
| `rms_norm_eps` | RMSNorm eps |
| `filename_tokens_embed_per_layer` / `filename_per_layer_model_projection` / `filename_per_layer_projection_norm` | per-layer weight files |

## Serving (serve)

| Field | Default | Description |
|---|---|---|
| `port` | 8000 | Listening port |
| `server_timeout_ms` | 300000 | Request timeout (also reused for concurrent queueing). It can also be overridden on the command line with `--server_timeout_ms <ms>`; at serve startup the effective value is printed as `server request/queue timeout: N ms` |
| `server_default_max_tokens` | 0 | Default value used when a request does not carry max_tokens (0 = use the built-in default) |
| `server_max_output_tokens` | 0 | Hard limit on output tokens (0 = no additional limit) |
| `server_forced_prompt_text` | — | Forced prompt (e.g. for OCR normalization) |

## Sampling / post-processing (post_config.json)

Sampling parameters go in `post_config.json` in the model directory (at the same level as `config.json`, specified by `post_config_path`). All keys are optional; a missing key is treated as default/disabled.

| Key | Default | Description |
|---|---|---|
| `enable_temperature` / `temperature` | false / 1.0 | Temperature. **Note**: when temperature is enabled but `top_k`/`top_p` are not, multinomial sampling is performed over the full distribution (previously this was ignored and treated as greedy); `temperature<=0` is treated as greedy. |
| `enable_top_k_sampling` / `top_k` | false / 1 | top-k sampling (k is automatically clamped to the vocabulary size). |
| `enable_top_p_sampling` / `top_p` | false / 1.0 | nucleus sampling; when enabled together with top_k, top_p takes precedence. |
| `enable_repetition_penalty` / `repetition_penalty` / `penalty_window` | false / 1.0 / 20 | Repetition penalty (applies only to the most recent `penalty_window` tokens). |
| `frequency_penalty` | 0.0 | OpenAI-style frequency penalty: `logit -= frequency_penalty × occurrences` (counted within the `penalty_window` window); enabled when non-zero. |
| `presence_penalty` | 0.0 | OpenAI-style presence penalty: `logit -= presence_penalty` (subtracted once if the token appeared within the window); enabled when non-zero. |

**Per-request override (serve / OpenAI-compatible API):** `temperature`, `top_p`, `frequency_penalty` and `presence_penalty` in the request body override the config defaults for that request, and fall back automatically after the request ends. Example:

```json
POST /v1/chat/completions
{ "model": "...", "messages": [ ... ],
  "temperature": 0.7, "frequency_penalty": 0.5, "presence_penalty": 0.3 }
```

> serve semantics: if a request carries neither `temperature` nor `top_p`, that request is handled as greedy (penalties still apply to the argmax).

## AXCL (PCIe multi-card)

| Field | Default | Description |
|---|---|---|
| `devices` | `[0]` | List of device ids to use (multi-card tensor parallelism) |

> **Environment variable override**: `AXLLM_DEVICES=0,1 axllm run/serve <model_dir>` overrides `devices` in the config (comma-separated device ids), with no need to edit config.json. It is commonly used to start a separate model instance on each set of cards for testing, for example:
> ```sh
> AXLLM_DEVICES=0,1 axllm serve <dirA> &
> AXLLM_DEVICES=2,3 axllm serve <dirB> &
> ```
> Only effective in AXCL builds; if it is set but no valid id can be parsed from it, the config value is used.

## Image generation (SD1.5)

| Field | Description |
|---|---|
| `model_type` / `task_type` = `image_generation` or `is_image_generation=true` | Start in image generation mode and provide `/v1/images/*` |
| `image_model_dir` | Root directory of the image model |

## EmbeddingGemma 2 (whole-sequence encoder embeddings)

With `model_type` = `embedding_gemma2`, axllm starts a `/v1/embeddings` server (no `run` mode, text input only). The model is one whole-sequence encoder axmodel per fixed length (for example 128/512/1024), not per-layer axmodels; the shortest model that fits the input is picked, and longer inputs are truncated keeping BOS and the final EOS.

| Field | Default | Description |
|---|---|---|
| `encoder_axmodels` | — | Encoder axmodels (inputs `inputs_embeds` [1,L,512] and `valid` [1,L], output `embedding` [1,768]) |
| `url_tokenizer_model` / `tokenizer_type` | — / `Gemma4` | Tokenizer file exported with tokenizer.axera |
| `filename_tokens_embed` / `tokens_embed_num` / `tokens_embed_size` | — / `262144` / `512` | bf16 token embedding table |
| `embed_scale` | `22.627417` | Embedding scale (sqrt(512), applied in fp32) |
| `embedding_dim` / `matryoshka_dims` | `768` / `[768,512,256,128]` | Output size and the sizes a request's `dimensions` may truncate to (re-normalised after truncation) |
| `bos_token_id` / `eos_token_id` / `pad_token_id` | `2` / `1` / `0` | Special tokens |
| `prompts` / `default_prompt` | — / `""` | Task prefixes; a request picks one with `input_type` or `prompt_name` (for example `query`, `document`) |
| `devices` | `[0]` | AXCL device id (or `AXLLM_DEVICES`) |
