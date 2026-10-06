# AXLLM의 SD1.5 이미지 생성 지원 구현 설명

[English](../en/sd15_axllm_implementation.md) | [中文](../sd15_axllm_implementation.md) | **한국어**

> 중국어 [원문](../sd15_axllm_implementation.md)을 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

이 문서는 이 브랜치에서 `axllm serve`가 AX650 위에서 순수 C++ 방식으로
SD1.5 기반 `lcm-lora-sdv1-5` 모델을 실행하고, 이를 OpenAI 호환 이미지 API를 통해
프런트엔드 WebUI에서 사용할 수 있도록 제공하는 방식을 설명합니다.

이번 구현은 주로 다음 파일에 반영되어 있습니다:

- `src/runner/image/sd15_image_generator.cpp`
- `src/runner/image/sd15_image_generator.hpp`
- `src/main.cpp`
- `src/runner/ax_model_runner/ax_model_runner_ax650.cpp`
- `third_party/openai-api.cpp`의 기존 OpenAI API 라우팅 로직

## 1. 목표

다음과 같은 실행 방식을 지원하는 것이 목표입니다:

```bash
axllm serve /path/to/lcm-lora-sdv1-5 --port 18100
```

실행 후 외부에 다음을 제공합니다:

- `GET /v1/models`
- `POST /v1/images/generations`
- `POST /v1/images/edits`

또한 이미지 생성 단계에서는 더 이상 외부 Python 추론 스크립트를 호출하지 않고, 모든 처리를 C++에서 수행합니다.

## 2. AXLLM이 가정하는 모델 저장소 구조

현재 구현은 이미지 모델 저장소의 루트 디렉터리 아래에 여러 variant 하위 디렉터리가 있을 수 있다고 가정합니다.
`config.json`에서 `image_variants`를 명시적으로 선언하는 것을 권장합니다:

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

일반적인 저장소 구조는 다음과 같습니다:

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

코드는 `image_variants`를 우선적으로 읽습니다. 기존 저장소에 이 필드가 없으면 내장된 후보 디렉터리 목록으로 fallback합니다.
각 variant가 실제로 사용 가능한지는 디렉터리 이름만으로 판단하지 않고, 실제 `axmodel`과 실제 텐서
shape로 추가 검증합니다.

## 3. 추론 경로 개요

### 3.1 txt2img

`txt2img`의 C++ 추론 흐름은 다음과 같습니다:

1. CLIP BPE 토크나이저로 프롬프트를 인코딩합니다.
2. `text_encoder`를 실행합니다.
3. 초기 latent noise를 생성합니다.
4. 4단계 LCM denoise loop를 실행합니다. 핵심 모델은 `unet`입니다.
5. latent를 `1 / 0.18215`로 스케일링합니다.
6. `vae_decoder`를 실행합니다.
7. PNG로 인코딩하고 OpenAI API 형식에 맞춰 반환합니다.

### 3.2 img2img

`img2img`의 C++ 추론 흐름은 다음과 같습니다:

1. 프런트엔드에서 업로드한 이미지 바이트 스트림을 읽습니다.
2. resize한 뒤 NCHW로 변환하고 `[-1, 1]` 범위로 정규화합니다.
3. `vae_encoder`를 실행합니다.
4. 인코딩된 latent를 샘플링한 다음 노이즈를 주입합니다.
5. denoise loop를 실행합니다.
6. `vae_decoder`를 실행합니다.
7. PNG로 인코딩하여 반환합니다.

## 4. `sd15_image_generator`를 별도로 구현한 이유

AXLLM의 원래 핵심 경로는 `LLM.cpp`이며, 주로 다음을 중심으로 구성되어 있습니다:

- 토크나이저
- token embedding
- KV 캐시
- decoder group

이 구조는 LLM/VLM을 위해 설계된 것이어서, SD1.5 같은 diffusion 파이프라인을 그대로 끼워 넣기에는 적합하지 않습니다.

그래서 여기서는 이미지 생성 경로를 별도로 구현했습니다:

- `sd15::ImageGenerator`: 추상 인터페이스
- `Sd15ImageGenerator`: SD1.5의 구체 구현
- `src/main.cpp`: server 모드에서 이미지 모델을 인식하고, OpenAI 이미지 API 콜백을 등록합니다

이렇게 하면 이미지 모델 경로와 텍스트 LLM 경로가 분리되어 서로 영향을 주지 않습니다.

## 5. OpenAI 이미지 API를 연결하는 방식

`src/main.cpp`의 `run_server_mode()`에서:

1. `create_image_generator()`를 호출합니다.
2. `generator->init(config.image_model_dir, err)`를 실행하여 사용 가능한 이미지 variant를 스캔합니다.
3. 실제 variant 각각을 하나의 OpenAI model id로 등록합니다.

현재 실제로 노출되는 모델 id의 예는 다음과 같습니다:

- `lcm-lora-sdv1-5-512x512`
- `lcm-lora-sdv1-5-768x1024`

여기에는 명확한 설계 결정이 하나 있습니다:

- `dall-e-2`, `dall-e-3` 같은 플레이스홀더 별칭은 더 이상 노출하지 않습니다
- `/v1/models`는 실제로 실행 가능한 모델만 반환합니다

이렇게 하면 프런트엔드의 “실제 모델 목록을 fetch하고 그중 하나의 모델을 선택해 통신하는” 동작과 일관성을 유지할 수 있습니다.

## 6. CLIP 토크나이저를 직접 구현해야 했던 이유

처음에는 SD1.5 경로가 AXLLM의 범용 토크나이저 방식을 재사용하여
`tokenizer.txt`를 내보냈습니다. 이렇게 해도 실행은 되었지만, 프롬프트 token id가 HuggingFace
`CLIPTokenizer`의 실제 출력과 일치하지 않았습니다.

전형적인 증상은 다음과 같습니다:

- API는 성공을 반환합니다
- 하지만 이미지에는 모자이크와 텍스처 덩어리만 있고 의미 있는 내용이 없습니다

근본 원인은 다음과 같습니다:

- SD1.5의 text encoder는 반드시 CLIP BPE tokenization을 엄격하게 따라야 합니다
- 범용 LLM 토크나이저의 동작은 CLIP 토크나이저와 같은 것이 아닙니다

그래서 최종적으로 `sd15_image_generator.cpp`에 로컬
`ClipBPETokenizer`를 새로 추가하여 다음 파일을 직접 읽도록 했습니다:

- `tokenizer/vocab.json`
- `tokenizer/merges.txt`

이를 통해 C++에서 Python CLIP 토크나이저와 동일한 인코딩을 수행합니다.

수정한 뒤에야 C++와 Python 레퍼런스 구현의 프롬프트 id가 완전히 일치하게 되었습니다.

## 7. SD1.5를 AX650에서 안정적으로 실행하기 위해 runner에 적용한 조정

### 7.1 추론 전후에 cache sync 수행

SD1.5의 `axmodel` 경로에는 다음 요구 사항이 있습니다:

- 입력 버퍼에 쓴 후, 추론 전에 반드시 flush해야 합니다
- 출력 버퍼를 읽기 전, 추론 후에 반드시 invalidate해야 합니다

따라서 이미지 경로에서는 다음을 명시적으로 활성화했습니다:

- `set_auto_sync_before_inference(true)`
- `set_auto_sync_after_inference(true)`

적용 대상은 다음과 같습니다:

- text encoder
- unet
- vae decoder
- vae encoder

### 7.2 텐서의 실제 바이트 수로 dtype 추론

모델 디렉터리에 여전히 dtype 설정이 남아 있지만, AX650에서 실제로 신뢰해야 하는 것은 다음입니다:

- 런타임이 노출하는 텐서 버퍼 크기
- 논리적 원소 개수

그래서 코드에 텐서 크기로부터 dtype을 역산하는 로직을 추가했으며, 다음을 지원합니다:

- `fp32`
- `fp16`
- `bf16`

현재 650에서 사용하는 이 SD1.5 모델의 경우, 최종적으로 검증된 주요 텐서는 대부분 `fp32`였습니다.

## 8. “모자이크 이미지”를 일으킨 scheduler 버그

이번에 가장 핵심적인 문제는 scheduler 상수를 잘못 작성한 것이었습니다.

Python 레퍼런스 구현은 다음과 같습니다:

```python
betas = torch.linspace(0.00085 ** 0.5, 0.012 ** 0.5, 1000) ** 2
```

그런데 초기 C++ 코드는 다음 값을:

- `0.00085`
- `0.012`

이미 제곱근을 취한 값으로 그대로 사용했습니다.

이로 인해 `alphas_cumprod` 곡선 전체가 잘못됩니다. 그래서 다음과 같은 경우에도:

- 프롬프트 id가 올바르고
- 프롬프트 임베딩이 올바르고
- `unet`의 단일 step 출력도 올바르더라도

최종 latent 궤적은 여전히 어긋나게 되고, 이미지는 모자이크로 변합니다.

수정 방법은 다음과 같습니다:

```cpp
constexpr float kBetaStartSqrt = 0.029154759f;  // sqrt(0.00085)
constexpr float kBetaEndSqrt = 0.109544512f;    // sqrt(0.012)
```

수정한 뒤에야 C++의 궤적이 Python 레퍼런스 구현과 일치하게 되었습니다.

## 9. `models_1024x768`이 최종적으로 `768x1024`로 노출되는 이유

또 다른 이미지 오류는 크기를 잘못 해석한 데서 비롯되었습니다.

디렉터리 이름은 `models_1024x768`이지만, 실제 `vae_decoder`의 출력 shape는 다음과 같습니다:

```text
[1, 3, 1024, 768]
```

즉:

- height = `1024`
- width = `768`

OpenAI 스타일의 `WxH` 표기법에 따르면 올바르게 노출해야 하는 값은 다음과 같습니다:

- `768x1024`

이를 `1024x768`로 잘못 노출하면, 이후 C++에서 이미지를 디코딩할 때 잘못된 너비와 높이로
텐서를 해석하게 되어, 결국 가로 방향 반복, 줄무늬, 이어 붙인 부분의 어긋남 같은 문제가 발생합니다.

따라서 현재 구현은 `vae_decoder`의 실제 출력 shape에서 최종 크기를 우선 추론하고, 디렉터리 이름 매핑은
fallback으로만 사용합니다.

## 10. 이후 “variant 온디맨드 로딩” 방식으로 바꾼 이유

AX650에서 SD1.5 variant 두 벌을 동시에 메모리에 상주시키면, 실행 중에 불안정해지기 쉽습니다:

- 시작은 성공합니다
- `/v1/models`도 응답을 반환합니다
- 하지만 실제로 요청하면 `text_encoder`가 `ret=0x8006008a`를 보고할 수 있습니다

이 문제는 본질적으로 보드 측 런타임의 리소스 압박 때문에 발생하며, OpenAI API 자체의 문제는 아닙니다.

다음 두 가지를 동시에 만족하기 위해:

- 하나의 포트에서 여러 실제 모델 id를 노출
- 650에서 최대한 안정적으로 동작

현재 구현은 다음과 같이 바뀌었습니다:

1. 시작 시 모든 후보 variant를 스캔합니다
2. 시작 시 각 variant를 먼저 한 번 완전히 검증하여 메타데이터를 얻습니다
3. 상주 상태로 보관하는 것은 다음뿐입니다:
   - `ImageModelVariant`
   - variant에 대응하는 디렉터리 경로
   - fallback size 정보
4. 실제로 요청을 받았을 때 `model id`에 따라 현재 필요한 런타임을 로드합니다
5. 다른 모델로 전환하면 이전의 active 런타임을 교체합니다

이렇게 하면 다음을 유지하면서:

- `/v1/models`에 여러 실제 모델을 동시에 나열

동시에 다음을 피할 수 있습니다:

- 여러 대형 모델 런타임이 장시간 함께 상주하면서 생기는 650의 불안정 문제

대가는 다음과 같습니다:

- 다른 모델 id로 처음 전환할 때는 같은 모델에 연속으로 요청할 때보다 느립니다

이는 현재 버전에서 의도적으로 받아들인 트레이드오프입니다.

## 11. 현재 기능 매트릭스

현재까지 검증된 기능은 다음과 같습니다:

| Model ID | txt2img | img2img | 설명 |
|---|---|---|---|
| `lcm-lora-sdv1-5-512x512` | 지원 | 지원 | `vae_encoder.axmodel` 있음 |
| `lcm-lora-sdv1-5-768x1024` | 지원 | 미지원 | 현재 저장소에 해당 `vae_encoder.axmodel`이 제공되지 않음 |

`img2img`를 지원하지 않는 variant의 경우, AXLLM은 크래시하지 않고 명확한 오류를 반환합니다:

```json
{
  "error": {
    "code": "image_request_error",
    "message": "img2img is not supported for this model variant",
    "type": "image_request_error"
  }
}
```

## 12. 대표 요청 예시

### 12.1 모델 목록 가져오기

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

## 13. 현재 제한 사항

1. 새 저장소에서는 `config.json/image_variants`로 variant를 명시적으로 선언할 것을 권장합니다. 기존 저장소는 고정된 후보 디렉터리 목록을 통해 계속 호환됩니다.
2. `img2img` 지원 여부는 해당 variant가 `vae_encoder.axmodel`을 제공하는지에 따라 결정됩니다.
3. 이제 여러 모델 간 전환의 안정성은 좋아졌지만, 처음 전환할 때 로딩 오버헤드가 있습니다.
4. 현재 이 SD1.5 경로는 주로 AX650 온칩 실행을 대상으로 검증했으며, AXCL은 이번 주요 검증 대상이 아닙니다.

## 14. 이번에 실제로 추가된 것

한마디로 요약하면, 이 브랜치에서 AXLLM의 SD1.5 지원은 실제로 다음 요소들로 구성됩니다:

- 독립적인 C++ 이미지 생성 파이프라인
- CLIP 호환 토크나이저
- AX650 런타임의 cache sync 및 dtype 처리
- `axllm serve`의 OpenAI 이미지 API 연결
- 실제 모델만 노출하는 `/v1/models`
- 650 안정성을 위한 variant 온디맨드 로딩 메커니즘

즉, 이 브랜치의 “AXLLM의 SD1.5 추론 지원”은 단순히 Python을 한 겹 감싼 것이 아니라,
핵심 추론 경로를 C++와 `axllm serve`에 직접 연결한 것입니다.
