# 설정 파일 설명(모델 디렉터리 `config.json`)

[English](../en/configuration.md) | [中文](../configuration.md) | **한국어**

> 중국어 [원문](../configuration.md)을 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

`axllm`은 시작할 때 `<model_dir>/config.json`을 읽습니다. 아래에 사용 가능한 모든 필드를 용도별로 묶어 나열합니다. **필수로 표시되지 않은 필드는 모두 선택 사항**이며, 지정하지 않으면 기본값을 사용합니다.

> 경로 필드(`filename_*` / `template_filename_axmodel` / `post_config_path` 등)는 모델 디렉터리를 기준으로 한 상대 경로로 해석됩니다.

## 필수

| 필드 | 타입 | 설명 |
|---|---|---|
| `model_name` | string | 모델 이름(`/v1/models`와 로그에 표시됨) |
| `tokenizer_type` | string | 토크나이저 타입(예: `Qwen3` / `Qwen3VL` / `Gemma4VL` / `SmolLM2` …) |
| `url_tokenizer_model` | string | **로컬** 토크나이저 파일 경로(예: `qwen3_tokenizer.txt`). ⚠ 필드 이름에 `url`이 들어가고 기본값에 `http`가 들어가는 것은 모두 과거의 잔재입니다. **현재는 로컬 파일만 읽으며 HTTP는 지원하지 않습니다** |
| `template_filename_axmodel` | string | 레이어별 axmodel 파일 이름 템플릿으로, `%d`를 포함합니다(예: `qwen3_p128_l%d_together.axmodel`) |
| `axmodel_num` | int | transformer 레이어 수 |
| `filename_post_axmodel` | string | logits를 출력하는 post axmodel |
| `filename_tokens_embed` | string | token embedding 가중치(bf16 bin) |
| `tokens_embed_num` | int | 어휘 크기 |
| `tokens_embed_size` | int | embedding 차원 |

## 일반 / 토큰화

| 필드 | 기본값 | 설명 |
|---|---|---|
| `system_prompt` | 빈 값 | 기본 시스템 프롬프트입니다. **`run`(대화형) 모드에서만** 시스템 프롬프트가 없을 때 자동으로 앞에 추가합니다. **`serve` 모드에서는 자동으로 주입하지 않습니다**(OpenAI 의미 체계를 따라 system은 요청 측에서 제어합니다. 요청에 system이 없으면 system 부분도 없습니다) |
| `post_config_path` | `post_config.json` | 샘플링 설정 파일 |
| `bos` / `eos` | `true` / `false` | BOS/EOS 추가 여부 |
| `pad_token_id` | 0 | pad token id |
| `enable_thinking`(bool) 또는 `thinking_mode`(string) | 모델 기본값 | 사고 모드 스위치입니다. 아래 '사고 모드'를 참고하세요 |

### 사고 모드

**이번 턴의 생성에서 모델이 사고할지 여부**(thinking / reasoning)를 제어합니다. 스위치는 하나이며, 동등한 두 가지 표기 방법이 있습니다:

- `enable_thinking`(bool): `true`=사고함, `false`=사고하지 않음;
- `thinking_mode`(string): `think`=사고함, `no_think`=사고하지 않음, `default`/`auto`=모델 기본값을 따름.

둘 다 지정하면 `enable_thinking`이 우선하며, 둘 다 지정하지 않으면 모델 기본값을 사용합니다. `/v1/chat/completions` 요청 본문에서 **요청마다 재정의**할 수도 있으며(마찬가지로 두 키를 모두 지원하고, 최상위 또는 `chat_template_kwargs` 아래에 둘 수 있습니다), 요청이 끝나면 config 기본값으로 되돌아갑니다.

⚠ **현재 실제로 적용되는 토크나이저: Qwen3 계열 전체(Qwen3 / Qwen3VL / Qwen3Omni / Qwen2.5 / Qwen3.5), MiniCPM5.** 다른 토크나이저는 아직 이 스위치를 지원하지 않으며, 설정하면 **무시되고 로그에 경고가 한 번 출력됩니다**(더 이상 아무 표시 없이 무효화되지 않습니다). `no_think`는 각 모델의 공식 템플릿에 맞추는 방식으로 구현됩니다(예: Qwen3의 경우 생성 프롬프트 뒤에 빈 `<think>\n\n</think>` 블록을 주입합니다).

> 주의: 이는 `think_in_prompt`와 **별개입니다**. `think_in_prompt`는 "멀티턴 히스토리에서 assistant의 이전 사고 내용을 유지할지 여부"를 뜻하며, 토크나이저 타입에 따라 자동으로 결정되고(Gemma4/Gemma4VL만 false이며 나머지는 유지), **설정할 수 없고** 이 스위치와 관계가 없습니다.

