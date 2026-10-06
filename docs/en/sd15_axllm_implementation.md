# Implementation Notes on SD1.5 Image Generation Support in AXLLM

**English** | [中文](../sd15_axllm_implementation.md) | [한국어](../ko/sd15_axllm_implementation.md)

> Translated from the Chinese [original](../sd15_axllm_implementation.md). If the two differ, the original is authoritative.

This document explains how, in this branch, `axllm serve` runs the SD1.5-based
`lcm-lora-sdv1-5` model on AX650 in pure C++, and serves it to the frontend WebUI
through OpenAI-compatible image endpoints.

The changes for this implementation are mainly in the following files:

- `src/runner/image/sd15_image_generator.cpp`
- `src/runner/image/sd15_image_generator.hpp`
- `src/main.cpp`
- `src/runner/ax_model_runner/ax_model_runner_ax650.cpp`
- the existing OpenAI API routing logic in `third_party/openai-api.cpp`

## 1. Goal

The goal is to support the following way of starting the server:

```bash
axllm serve /path/to/lcm-lora-sdv1-5 --port 18100
```

After startup, it exposes:

- `GET /v1/models`
- `POST /v1/images/generations`
- `POST /v1/images/edits`

and the image generation stage no longer calls an external Python inference script; everything is done in C++.

## 2. Model repository layout assumed by AXLLM

The current implementation assumes that the root of an image model repository can contain several variant subdirectories. It is recommended to
declare `image_variants` explicitly in `config.json`:

```json
{
  "image_variants": [
    {
      "id": "lcm-lora-sdv1-5-512x512",
      "dir": "models",
      "size": "512x512",
      "chip": "ax650",
      "supports_img2img": true
    },
    {
      "id": "lcm-lora-sdv1-5-768x1024",
      "dir": "models_1024x768",
      "size": "768x1024",
      "chip": "ax650",
      "supports_img2img": false
    }
  ]
}
```

A typical repository layout is:

```text
lcm-lora-sdv1-5/
├── config.json
├── models/
│   ├── text_encoder/sd15_text_encoder_sim.axmodel
│   ├── tokenizer/{vocab.json, merges.txt, ...}
│   ├── unet.axmodel
│   ├── vae_decoder.axmodel
│   ├── vae_encoder.axmodel
│   ├── time_input_txt2img.npy
│   └── time_input_img2img.npy
└── models_1024x768/
    ├── text_encoder/sd15_text_encoder_sim.axmodel
    ├── tokenizer/{vocab.json, merges.txt, ...}
    ├── unet.axmodel
    ├── vae_decoder.axmodel
    └── time_input_txt2img.npy
```

The code reads `image_variants` first. If an older repository does not have this field, it falls back to a built-in table of candidate directories.
Whether each variant is actually usable is not decided by the directory name alone; it is further validated against the real `axmodel` and the real tensor
shapes.

## 3. Inference pipeline overview

### 3.1 txt2img

The C++ inference flow for `txt2img` is:

1. Encode the prompt with the CLIP BPE tokenizer
2. Run `text_encoder`
3. Generate the initial latent noise
4. Run the 4-step LCM denoise loop, whose core model is `unet`
5. Scale the latent by `1 / 0.18215`
6. Run `vae_decoder`
7. Encode as PNG and return it in the OpenAI API format

### 3.2 img2img

The C++ inference flow for `img2img` is:

1. Read the image bytes uploaded by the frontend
2. Resize, convert to NCHW, and normalize to `[-1, 1]`
3. Run `vae_encoder`
4. Sample the encoded latent, then inject noise
5. Run the denoise loop
6. Run `vae_decoder`
7. Encode as PNG and return it

## 4. Why `sd15_image_generator` is implemented separately

AXLLM's original core path is `LLM.cpp`, which is built mainly around:

- tokenizer
- token embedding
- KV cache
- decoder group

This structure is designed for LLM/VLM and is not suited to having a diffusion pipeline such as SD1.5 squeezed into it directly.

So a separate image generation path is implemented here:

- `sd15::ImageGenerator`: abstract interface
- `Sd15ImageGenerator`: concrete SD1.5 implementation
- `src/main.cpp`: recognizes image models in server mode and registers the OpenAI image API callbacks

