# Vision/VLM 브랜치 패턴(노트)

[English](../vision_encoder_patterns.md) | [中文](../zh/vision_encoder_patterns.md) | **한국어**

> 영어 [원문](../vision_encoder_patterns.md)을 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

이 저장소의 `axllm` 브랜치는 현재 다음과 같은 방식으로 **텍스트 전용** 모델을 실행합니다:

- `tokenizer->encode(history)`로 token id를 얻습니다.
- `last_tokens_ids` 대비 `tokens_diff`를 계산합니다(append-only fast path).
- `SetKVCache(k/v, precompute_len, input_num_token)`
- `embed_selector.getByIndex(id)`로 `tokens_diff`에 대한 `out_embed`를 구성합니다.
- `Run(out_embed)`로 prefill/decode를 수행합니다.
- `GetKVCache(...)`를 수행하고, assistant 응답을 추가한 뒤 `last_tokens_ids`를 갱신합니다.

즉, `axllm`의 "컨텍스트"(KV 캐시) 지원은 **token-id diff** + 캐싱에 기반합니다.

아래는 VLM 브랜치들(Qwen/InternVL/FastVLM/SmolVLM2, 멀티 프레임/비디오 variant 포함)에서 추려 낸 패턴입니다.

## 1) 이미지/비디오가 프롬프트에 들어가는 방식

모든 VLM 브랜치는 동일한 상위 수준의 아이디어를 따릅니다:

1. 토크나이저의 chat template이 각 미디어 항목마다 **플레이스홀더 token**을 출력합니다.
2. vision encoder가 **플레이스홀더별 임베딩 벡터**를 생성합니다.
3. 코드가 `input_ids`에서 플레이스홀더 위치를 찾아 `out_embed`의 해당 token 임베딩을 **교체**합니다.

차이점은 (a) 어떤 플레이스홀더 token을 사용하는지와 (b) 그 위치를 어떻게 찾는지에 있습니다.

### A. Qwen3/Qwen2.5-VL 방식

- 프롬프트는 `<|vision_start|> ... <|vision_end|>`로 감쌉니다.
- 비전 블록 내부에서는 플레이스홀더가 반복됩니다:
  - 이미지: `<|image_pad|>`를 `num_media * num_media_tokens`번 반복
  - 비디오: `<|video_pad|>`를 `num_media * num_media_tokens`번 반복
- 주입 오프셋은 보통 `vision_start_token_id`를 스캔한 뒤 `offset=i+1`(start 다음의 첫 번째 플레이스홀더)을 취하는 방식으로 찾습니다.

### B. InternVL 방식

- 프롬프트는 `<img> ... </img>`로 감쌉니다.
- 플레이스홀더 token은 보통 `<IMG_CONTEXT>`이며 `num_media_tokens`번 반복됩니다.
- 어떤 템플릿은 `content.data`를 플레이스홀더 앞에 두고, 어떤 템플릿은 뒤에 둡니다.
- 주입 오프셋은 `input_ids`에서 `IMAGE_CONTEXT_TOKEN`을 스캔하여 찾습니다(또는 `<|vision_start|>`를 찾은 뒤 그다음 token을 사용).

### C. FastVLM 방식

- InternVL과 비슷하게, 단순한 플레이스홀더 token(예: `"<image>"`)을 반복해서 사용합니다.
- 코드는 `input_ids`에서 고정된 `IMAGE_CONTEXT_TOKEN` id를 스캔하여 해당 슬롯을 덮어씁니다.

### D. SmolVLM2 방식

- 플레이스홀더 token은 `"<image>"`입니다(대개 단일 token id).
- 템플릿에는 이미지 token 주위에 추가 "header token"이 포함될 수 있습니다.
- 주입 오프셋은 연속된 `IMAGE_CONTEXT_TOKEN` id의 **연속 구간**(run)을 검출하고, 각 구간의 첫 번째 id를 추가하는 방식으로 찾습니다.

## 2) Vision Encoder 출력 shape와 전처리

encoder의 IO 방식은 크게 두 가지로 나뉩니다:

### A. "전통적인 image encoder"(단일 이미지 -> 임베딩 시퀀스)