## 로딩과 메모리

| 필드 | 기본값 | 설명 |
|---|---|---|
| `use_mmap_load_embed`(별칭 `b_use_mmap_load_embed`) | `false` | embedding을 mmap으로 로드(메모리 절약) |
| `use_mmap_load_layer`(별칭 `b_use_mmap_load_layer`) | `true`(AX650 전용) | 레이어 가중치에 mmap 사용 |
| `dynamic_load_enable` | `false` | 레이어 동적 로딩(CMM 절약, 속도 저하). README 참고 |
| `dynamic_load_pool_size` | `2` | 동적 로딩 시 상주하는 레이어 수(활성화했을 때만 적용) |
| **`mem_guard_enable`** | `true` | **로드 전 메모리 사전 점검의 마스터 스위치**(아래 '메모리 안전 사전 점검' 참고) |
| **`mem_guard_floor_mb`** | `128` | 추정 사용량 외에 추가로 확보하는 안전 여유분(MB) |
| **`mem_guard_on_unsafe`** | `prompt` | 안전하지 않을 때의 동작: `prompt`(TTY가 있으면 Y/N 확인 창 표시, TTY가 없으면 abort) / `abort` / `warn` |

### 메모리 안전 사전 점검(CMM/DDR 초과 로드로 인한 드라이버 크래시 방지)

활성화하면 **두 단계**로 남은 메모리를 확인하며, 어느 단계에서든 안전하지 않으면 `mem_guard_on_unsafe` 설정에 따라 처리합니다.

**① 로드 전(파일 크기로 추정)** — "명백히 들어가지 않는" 모델을 차단합니다:

- **CMM**(디바이스 메모리, 각 레이어 / post / 비전&오디오 인코더): AX650은 `/proc/ax_proc/mem_cmm_info`를 읽고, AXCL은 `axcl_GetCMMRemain`을 사용하며, 멀티 카드에서는 카드별로 따로 계산합니다.
- **DDR**(호스트 메모리, token embedding을 mmap으로 로드하지 않을 때 / Gemma per-layer 가중치): `/proc/meminfo`의 **MemAvailable**을 읽습니다(회수 가능한 buffer/cache를 이미 제외한 "실제 사용 가능한" 값이므로 오탐을 피할 수 있습니다).

**② 로드 중(실측 기반 외삽)** — 파일 크기만으로는 엔진이 각 레이어를 로드할 때 추가로 할당하는 KV/IO 버퍼를 추정할 수 없습니다(순수 가중치보다 약 ~30% 더 많고, 컨텍스트 길이에 따라 증가합니다). 따라서 레이어를 로드하는 동안 **실측한 레이어별 CMM 증가량**으로 "남은 레이어 + post 끝부분"을 외삽하고, floor를 넘을 것으로 예상되는 즉시 **할당 전에 중단합니다**(이 시점에는 처음 몇 개 레이어만 로드되었으므로 깔끔하게 회수할 수 있고 드라이버가 크래시되지 않습니다). 멀티 카드 병렬 로드 시에는 어느 한 카드에서 트리거되면 다른 카드도 중지합니다. 참고: 이 단계에서는 더 이상 대화형 확인 창을 띄우지 않으므로(①에서 이미 확인함) 여기서 `prompt`는 `abort`와 같습니다.

판정: `남은 용량 - 추정치 < mem_guard_floor_mb`인 경우 → `abort`는 즉시 중단하고 오류를 보고합니다. `warn`은 경고만 하고 계속 진행합니다. `prompt`는 대화형 터미널에서 `[y/N]`을 표시하며(기본값 N=로드하지 않음), 터미널이 없으면(serve/docker) `abort`로 대체됩니다.

- 끄기: `mem_guard_enable=false`.
- `dynamic_load_enable=true`: 레이어 가중치는 로드 후 바로 해제되므로 ①에서는 레이어 가중치를 계산에 넣지 않고, ②에서는 여전히 실측값으로 레이어 IO를 차단합니다.
- 멀티 슬롯(`kv_cache_slots>1`)의 N×KV 할당은 원래 알려진 실제 크기로 정확하게 예산을 잡으며, `mem_guard_floor_mb`를 예비 여유분으로 사용합니다(내장값 256MB/512MB와 비교해 더 큰 값을 적용).

## 어텐션(하이브리드 어텐션 / 긴 컨텍스트 모델)