This keeps the image model path and the text LLM path separate, so they do not interfere with each other.

## 5. How the OpenAI image API is wired in

In `run_server_mode()` in `src/main.cpp`:

1. Call `create_image_generator()`
2. Run `generator->init(config.image_model_dir, err)` to scan the available image variants
3. Register each real variant as an OpenAI model id

Examples of the model ids currently exposed:

- `lcm-lora-sdv1-5-512x512`
- `lcm-lora-sdv1-5-768x1024`

There is an explicit design decision here:

- placeholder aliases such as `dall-e-2` and `dall-e-3` are no longer exposed
- `/v1/models` returns only models that can actually run

This keeps it consistent with the frontend behavior of "fetch the real model list and pick one of those models to talk to".

## 6. Why we had to implement our own CLIP tokenizer

At first, the SD1.5 path reused AXLLM's generic tokenizer approach and exported a
`tokenizer.txt`. This did run, but the prompt token ids did not match what HuggingFace
`CLIPTokenizer` actually outputs.

The typical symptoms were:

- the API returned success
- but the image contained only mosaic and texture blocks, with no semantic content

The root cause was:

- the SD1.5 text encoder must strictly use CLIP BPE tokenization
- a generic LLM tokenizer does not behave the same way as the CLIP tokenizer

So in the end a local
`ClipBPETokenizer` was added to `sd15_image_generator.cpp`, which reads directly:

- `tokenizer/vocab.json`
- `tokenizer/merges.txt`

and performs encoding in C++ that is aligned with the Python CLIP tokenizer.

Only after this fix did the prompt ids from C++ and from the Python reference implementation match exactly.

## 7. Runner adaptations made so SD1.5 runs stably on AX650

### 7.1 Cache sync before and after inference

The SD1.5 `axmodel` path requires:

- after writing to the input buffer, a flush before inference
- before reading from the output buffer, an invalidate after inference

Therefore the image path explicitly enables:

- `set_auto_sync_before_inference(true)`
- `set_auto_sync_after_inference(true)`

This applies to:

- text encoder
- unet
- vae decoder
- vae encoder

### 7.2 Inferring dtype from the actual tensor byte size

Although the model directory still keeps a dtype configuration, on AX650 what should really be trusted is:

- the tensor buffer size exposed by the runtime
- the logical element count

Therefore logic was added to infer the dtype from the tensor size, supporting:

- `fp32`
- `fp16`
- `bf16`

For the current set of SD1.5 models on the 650, most of the key tensors were ultimately verified to be `fp32`.

## 8. The scheduler bug that caused "mosaic images"

The most critical issue this time was a wrongly written scheduler constant.

The Python reference implementation is:

```python
betas = torch.linspace(0.00085 ** 0.5, 0.012 ** 0.5, 1000) ** 2
```

But the original C++ code treated:

- `0.00085`
- `0.012`

directly as values that had already been square-rooted.

This made the whole `alphas_cumprod` curve wrong. So even though:

- the prompt ids were correct
- the prompt embeddings were correct
- the single-step `unet` output was also correct

the final latent trajectory still drifted, and the image turned into a mosaic.

The fix is:

```cpp
constexpr float kBetaStartSqrt = 0.029154759f;  // sqrt(0.00085)
constexpr float kBetaEndSqrt = 0.109544512f;    // sqrt(0.012)
```

Only after the fix did the C++ trajectory align with the Python reference implementation.

## 9. Why `models_1024x768` is ultimately exposed as `768x1024`

Another image error came from interpreting the size incorrectly.

The directory name is `models_1024x768`, but the real `vae_decoder` output shape is:

```text
[1, 3, 1024, 768]
```

That is:

- height = `1024`
- width = `768`

In OpenAI-style `WxH` notation, the correct exposed size should be:

- `768x1024`

If it were wrongly exposed as `1024x768`, the C++ image decoding step would later interpret the
tensor with the wrong width and height, resulting in horizontal repetition, stripes, scrambled tiling and similar problems.

Therefore the current implementation infers the final size from the real output shape of `vae_decoder` first, and uses the directory-name mapping only
as a fallback.

