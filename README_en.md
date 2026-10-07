# AX-LLM

**English** | [中文](README.md) | [한국어](README_ko.md)

> Translated from the Chinese [README.md](README.md). If the two differ, the Chinese original is authoritative.

![GitHub License](https://img.shields.io/github/license/AXERA-TECH/ax-llm)

## Introduction

**AX-LLM** is developed under the lead of **[Axera](https://www.axera-tech.com/)**. The project explores the feasibility and the capability limits of deploying commonly used **LLMs (Large Language Models)** on existing chip platforms, so that community developers can **conveniently** carry out **quick evaluation** and **further development** of their own **LLM applications**.


### Supported chips

- AX650A/AX650N/AX8850N/AX8850
  - SDK ≥ v3.16.0
- AX637R/AX8190
  - SDK ≥ v1.12.0
- AX630C
  - SDK ≥ v3.0.0

### Supported models

#### LLM
- Qwen2.5
- Qwen3
- MiniCPM
- SmolLM2
- Gemma4
- Llama3
- HY-MT1.5-1.8B
- EmbeddingGemma 2 (text embedding, `model_type: embedding_gemma2`, see [docs/en/configuration.md](docs/en/configuration.md))
- ...

#### VLM (multimodal)
- Qwen3-VL-2B-Instruct
- Qwen3.5-2B
- Qwen3-VL-Embedding-2B (Embedding, multimodal)
- SmolVLM2-500M-Video-Instruct
- FastVLM-1.5B-GPTQ-Int4
- InternVL3_5-1B-GPTQ-INT4
- PaddleOCR-VL-1.5
- ...

### Where to get models

Our ModelZoo has moved to [Huggingface](https://huggingface.co/AXERA-TECH)

## Current branch (axllm)

In this branch the output executable is always named `axllm`; it automatically selects the AX650 on-chip backend or the AXCL PCIe backend according to the runtime environment.

> **Known limitation**: `gemma-4` (e.g. `gemma-4-E2B-it-GPTQ-INT4`) currently runs inference correctly only on the **AX650 on-chip backend**; on the **AXCL PCIe backend** it outputs garbled/repetitive text ([issue #39](https://github.com/AXERA-TECH/ax-llm/issues/39)). The cause has been traced to numerical divergence when the AXCL runtime executes gemma-4's cross-layer shared-KV (the last 20 layers reuse the KV cache of earlier layers): the on-chip results are correct, the model files on both sides have the same md5, and the ax-llm input-feeding logic is the same, so the difference lies on the runtime side; there is no fix on the ax-llm side for now. To use gemma-4, use the on-chip AX650 backend.

### Installation (recommended)

Use the install script in the repository root:

```shell
./install.sh
```

On Windows + AXCL + MinGW64 you can use:

```bat
install.bat
```

Or download and run it with a single command (default branch `axllm`):

```shell
curl -fsSL https://raw.githubusercontent.com/AXERA-TECH/ax-llm/axllm/install.sh | bash
```

Script logic:

- **AX650 on-chip backend**
  - Condition: `/proc/ax_proc/board_id` contains `AX650` and `gcc` is available on the machine
  - Behavior: automatically downloads the BSP (msp_3.6.2), builds, and installs to `/usr/bin/axllm`
- **AXCL PCIe backend**
  - Condition: `axcl-smi` can be run and `/usr/include/axcl/` and `/usr/lib/axcl/` exist
  - Behavior: builds the backend with the system AXCL headers and libraries and installs it to `/usr/bin/axllm`

By default the script pulls the **axllm branch** from the current repository's `origin` (this can be overridden with environment variables):

```shell
REPO_URL=git@github.com:AXERA-TECH/ax-llm.git BRANCH=axllm ./install.sh
```

Uninstall:

```shell
./uninstall.sh
```

For a local installation on Windows you can use:

```bat
uninstall.bat
```

### Building (manual)

If you need to build manually, choose the script for your backend:

```shell
# AX650 on-chip backend
./build_ax650.sh

# AXCL PCIe backend (x86)
./build_axcl_x86.sh

# AXCL PCIe backend (aarch64 cross-compilation)
./build_axcl_aarch64.sh
```

`build_ax650.sh` disables OpenCV by default and uses the built-in SimpleCV, to avoid accidentally linking the host-architecture OpenCV when cross-compiling. If you have an OpenCV build that works for the target architecture, you can override this default:

```shell
AXLLM_CMAKE_ARGS="-DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=FALSE" ./build_ax650.sh
```

After the build, `axllm` is generated in the `build*/install/bin/` directory (this branch uses this single name throughout).

When building the AXCL backend on Windows, explicitly turn off `BUILD_AX650` and pass `AXCL_DIR`:

```powershell
# MinGW64
cmake -S . -B build_windows_mingw -G "MinGW Makefiles" `
  -DBUILD_AX650=OFF -DBUILD_AXCL=ON `
  -DAXCL_DIR="C:\Program Files\AXCL\axcl\out\axcl_win_x64"
cmake --build build_windows_mingw --parallel

# MSVC
cmake -S . -B build_windows_msvc -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_AX650=OFF -DBUILD_AXCL=ON `
  -DAXCL_DIR="C:\Program Files\AXCL\axcl\out\axcl_win_x64"
cmake --build build_windows_msvc --config Release --parallel
```

## Usage

After building/installing, run:

```shell
axllm
```

For the API/Gradio examples, you can keep using the scripts under `scripts/` (consistent with the features of this branch).

## Configuration file

For a **description of every field** in the model directory's `config.json`, see **[docs/en/configuration.md](docs/en/configuration.md)** (required fields, dynamic loading, multi-slot KV cache, memory safety pre-check, VLM/vision, Embedding, serving, AXCL multi-card, etc.).

> The **memory safety pre-check** (`mem_guard_enable`) is enabled by default: before loading a model it estimates usage from the file sizes and compares it against the remaining CMM/DDR; if the model does not fit, it warns/aborts (and can show a Y/N confirmation), so that force-loading an oversized model does not crash the driver. See the configuration document for details.

### Docker (images exported by CI)

This repository provides a GitHub Actions workflow, `Docker Images`, that builds and exports Docker images (split by backend/architecture). The artifacts are provided in two forms:

- `.tar` (uncompressed, for target devices that have no gzip)
- `.tar.gz` (compressed, smaller)

- `docker-axcl-amd64`: AXCL (PCIe) x86_64 Host
- `docker-axcl-arm64`: AXCL (PCIe) aarch64 Host
- `docker-ax650-arm64`: AX650 (on-board) aarch64

After downloading the artifact, load the image:

```shell
docker load -i axllm-docker-axcl-amd64.tar
docker load -i axllm-docker-axcl-amd64.tar.gz
```

You can also download from fixed links (which always point to the artifacts of the latest `axllm` branch build):

```shell
# Docker image
curl -L -o axllm-docker-axcl-amd64.tar https://github.com/AXERA-TECH/ax-llm/releases/latest/download/axllm-docker-axcl-amd64.tar

# Build artifact (non-Docker)
curl -L -o axllm-axcl-linux-amd64 https://github.com/AXERA-TECH/ax-llm/releases/latest/download/axllm-axcl-linux-amd64
```

To push the images to GHCR, manually trigger `Docker Images` in Actions with `publish_ghcr=true`; the images can then be pulled from `ghcr.io/<owner>/`:

```shell
docker pull ghcr.io/<owner>/axllm-axcl-amd64:<sha>
```

AXCL run example (the AXCL Driver is installed on the Host; device nodes must be passed through, and the simplest way is `--privileged`):

```shell
docker run --rm --network host --privileged \
  -v /path/to/models:/models \
  axllm-axcl-amd64:<sha> serve /models/<model_dir> --port 8000
```

AX650 run example (on the board; `/soc` must be mounted to provide the runtime libraries):

```shell
docker run --rm --network host --privileged \
  -v /soc:/soc:ro \
  -e LD_LIBRARY_PATH=/soc/lib:$LD_LIBRARY_PATH \
  -v /path/to/models:/models \
  axllm-ax650-arm64:<sha> serve /models/<model_dir> --port 8000
```

### VLM usage

- Run `axllm run <vlm_model_path>` with a VLM model directory
- After you enter a `prompt` in each turn, you are prompted with `image >>`
  - Press Enter directly: text-only conversation for this turn
  - Enter an image path: image-text conversation
  - Enter `video:<frames_dir>`: video/multi-frame conversation (frames are read in file-name order)
  - Enter `video:<video_file>[:<fps>]`: frames are sampled uniformly from a single video file at the sampling FPS; the default is `fps=2`, and an explicit `fps` can be a positive integer or a positive decimal

The `config.json` of a VLM model must contain the following (or equivalent fields):

- `vlm_type`
- `filename_image_encoder_axmodel`

### Dynamic loading (reducing CMM usage)

On devices with a small CMM you can enable dynamic loading: at runtime only the weights of a few layers stay resident, the others are loaded on demand and released when the pool is full, which saves a significant amount of CMM (but reduces speed). Configure `dynamic_load_enable` / `dynamic_load_pool_size`; see the [configuration document](docs/en/configuration.md) for details.

### Multi-slot prefix KV cache (speeding up serve for multiple users / multiple prompt sets)

`serve` processes requests serially in a single instance. When multiple users / multiple sets of system prompts send requests in turn, by default only one context is kept, so prefill is re-run again and again. With multiple slots enabled, several prefix KV caches can be kept: on a hit they are reused by longest common prefix, and on a miss the LRU slot is overwritten (supported for both text and VLM; on VLM reuse the vision encoder is skipped). Configure `kv_cache_slots` / `kv_cache_slot_location`; see the [configuration document](docs/en/configuration.md) and the [design document](docs/en/multi_slot_kv_cache.md) for details.

### Embedding (/v1/embeddings) usage

`axllm` can load an Embedding model in `serve` mode and provides an OpenAI-compatible `/v1/embeddings` endpoint (Embedding models **do not support** the `run` interactive mode).

llama.cpp-compatible Embedding paths are also provided:

- `POST /embedding`: single embedding (llama.cpp style, returns `{ "embedding": [...], "model": "..." }`)
- `POST /embeddings`: batch embedding (llama.cpp style, returns `[{ "index": 0, "embedding": [...] }, ...]`)

1) Enable the switch in the `config.json` of the Embedding model directory:

```json
{
  "is_embedding": true
}
```

2) Start the service:

```shell
axllm serve <embedding_model_dir> --port 8000
```

After startup, the full API URLs reachable on this machine are printed in the terminal (including `127.0.0.1` and the IPs of the local network interfaces).

3) Call examples:

```shell
# Health check
curl -s http://127.0.0.1:8000/health

# Get the list of registered models
curl -s http://127.0.0.1:8000/v1/models

# Generate embeddings (input can be a string or an array of strings)
curl -s http://127.0.0.1:8000/v1/embeddings \
  -H 'Content-Type: application/json' \
  -d '{"model":"<model_name>","input":["hello","world"]}'
```

You can also refer to the Python example: `python3 scripts/openai_embedding_demo.py --model <model_name> --api_url http://127.0.0.1:8000/v1` (run `pip install openai` first).

#### Multimodal Embedding (messages extension)

For Embedding models that support visual input (such as `Qwen3-VL-Embedding-2B`), `/v1/embeddings` additionally accepts `messages` (in the same format as `/v1/chat/completions`), which enables image-text embeddings:

```shell
python3 scripts/openai_vl_embedding_demo.py \
  --model <model_name> \
  --api_url http://127.0.0.1:8000/v1 \
  --image /path/to/image.jpg \
  --text "Describe this image"
```

## Run examples
### Command-line chat
```shell
$ axllm run smollm2-360m-ax650/
[I][                            Init][ 127]: LLM init start
tokenizer_type = 1
 97% | ████████████████████████████████  |  34 /  35 [1.98s<2.03s, 17.21 count/s] init post axmodel ok,remain_cmm(11249 MB)
[I][                            Init][ 188]: max_token_len : 2047
[I][                            Init][ 191]: kv_cache_size : 320, kv_cache_num: 2047
[I][                            Init][ 194]: prefill_token_num : 128
[I][                            Init][ 198]: grp: 1, prefill_max_kv_cache_num : 1
[I][                            Init][ 198]: grp: 2, prefill_max_kv_cache_num : 128
[I][                            Init][ 198]: grp: 3, prefill_max_kv_cache_num : 256
[I][                            Init][ 198]: grp: 4, prefill_max_kv_cache_num : 384
[I][                            Init][ 198]: grp: 5, prefill_max_kv_cache_num : 512
[I][                            Init][ 198]: grp: 6, prefill_max_kv_cache_num : 640
[I][                            Init][ 198]: grp: 7, prefill_max_kv_cache_num : 768
[I][                            Init][ 198]: grp: 8, prefill_max_kv_cache_num : 896
[I][                            Init][ 198]: grp: 9, prefill_max_kv_cache_num : 1024
[I][                            Init][ 203]: prefill_max_token_num : 1024
[I][                            Init][  27]: LLaMaEmbedSelector use mmap
100% | ████████████████████████████████ |  35 /  35 [1.98s<1.98s, 17.70 count/s] embed_selector init ok
[I][                     load_config][ 282]: load config: 
{
    "enable_repetition_penalty": false,
    "enable_temperature": false,
    "enable_top_k_sampling": false,
    "enable_top_p_sampling": false,
    "penalty_window": 20,
    "repetition_penalty": 1.2,
    "temperature": 0.9,
    "top_k": 10,
    "top_p": 0.8
}

[I][                            Init][ 224]: LLM init ok
Type "q" to exit
Ctrl+c to stop current running
"reset" to reset kvcache
"dd" to remove last conversation.
"pp" to print history.
----------------------------------------
prompt >> hello,my name is Allen
[I][                      SetKVCache][ 357]: prefill_grpid:2 kv_cache_num:128 precompute_len:0 input_num_token:27
[I][                      SetKVCache][ 359]: current prefill_max_token_num:1024
[I][                      SetKVCache][ 360]: first run
[I][                             Run][ 412]: input token num : 27, prefill_split_num : 1
[I][                             Run][ 474]: ttft: 177.20 ms
Hello, Allen. How can I assist you today?

[N][                             Run][ 554]: hit eos,avg 26.19 token/s

[I][                      GetKVCache][ 331]: precompute_len:38, remaining:986
prompt >> 
```

### Server (OpenAI API compatible)

```shell
$ axllm serve smollm2-360m-ax650/
[I][                            Init][ 127]: LLM init start
tokenizer_type = 1
 97% | ████████████████████████████████  |  34 /  35 [1.99s<2.05s, 17.09 count/s] init post axmodel ok,remain_cmm(11249 MB)
[I][                            Init][ 188]: max_token_len : 2047
[I][                            Init][ 191]: kv_cache_size : 320, kv_cache_num: 2047
[I][                            Init][ 194]: prefill_token_num : 128
[I][                            Init][ 198]: grp: 1, prefill_max_kv_cache_num : 1
[I][                            Init][ 198]: grp: 2, prefill_max_kv_cache_num : 128
[I][                            Init][ 198]: grp: 3, prefill_max_kv_cache_num : 256
[I][                            Init][ 198]: grp: 4, prefill_max_kv_cache_num : 384
[I][                            Init][ 198]: grp: 5, prefill_max_kv_cache_num : 512
[I][                            Init][ 198]: grp: 6, prefill_max_kv_cache_num : 640
[I][                            Init][ 198]: grp: 7, prefill_max_kv_cache_num : 768
[I][                            Init][ 198]: grp: 8, prefill_max_kv_cache_num : 896
[I][                            Init][ 198]: grp: 9, prefill_max_kv_cache_num : 1024
[I][                            Init][ 203]: prefill_max_token_num : 1024
[I][                            Init][  27]: LLaMaEmbedSelector use mmap
100% | ████████████████████████████████ |  35 /  35 [1.99s<1.99s, 17.58 count/s] embed_selector init ok
[I][                     load_config][ 282]: load config: 
{
    "enable_repetition_penalty": false,
    "enable_temperature": false,
    "enable_top_k_sampling": false,
    "enable_top_p_sampling": false,
    "penalty_window": 20,
    "repetition_penalty": 1.2,
    "temperature": 0.9,
    "top_k": 10,
    "top_p": 0.8
}

[I][                            Init][ 224]: LLM init ok
Starting server on port 8000 with model 'AXERA-TECH/SmolLM2-360M-Instruct'...
OpenAI API Server starting on http://0.0.0.0:8000
Max concurrency: 1
Models: AXERA-TECH/SmolLM2-360M-Instruct
```
### Testing the OpenAI API

Text-only chat:
```shell
python scripts/openai_demo.py --model AXERA-TECH/SmolLM2-360M-Instruct --api_url http://127.0.0.1:8000/v1
```

Custom prompt:
```shell
python scripts/openai_demo.py --model AXERA-TECH/SmolLM2-360M-Instruct --prompt "请介绍一下你自己"
```

Image-text chat (VLM; the image is automatically base64-encoded and sent):
```shell
python scripts/openai_demo.py --model AXERA-TECH/Qwen3-VL-2B-Instruct --image /path/to/image.jpg --prompt "描述一下这张图片"
```

Audio chat (Gemma4; currently each message supports a single audio file):
```shell
python scripts/openai_demo.py --model AXERA-TECH/gemma-4-E2B-it --audio /path/to/audio.wav --prompt "Transcribe the speech in its original language. Output only the transcription."
```

Audio transcription (OpenAI Audio API):
```shell
curl http://127.0.0.1:8000/v1/audio/transcriptions \
  -F "model=AXERA-TECH/gemma-4-E2B-it" \
  -F "file=@/path/to/audio.wav" \
  -F "response_format=json"
```

Audio translation (OpenAI Audio API):
```shell
curl http://127.0.0.1:8000/v1/audio/translations \
  -F "model=AXERA-TECH/gemma-4-E2B-it" \
  -F "file=@/path/to/audio.wav" \
  -F "response_format=text"
```

`response_format` currently supports `json`, `text`, `srt`, `vtt`.

> **Parameters**
> | Parameter | Description | Default |
> |------|------|--------|
> | `--model` | Model name (required) | — |
> | `--api_url` | API address | `http://127.0.0.1:8000/v1` |
> | `--prompt` | User prompt text | `hello` |
> | `--image` | Image path (optional, VLM mode) | — |
> | `--audio` | Audio path (optional, Gemma4 mode) | — |
>
> When `--image` or `--audio` is specified, the script reads the media file and embeds it in the request in `data:...;base64,...` format; the server automatically decodes it into a temporary file for the corresponding encoder.

## Technical discussion

- Github issues
- QQ group: 139953715