| 필드 | 기본값 | 설명 |
|---|---|---|
| `full_attention_interval` | 0 | N번째 레이어마다(1-indexed) full-attention이고 나머지는 linear(예: Qwen3.5) |
| `layer_types` | 빈 값 | 레이어별 타입을 명시하는 배열(`full_attention`/`linear_attention`/`sliding_attention`) |
| `sliding_window` | 0 | 슬라이딩 윈도 크기 |
| `num_kv_shared_layers` | 0 | 끝부분에서 KV를 공유하는 레이어 수 |

> 이 항목들은 `config.json` 최상위에 쓰지 않아도 됩니다. `full_attention_interval` / `num_kv_shared_layers`는 대체 경로로 `text_config.*` 값을 읽고, `sliding_window` / `layer_types`는 설정되지 않은 경우 모델 디렉터리에 있는 토크나이저의 sidecar config에서 자동으로 읽습니다.

## 멀티 슬롯 프리픽스 KV 캐시(serve 다중 사용자/여러 프롬프트 가속)

| 필드 | 기본값 | 설명 |
|---|---|---|
| `kv_cache_slots` | 1 | 슬롯 수. 1=끄기(기존 동작과 동일) |
| `kv_cache_slot_location` | `device` | `device`(제로 카피 포인터 전환) / `host`(CMM 절약, 전환 시 복사) |

자세한 내용은 [multi_slot_kv_cache.md](multi_slot_kv_cache.md)를 참고하세요.

## VLM / 비전 / 오디오

| 필드 | 기본값 | 설명 |
|---|---|---|
| `vlm_type`(별칭 `VLM_TYPE`) | `None` | `Qwen2_5VL`/`Qwen3VL`/`InternVL3`/`FastVLM`/`SmolVLM2`/`PaddleOCRVL`/`Gemma4VL`/`MiniCPMV46VL` |
| `filename_image_encoder_axmodel` | — | 비전 인코더 axmodel(VLM에서는 필수) |
| `filename_audio_encoder_axmodel_5s` / `_30s` | — | Gemma4 오디오 인코더(ASR) |
| `vision_cache_dir` | 빈 값 | 비전 embedding 디스크 캐시 디렉터리 |
| `vision_width` / `vision_height` | 448 | 비전 입력 크기(설정하지 않으면 인코더 입력 형태에서 자동으로 추론) |
| `vision_patch_size` / `vision_temporal_patch_size` / `vision_spatial_merge_size` | 14 / 2 / 2 | patchify 매개변수 |
| `vision_fps` / `vision_tokens_per_second` | 1 / 1 | 비디오 시간 스케일링(Qwen2.5-VL mRoPE) |
| `vision_num_frames` / `vision_do_sample_frames` | 0 / true | 비디오 프레임 추출 상한 / 균등 프레임 추출 여부 |

> **비전 캐시(vision_cache) 환경 변수:**
> - `AXLLM_VISION_CACHE=0`: 비전 캐시(디스크+메모리)를 완전히 끕니다.
> - `AXLLM_VISION_MEM_CACHE_SIZE=<N>`: 메모리 캐시 항목 수의 상한입니다(기본값 8, LRU 방식으로 제거하며 장시간 실행되는 serve를 보호합니다).
> - `AXLLM_VISION_DISK_CACHE_MAX_MB=<MB>`: 디스크 캐시 디렉터리의 전체 용량 상한입니다(기본값 1024). 초과하면 mtime이 가장 오래된 `.bin`부터 제거합니다.
> - `AXLLM_VISION_DISK_CACHE_MIN_FREE_MB=<MB>`: 디스크에 쓰기 전에 필요한 최소 여유 공간입니다(기본값 300). 임계값보다 적으면 디스크 쓰기를 건너뛰어(메모리 캐시는 계속 사용) 디스크가 가득 차서 시스템 서비스가 멈추는 것을 방지합니다.

## Embedding

| 필드 | 기본값 | 설명 |
|---|---|---|
| `is_embedding`(별칭 `embedding`, 이전의 `embedding_type`/`EMBEDDING_TYPE`은 폐기됨) | `false` | Embedding 모드로 시작하여 `/v1/embeddings`를 제공합니다(`run`은 지원하지 않음) |

## Gemma4 per-layer 프로젝션

| 필드 | 설명 |
|---|---|
| `hidden_size_per_layer_input` | per-layer 프로젝션 차원(>0일 때 활성화) |
| `rms_norm_eps` | RMSNorm eps |
| `filename_tokens_embed_per_layer` / `filename_per_layer_model_projection` / `filename_per_layer_projection_norm` | per-layer 가중치 파일 |

