# LocateAnything-3B (grounding / 검출 VLM)

[English](../locate_anything.md) | [中文](../zh/locate_anything.md) | **한국어**

> 영어 [원문](../locate_anything.md)을 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

[nvidia/LocateAnything-3B](https://huggingface.co/nvidia/LocateAnything-3B)
(AXERA 빌드: `AXERA-TECH/LocateAnything-3B`)에 대한 `serve`/`run` 지원입니다. 이 모델은 Qwen2.5-3B 기반 visual grounding 모델로,
zero-shot으로 **객체 검출, phrase grounding, OCR / scene-text 검출, 문서 레이아웃,
GUI grounding 및 pointing**을 수행합니다. 이 구현은 모델 자체의
`infer_locateanything_axengine.py` 레퍼런스를 그대로 포팅한 것입니다.

## 동작 방식

- **비전**: 이미지를 560×560으로 resize(Pillow bicubic)하고 `pixel/127.5 - 1`로 정규화한 뒤,
  `[1600, 3, 14, 14]`로 patchify하여 `image_encoder_mlp.axmodel`에 통과시키면 → `400×2048`
  visual token이 됩니다(`VLMType::LocateAnythingVL`, PaddleOCR-VL의 `encode_block_normalized_float`
  경로 + 560 고정 `LocateAnythingImageProcessor`를 재사용).
- **프롬프트**(`tokenizer_type = LocateAnything`): `<image N><img><IMG_CONTEXT>×400</img>` +
  사용자의 지시문으로 구성되며, 400개의 이미지 임베딩이 `<IMG_CONTEXT>`(id 151665) 위치에 주입됩니다.
- **출력**: 모델은 기하 정보를 일반 텍스트로 렌더링되는 special token 형태로 출력합니다:
  - 박스: `<box><x1><y1><x2><y2></box>`
  - 점: `<box><x><y></box>`
  - 레이블(선택 사항): 박스 그룹 앞에 오는 `<ref>...</ref>`
  - 각 `<N>`은 **0–1000으로 정규화된** 좌표이며, 픽셀 = `N / 1000 * image_dim`입니다.

## config.json

배포된 `config.json`에는 비전 관련 필드가 빠져 있으므로 다음과 같이 추가하세요:

```json
{
    "model_name": "AXERA-TECH/LocateAnything-3B",
    "url_tokenizer_model": "qwen2_5_tokenizer.txt",
    "tokenizer_type": "LocateAnything",
    "template_filename_axmodel": "qwen2_p128_l%d_together.axmodel",
    "axmodel_num": 36,
    "filename_post_axmodel": "qwen2_post.axmodel",
    "filename_tokens_embed": "model.embed_tokens.weight.bfloat16.bin",
    "tokens_embed_num": 152681,
    "tokens_embed_size": 2048,
    "vlm_type": "LocateAnythingVL",
    "filename_image_encoder_axmodel": "image_encoder_mlp.axmodel",
    "vision_width": 560,
    "vision_height": 560,
    "vision_patch_size": 14,
    "use_mmap_load_embed": true,
    "use_mmap_load_layer": true,
    "devices": [0]
}
```

## 사용법

```shell
axllm serve /path/to/LocateAnything-3B --port 8010
```

OpenAI chat API로 이미지와 작업 지시문을 함께 보내세요(요청당 이미지 1장):

```json
{
  "model": "AXERA-TECH/LocateAnything-3B",
  "temperature": 0,
  "messages": [{"role": "user", "content": [
    {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,..."}},
    {"type": "text", "text": "Locate all the instances that matches the following description:person"}
  ]}]
}
```

### 작업별 프롬프트(레퍼런스 기준)

| 작업 | 지시문 템플릿 | 출력 |
|--|--|--|
| 객체 검출 | `Locate all the instances that matches the following description:{categories}` | 박스 여러 개 |
| phrase grounding(단일) | `Locate a single instance that matches the following description: {phrase}.` | 박스 1개 |
| phrase grounding(다중) | `Locate all the instances that match the following description: {phrase}.` | 박스 여러 개 |
| text grounding | `Please locate the text referred as {phrase}.` | 박스 여러 개 |
| scene-text 검출 / OCR | `Detect all the text in box format.` | `<ref>text</ref>` + 박스 여러 개 |
| 문서 레이아웃 | `Detect all the objects in the image that belong to the category set: {categories}.` | 박스 여러 개 |
| GUI grounding(박스) | `Locate the region that matches the following description: {phrase}.` | 박스 1개 |
| GUI grounding / pointing | `Point to: {phrase}.` | 점 |

## WebUI 데모

`scripts/locateanything_webui.py`는 실행 중인 `serve` 인스턴스용의 작은 웹 프런트엔드로, 표준 라이브러리만 사용합니다(추가 의존성 없음).
박스를 하나씩 점진적으로 그리면서 실시간 검출 / phrase grounding을 수행합니다.

```shell
AXLLM_SERVE_URL=http://127.0.0.1:8010 \
AXLLM_IMAGE_DIR=/path/to/sample_images \
python3 scripts/locateanything_webui.py --port 7861
```

`http://<host>:7861`을 여세요. 플래그: `--host`, `--port`, `--serve-url`, `--image-dir`, `--model`.

- 자동으로 스크롤되는 썸네일 배너(마우스를 올리면 일시 정지, 마우스 휠로 스크롤, 클릭하면 로드), **Upload** 버튼.
- **Task** = *Object detection*(색상이 있는 카테고리 칩을 편집하며, 카테고리마다 쿼리 1회) 또는
  *Phrase grounding*(설명을 입력, 예: `the dog on the left`).
- **Max targets** 슬라이더(16 / 64 / 256). prefill + 이미지
  인코딩 중에는 스캔 애니메이션이 재생되고, 이후 박스가 하나씩 스트리밍되어 나타납니다. 상태 표시등 + **Detect** / **Stop**.

이미지별 프리셋(선택 사항): `AXLLM_IMAGE_DIR` 안의 이미지 옆에 `tags.json`을 두세요:

```json
{
  "dogs.jpg":   { "tags": ["dog"],               "phrase": "the dog in the center" },
  "safari.jpg": { "tags": ["zebra", "elephant"], "phrase": "the elephant" }
}
```

`tags`는 검출 모드에서 사용하는 카테고리이고, `phrase`는 grounding
모드에서 사용하는 문장입니다. 썸네일을 클릭하면 둘 다 로드되며, 실제로 어느 쪽을 사용할지는 작업 선택기가 결정합니다. 단순 리스트
(`"dogs.jpg": ["dog"]`)도 허용됩니다(카테고리만 지정).

## 참고 사항

- 가장 안정적이고 형식이 올바른 기하 정보를 얻으려면 **greedy decoding을 사용하세요**(temperature 0 / 샘플링 끔).
  다른 ≤4B 모델과 동일한 권장 사항입니다.
- 기하 정보는 텍스트(`<box>…</box>`)로 반환되므로, 클라이언트 측에서 파싱한 뒤 이미지
  크기에 맞게 스케일링하세요(`N/1000*dim`). 서버 측에서 구조화된 박스를 출력하는 기능은 향후 추가될 수 있습니다.
- Qwen2.5 기반 LocateAnything만 지원하며, 요청당 이미지는 1장입니다.