`ax-fastvlm`, `ax-internvl`에서 흔히 사용합니다:

- 입력 레이아웃을 판별합니다:
  - NCHW float 입력: `(x/255 - mean) / std`로 정규화하고, 입력 텐서에 `float`로 기록합니다.
  - NHWC u8 입력: resize + RGB 변환 후 입력 텐서에 memcpy합니다.
- 출력 dtype을 판별합니다:
  - 출력 크기가 `elem_count * 2`와 같으면 => bf16 출력
  - 출력 크기가 `elem_count * 4`와 같으면 => fp32 출력(이후 bf16으로 변환)
- 결과는 (개념적으로) 길이가 `(num_media_tokens * tokens_embed_size)`인 평탄한 bf16 배열입니다.

### B. Qwen-VL "video processor"(프레임 -> 패치 -> 임베딩 시퀀스)

`ax-qwen2_5-vl`, `ax-qwen3-vl`, `axcl-qwen3-vl`에서 흔히 사용합니다:

- (이미지인 경우에도) "비디오와 유사한" 패치 분할 파이프라인으로 프레임을 전처리합니다:
  - `(vision_config.height, vision_config.width)`로 resize
  - RGB
  - 시간축 패치 분할(`temporal_patch_size`)
  - 공간 병합(`spatial_merge_size`)
  - 패치 크기(`patch_size`)
- grid segment마다 `pixel_values`를 생성합니다(비디오의 경우 segment가 여러 개).
- 각 segment를 `image_encoder`에 입력하여 임베딩 블록을 생성합니다.
- mRoPE를 위해 `cfg.image_grid_thw` 및/또는 `cfg.video_grid_thw`를 추적합니다.

## 3) 일부 VLM이 사용하는 추가 보조 입력

### A. mRoPE / position ids(Qwen-VL)

Qwen-VL 브랜치는 다음을 기반으로 `position_ids`(3 x seq_len)를 계산합니다:

- `input_ids`
- `cfg.image_grid_thw` / `cfg.video_grid_thw`
- 비전 설정: `spatial_merge_size`, 경우에 따라 비디오 시간 스케일링(`second_per_grid_ts`)

이렇게 구한 `position_ids`는 prefill 단계에서 단순히 단조 증가하는 인덱스 대신
모델의 `indices` 입력에 기록되어 사용됩니다.

### B. deepstack feature(일부 AXCL Qwen-VL variant)

일부 image encoder는 추가 텐서(예: 3개의 "deepstack feature")를 출력합니다.
prefill 중 `visual_pos_mask[j] == 1`인 token에 대해, 코드는 이 feature들을
중간 임베딩 스트림에 더합니다(bf16->fp32 add -> bf16).

### C. visual_pos_mask

`input_ids`에서 `image_token_id` 또는 `video_token_id`와 같은 위치를 표시하여 계산합니다.
deepstack feature를 비전 플레이스홀더 위치에만 맞추어 정렬하는 데 사용합니다.

## 4) 멀티 이미지 / 멀티 프레임의 표현 방식

모든 브랜치는 멀티 이미지를 다음과 같이 인코딩합니다:

- `Content{ role=USER, type=IMAGE, data=prompt, num_media=N, num_media_tokens=T }`
- 토크나이저가 플레이스홀더 token을 `N*T`번 반복합니다.
- vision encoder는 `N`개의 블록을 반환하며, 각 블록의 길이는 `T * tokens_embed_size`입니다.

비디오의 경우:

- 일부 브랜치는 각 시간축 grid segment를 하나의 "미디어 블록"으로 취급합니다.
- 일부 브랜치는 `cfg.video_grid_thw = {{grid_t, grid_h, grid_w}}`를 계산한 뒤 내부적으로 확장합니다.

## 5) `axllm`과의 핵심 차이(컨텍스트 지원)

위의 VLM 브랜치들은 보통 전체 프롬프트에 대한 `input_ids`를 만든 다음, 전체
`out_embed`(텍스트 token 임베딩 + 주입된 비전 임베딩)를 한 번에 구성합니다.

`axllm`은 다릅니다:

- KV 캐시 컨텍스트를 지원하기 위해 `tokens_diff`(증분 tail)에 대해서만 임베딩을 실제로 생성합니다.
- 현재 플레이스홀더 token 임베딩을 비전 임베딩으로 교체하는 **hook이 없습니다**.

따라서 `axllm`용 pluggable image encoder는 다음을 해결해야 합니다:

- `tokens_diff`에 플레이스홀더 id가 포함된 경우, 다음을 포함하는 임베딩을 생성해야 합니다:
  - 텍스트 token에 대한 일반 token 임베딩
  - 플레이스홀더 슬롯에 대한 비전 임베딩
- 동시에 기존의 "token-id diff + KV 캐시" 로직은 그대로 유지해야 합니다.

실질적인 의미:

- "미디어 -> 플레이스홀더 슬롯" 매핑은 `(history, token_ids)` 및/또는
  저장된 상태로부터 재현할 수 있어야 합니다. 그래야 증분 인코딩 시 대화에서 새로 추가된
  부분에 대해서만 올바른 비전 임베딩을 주입할 수 있습니다.

## 6) Pluggable 비전 모듈의 추상화 축(제안)

`axllm`의 주 제어 흐름을 바꾸지 않고 여러 브랜치를 통합하려면, pluggable 모듈이
다음 책임을 담당해야 합니다:

- `Tokenizer side`:
  - 플레이스홀더 token과 미디어 항목당 token 수(`num_media_tokens`)를 정의합니다.
  - `input_ids`에서 플레이스홀더 오프셋을 찾는 방법을 정의합니다.
  - (선택) `position_ids` 생성 규칙(mRoPE)을 제공합니다.
- `Vision side`:
  - image encoder axmodel을 로드/초기화합니다.
  - 이미지/비디오 프레임을 전처리합니다.
  - 미디어별 임베딩 블록을 생성합니다(bf16, `tokens_embed_size` 정렬).
  - (선택) deepstack feature
- `Injection side`:
  - `input_ids`와 미디어 블록이 주어지면, 플레이스홀더 슬롯이 비전 임베딩으로
    덮어써진 "임베딩 스트림"을 생성합니다.
  - `axllm` 컨텍스트 모드의 경우: 플레이스홀더 정렬을 올바르게 유지하면서
    위 작업을 **tail에 대해서만**(`tokens_diff`) 수행할 수 있도록 지원합니다.

이 노트는 의도적으로 특정 구현에 얽매이지 않게 작성했으므로, 컨텍스트를 갖춘 LLM + VLM을 지원하도록
`axllm`을 리팩터링할 때 체크리스트로 사용할 수 있습니다.

## 7) 현재 `axllm` 구현 노트(이 브랜치)

이 브랜치는 런타임 config 스위치로 켜고 끄는 pluggable 비전 모듈을 추가합니다(컴파일 타임 토글 없음):

- `config.json`: `vlm_type`(또는 `VLM_TYPE`)으로 비전 모듈을 선택합니다.
- `vlm_type != "None"(0)`이면 `filename_image_encoder_axmodel`을 반드시 설정해야 합니다.
 - 비전 전처리 백엔드는 **CMake configure 시점**에 선택됩니다:
   - OpenCV가 있으면 OpenCV를 우선 사용합니다.
   - 없으면 `third_party/SimpleCV`로 fallback하고 CMake 경고를 출력합니다(OpenCV와 약간의 차이가 있을 수 있습니다).

VLM 런타임 데이터 흐름(기존 token-diff + KV 캐시 로직을 유지합니다):

- 토크나이저는 여전히 `Content.num_media`와 `Content.num_media_tokens`를 기반으로 플레이스홀더 token을 생성합니다.
- 비전 모듈은 이 두 필드를 채운 `history`의 사본을 준비한 뒤:
  - `image_encoder.axmodel`로 이미지/비디오를 인코딩합니다.
  - `input_ids`의 플레이스홀더 위치에 대한 `pos2vision` 매핑을 구성합니다.
  - (Qwen-VL) `position_ids`(mRoPE)와 decode 시작 위치 override를 계산합니다.
- LLM 루프는 `tokens_diff`에 대해서만 임베딩을 구성하고, 그 tail 안의 비전 플레이스홀더 슬롯만 교체합니다.