## 서비스(serve)

| 필드 | 기본값 | 설명 |
|---|---|---|
| `port` | 8000 | 수신 대기 포트 |
| `server_timeout_ms` | 300000 | 요청 타임아웃(동시 요청 대기열에도 같은 값을 사용합니다). 명령줄 옵션 `--server_timeout_ms <ms>`로 재정의할 수도 있으며, serve 시작 시 적용된 값이 `server request/queue timeout: N ms` 형식으로 출력됩니다 |
| `server_default_max_tokens` | 0 | 요청에 max_tokens가 없을 때의 기본값(0=내장 기본값 사용) |
| `server_max_output_tokens` | 0 | 출력 token 수의 하드 상한(0=추가 제한 없음) |
| `server_forced_prompt_text` | — | 강제 프롬프트(예: OCR 정규화) |

## 샘플링 / 후처리(post_config.json)

샘플링 매개변수는 모델 디렉터리의 `post_config.json`에 둡니다(`config.json`과 같은 위치에 있으며 `post_config_path`로 지정합니다). 모든 키는 선택 사항이며, 없으면 기본값/비활성으로 처리합니다.

| 키 | 기본값 | 설명 |
|---|---|---|
| `enable_temperature` / `temperature` | false / 1.0 | 온도입니다. **주의**: 온도를 활성화했지만 `top_k`/`top_p`를 켜지 않은 경우 전체 분포에서 다항 분포 샘플링을 수행합니다(이전에는 greedy로 간주되어 무시되었습니다). `temperature<=0`은 greedy로 간주합니다. |
| `enable_top_k_sampling` / `top_k` | false / 1 | top-k 샘플링입니다(k는 어휘 크기를 넘지 않도록 자동으로 조정됩니다). |
| `enable_top_p_sampling` / `top_p` | false / 1.0 | nucleus 샘플링입니다. top_k와 함께 켜면 top_p가 우선합니다. |
| `enable_repetition_penalty` / `repetition_penalty` / `penalty_window` | false / 1.0 / 20 | 반복 페널티입니다(최근 `penalty_window`개의 token에만 적용됩니다). |
| `frequency_penalty` | 0.0 | OpenAI 방식의 빈도 페널티: `logit -= frequency_penalty × 출현 횟수`(`penalty_window` 윈도 안에서 집계합니다). 0이 아니면 활성화됩니다. |
| `presence_penalty` | 0.0 | OpenAI 방식의 존재 페널티: `logit -= presence_penalty`(윈도 안에 한 번이라도 나타났으면 한 번 뺍니다). 0이 아니면 활성화됩니다. |

**요청별 재정의(serve / OpenAI 호환 API):** 요청 본문의 `temperature`, `top_p`, `frequency_penalty`, `presence_penalty`는 해당 요청에 대해 config 기본값을 재정의하며, 요청이 끝나면 자동으로 원래 값으로 되돌아갑니다. 예:

```json
POST /v1/chat/completions
{ "model": "...", "messages": [ ... ],
  "temperature": 0.7, "frequency_penalty": 0.5, "presence_penalty": 0.3 }
```

> serve 의미 체계: 요청에 `temperature`와 `top_p`가 모두 없으면 해당 요청은 greedy로 처리됩니다(페널티는 여전히 argmax에 적용됩니다).

## AXCL(PCIe 멀티 카드)

| 필드 | 기본값 | 설명 |
|---|---|---|
| `devices` | `[0]` | 사용할 디바이스 id 목록(멀티 카드 텐서 병렬) |

> **환경 변수 재정의**: `AXLLM_DEVICES=0,1 axllm run/serve <model_dir>` 형태로 실행하면 config의 `devices`(쉼표로 구분한 디바이스 id)를 재정의하므로 config.json을 수정할 필요가 없습니다. 여러 카드에서 카드마다 모델 인스턴스를 하나씩 띄워 테스트할 때 주로 사용합니다. 예:
> ```sh
> AXLLM_DEVICES=0,1 axllm serve <dirA> &
> AXLLM_DEVICES=2,3 axllm serve <dirB> &
> ```
> AXCL 빌드에서만 유효합니다. 설정했더라도 유효한 id를 파싱할 수 없으면 config 값으로 돌아갑니다.

## 이미지 생성(SD1.5)

| 필드 | 설명 |
|---|---|
| `model_type` / `task_type` = `image_generation` 또는 `is_image_generation=true` | 이미지 생성 모드로 시작하여 `/v1/images/*` 엔드포인트를 제공합니다 |
| `image_model_dir` | 이미지 모델 루트 디렉터리 |