## 10. Why we later switched to "loading variants on demand"

On AX650, if both SD1.5 variants are kept resident in memory at the same time, the runtime is prone to instability:

- startup succeeds
- `/v1/models` returns
- but on an actual request, `text_encoder` may report `ret=0x8006008a`

This problem is essentially caused by runtime resource pressure on the board, not by the OpenAI API itself.

To satisfy both of the following:

- exposing multiple real model ids on one port
- being as stable as possible on the 650

the current implementation was changed to:

1. Scan all candidate variants at startup
2. At startup, fully validate each variant once to obtain its metadata
3. Keep only the following resident:
   - `ImageModelVariant`
   - the directory path of the variant
   - fallback size information
4. Only when a request actually arrives, load the runtime needed for that `model id`
5. When switching to another model, replace the previous active runtime

This preserves:

- `/v1/models` listing multiple real models at the same time

while avoiding:

- the instability on the 650 caused by keeping multiple large model runtimes resident together for a long time

The cost is:

- the first switch to another model id is slower than consecutive requests to the same model

This is a trade-off deliberately accepted in the current version.

## 11. Current capability matrix

The capabilities verified so far are:

| Model ID | txt2img | img2img | Notes |
|---|---|---|---|
| `lcm-lora-sdv1-5-512x512` | Supported | Supported | Has `vae_encoder.axmodel` |
| `lcm-lora-sdv1-5-768x1024` | Supported | Not supported | The current repository does not provide a matching `vae_encoder.axmodel` |

For a variant that does not support `img2img`, AXLLM does not crash; instead it returns an explicit error:

```json
{
  "error": {
    "code": "image_request_error",
    "message": "img2img is not supported for this model variant",
    "type": "image_request_error"
  }
}
```

## 12. Typical request examples

### 12.1 Fetch the model list

```bash
curl http://127.0.0.1:18100/v1/models
```

### 12.2 512x512 txt2img

```bash
curl -X POST http://127.0.0.1:18100/v1/images/generations \
  -H 'Content-Type: application/json' \
  -d '{
    "model": "lcm-lora-sdv1-5-512x512",
    "prompt": "Self-portrait oil painting, a beautiful cyborg with golden hair, 8k",
    "size": "512x512",
    "n": 1,
    "response_format": "b64_json",
    "seed": 0
  }'
```

### 12.3 512x512 img2img

```bash
curl -X POST http://127.0.0.1:18100/v1/images/edits \
  -F model=lcm-lora-sdv1-5-512x512 \
  -F prompt='Astronauts in a jungle, cold color palette, muted colors, detailed, 8k' \
  -F size=512x512 \
  -F response_format=b64_json \
  -F image=@init.png
```

### 12.4 768x1024 txt2img

```bash
curl -X POST http://127.0.0.1:18100/v1/images/generations \
  -H 'Content-Type: application/json' \
  -d '{
    "model": "lcm-lora-sdv1-5-768x1024",
    "prompt": "Self-portrait oil painting, a beautiful cyborg with golden hair, 8k",
    "size": "768x1024",
    "n": 1,
    "response_format": "b64_json",
    "seed": 0
  }'
```

## 13. Current limitations

1. New repositories should declare variants explicitly with `config.json/image_variants`; older repositories are still supported through the fixed table of candidate directories.
2. Whether `img2img` is supported depends on whether the variant provides `vae_encoder.axmodel`.
3. Switching between multiple models is now more stable, but the first switch has a loading overhead.
4. This SD1.5 path has mainly been validated for on-chip execution on AX650; AXCL was not the main validation target this time.

## 14. What was actually added this time

To sum it up in one sentence, SD1.5 support in AXLLM in this branch actually consists of the following parts:

- an independent C++ image generation pipeline
- a CLIP-compatible tokenizer
- cache sync and dtype handling for the AX650 runtime
- OpenAI image API wiring in `axllm serve`
- a `/v1/models` that exposes only real models
- an on-demand variant loading mechanism aimed at stability on the 650

In other words, in this branch "AXLLM supports SD1.5 inference" does not mean simply wrapping a layer of Python around it; the
core inference pipeline has actually been wired into C++ and `axllm serve`.
