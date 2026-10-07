# AX-LLM

[English](README_en.md) | [中文](README.md) | **한국어**

> 중국어 원문 [README.md](README.md)를 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

![GitHub License](https://img.shields.io/github/license/AXERA-TECH/ax-llm)

## 소개

**AX-LLM**은 **[Axera(爱芯元智)](https://www.axera-tech.com/)** 주도로 개발되고 있습니다. 이 프로젝트는 업계에서 널리 쓰이는 **LLM(Large Language Model)을** 기존 칩 플랫폼에 적용할 때의 실현 가능성과 관련 성능의 한계를 탐색하며, 커뮤니티 개발자가 **손쉽게** 자신의 **LLM 애플리케이션**을 **빠르게 평가**하고 **2차 개발**할 수 있도록 돕습니다.


### 지원 칩

- AX650A/AX650N/AX8850N/AX8850
  - SDK ≥ v3.16.0
- AX637R/AX8190
  - SDK ≥ v1.12.0
- AX630C
  - SDK ≥ v3.0.0

### 지원 모델

#### LLM
- Qwen2.5
- Qwen3
- MiniCPM
- SmolLM2
- Gemma4
- Llama3
- HY-MT1.5-1.8B
- EmbeddingGemma 2(텍스트 임베딩, `model_type: embedding_gemma2`, [docs/ko/configuration.md](docs/ko/configuration.md) 참고, 모델: [jonpark0/embeddinggemma-2-AX650](https://huggingface.co/jonpark0/embeddinggemma-2-AX650))
- ...

#### VLM(멀티모달)
- Qwen3-VL-2B-Instruct
- Qwen3.5-2B
- Qwen3-VL-Embedding-2B(Embedding, 멀티모달)
- SmolVLM2-500M-Video-Instruct
- FastVLM-1.5B-GPTQ-Int4
- InternVL3_5-1B-GPTQ-INT4
- PaddleOCR-VL-1.5
- ...

### 다운로드 위치

저희 ModelZoo는 [Huggingface](https://huggingface.co/AXERA-TECH)로 이전되었습니다.

## 현재 브랜치(axllm)

이 브랜치는 실행 파일 이름을 `axllm`으로 통일하여 출력하며, 실행 환경에 따라 AX650 온칩 백엔드 또는 AXCL PCIe 백엔드를 자동으로 선택합니다.

> **알려진 제한 사항**: `gemma-4`(예: `gemma-4-E2B-it-GPTQ-INT4`)는 현재 **AX650 온칩 백엔드**에서만 정상적으로 추론됩니다. **AXCL PCIe 백엔드**에서는 깨진 문자나 반복 출력이 발생합니다([issue #39](https://github.com/AXERA-TECH/ax-llm/issues/39)). 원인은 AXCL 런타임이 gemma-4의 레이어 간 shared-KV(뒤쪽 20개 레이어가 앞쪽 레이어의 KV 캐시를 재사용)를 실행할 때 발생하는 수치 발산으로 확인되었습니다. 온칩 결과는 정확하고, 양쪽 모델 파일의 md5가 일치하며, ax-llm의 데이터 입력 로직도 동일하므로 차이는 런타임 측에 있으며, 현재 ax-llm 측의 수정은 없습니다. gemma-4를 사용하려면 온칩 AX650 백엔드를 사용하세요.

### 설치 방법(권장)

루트 디렉터리의 설치 스크립트를 사용합니다:

```shell
./install.sh
```

Windows + AXCL + MinGW64 환경에서는 다음을 사용할 수 있습니다:

```bat
install.bat
```

또는 한 줄 명령으로 다운로드하여 실행할 수 있습니다(기본 브랜치 `axllm`):

```shell
curl -fsSL https://raw.githubusercontent.com/AXERA-TECH/ax-llm/axllm/install.sh | bash
```

스크립트 동작 방식:

- **AX650 온칩 백엔드**
  - 조건: `/proc/ax_proc/board_id`에 `AX650`이 포함되어 있고 로컬에 `gcc`가 있음
  - 동작: BSP(msp_3.6.2)를 자동으로 다운로드하고 빌드하여 `/usr/bin/axllm`에 설치
- **AXCL PCIe 백엔드**
  - 조건: `axcl-smi`를 실행할 수 있고 `/usr/include/axcl/`과 `/usr/lib/axcl/`이 존재함
  - 동작: 시스템의 AXCL 헤더 파일과 라이브러리로 백엔드를 빌드하여 `/usr/bin/axllm`에 설치

기본적으로 현재 저장소의 `origin`에서 **axllm 브랜치**를 가져옵니다(환경 변수로 재정의할 수 있습니다):

```shell
REPO_URL=git@github.com:AXERA-TECH/ax-llm.git BRANCH=axllm ./install.sh
```

제거:

```shell
./uninstall.sh
```

Windows 로컬 설치의 경우 다음을 사용할 수 있습니다:

```bat
uninstall.bat
```

### 빌드 방법(수동)

수동으로 빌드해야 하는 경우 백엔드에 맞는 스크립트를 선택하세요:

```shell
# AX650 온칩 백엔드
./build_ax650.sh

# AXCL PCIe 백엔드(x86)
./build_axcl_x86.sh

# AXCL PCIe 백엔드(aarch64 크로스 컴파일)
./build_axcl_aarch64.sh
```

`build_ax650.sh`는 크로스 컴파일 시 호스트 아키텍처용 OpenCV가 잘못 링크되는 것을 막기 위해 기본적으로 OpenCV를 비활성화하고 내장 SimpleCV를 사용합니다. 대상 아키텍처에서 사용할 수 있는 OpenCV를 준비했다면 이 기본값을 재정의할 수 있습니다:

```shell
AXLLM_CMAKE_ARGS="-DCMAKE_DISABLE_FIND_PACKAGE_OpenCV=FALSE" ./build_ax650.sh
```

빌드가 끝나면 `build*/install/bin/` 디렉터리에 `axllm`이 생성됩니다(이 브랜치에서 이름을 통일했습니다).

Windows에서 AXCL 백엔드를 빌드할 때는 `BUILD_AX650`을 명시적으로 끄고 `AXCL_DIR`을 전달하세요:

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

## 사용 방법

빌드/설치 후 실행합니다:

```shell
axllm
```

API/Gradio 예제가 필요하면 `scripts/` 아래의 스크립트를 계속 사용할 수 있습니다(이 브랜치의 기능과 일치합니다).

## 설정 파일

모델 디렉터리 `config.json`의 **전체 필드 설명**은 **[docs/ko/configuration.md](docs/ko/configuration.md)** 문서를 참고하세요(필수 항목, 동적 로딩, 멀티 슬롯 KV 캐시, 메모리 안전 사전 점검, VLM/비전, Embedding, 서비스, AXCL 멀티 카드 등).

> **메모리 안전 사전 점검**(`mem_guard_enable`)은 기본적으로 켜져 있습니다. 모델을 로드하기 전에 파일 크기로 사용량을 추정하여 남은 CMM/DDR과 비교하고, 들어가지 않으면 경고하거나 중단하여(Y/N 확인 창을 띄울 수 있음) 용량을 초과하는 모델을 강제로 로드하다가 드라이버가 크래시되는 것을 방지합니다. 자세한 내용은 설정 문서를 참고하세요.

### Docker(CI에서 내보낸 이미지)

이 저장소는 Docker 이미지를 빌드하고 내보내는 GitHub Actions 워크플로 `Docker Images`를 제공하며(백엔드/아키텍처별로 분리), 산출물은 다음 두 가지 형식으로 함께 제공됩니다:

- `.tar`(비압축, 대상 장치에 gzip이 없는 경우에 적합)
- `.tar.gz`(압축 파일, 크기가 더 작음)

- `docker-axcl-amd64`: AXCL(PCIe) x86_64 호스트
- `docker-axcl-arm64`: AXCL(PCIe) aarch64 호스트
- `docker-ax650-arm64`: AX650(보드) aarch64

artifact를 다운로드한 후 이미지를 로드합니다:

```shell
docker load -i axllm-docker-axcl-amd64.tar
docker load -i axllm-docker-axcl-amd64.tar.gz
```

고정 링크에서 다운로드할 수도 있습니다(항상 `axllm` 브랜치의 최신 빌드 산출물을 가리킵니다):

```shell
# Docker 이미지
curl -L -o axllm-docker-axcl-amd64.tar https://github.com/AXERA-TECH/ax-llm/releases/latest/download/axllm-docker-axcl-amd64.tar

# 빌드 산출물(Docker 아님)
curl -L -o axllm-axcl-linux-amd64 https://github.com/AXERA-TECH/ax-llm/releases/latest/download/axllm-axcl-linux-amd64
```

이미지를 GHCR에 푸시하려면 Actions에서 `Docker Images`를 수동으로 실행하면서 `publish_ghcr=true`로 설정하세요. 이후 `ghcr.io/<owner>/`에서 pull할 수 있습니다:

```shell
docker pull ghcr.io/<owner>/axllm-axcl-amd64:<sha>
```

AXCL 실행 예시(호스트에 AXCL Driver가 설치되어 있어야 하며 디바이스 노드를 패스스루해야 합니다. 가장 간단한 방법은 `--privileged`를 사용하는 것입니다):

```shell
docker run --rm --network host --privileged \
  -v /path/to/models:/models \
  axllm-axcl-amd64:<sha> serve /models/<model_dir> --port 8000
```

AX650 실행 예시(보드에서 실행하며, 런타임 라이브러리를 제공하는 `/soc`를 마운트해야 합니다):

```shell
docker run --rm --network host --privileged \
  -v /soc:/soc:ro \
  -e LD_LIBRARY_PATH=/soc/lib:$LD_LIBRARY_PATH \
  -v /path/to/models:/models \
  axllm-ax650-arm64:<sha> serve /models/<model_dir> --port 8000
```

### VLM 사용 안내

- VLM 모델 디렉터리로 `axllm run <vlm_model_path>`를 실행합니다
- 매 턴 `prompt`를 입력하면 `image >>` 입력 프롬프트가 표시됩니다
  - 바로 Enter: 이번 턴은 텍스트로만 대화
  - 이미지 경로 입력: 이미지-텍스트 대화
  - `video:<frames_dir>` 입력: 비디오/다중 프레임 대화(파일 이름 순으로 프레임을 읽음)
  - `video:<video_file>[:<fps>]` 입력: 단일 비디오 파일에서 샘플링 FPS에 따라 프레임을 균등하게 추출하며, 기본값은 `fps=2`이고 명시적인 `fps`는 양의 정수 또는 양의 소수를 지원

VLM 모델의 `config.json`에는 다음 필드(또는 이에 해당하는 필드)가 있어야 합니다:

- `vlm_type`
- `filename_image_encoder_axmodel`

### 동적 로딩(CMM 사용량 절감)

CMM이 작은 장치에서는 동적 로딩을 켤 수 있습니다. 실행 중에는 소수 레이어의 가중치만 상주시키고 나머지는 필요할 때 로드하며 풀이 가득 차면 해제하므로 CMM을 크게 절약합니다(대신 속도가 느려집니다). `dynamic_load_enable` / `dynamic_load_pool_size`를 설정하세요. 자세한 내용은 [설정 문서](docs/ko/configuration.md)를 참고하세요.

### 멀티 슬롯 프리픽스 KV 캐시(serve 다중 사용자 / 여러 프롬프트 세트 가속)

`serve`는 단일 인스턴스에서 요청을 순차적으로 처리하므로, 여러 사용자나 여러 세트의 시스템 프롬프트가 번갈아 요청하면 기본적으로 컨텍스트가 하나뿐이어서 prefill을 반복해서 다시 실행하게 됩니다. 멀티 슬롯을 켜면 여러 개의 프리픽스 KV를 캐시할 수 있으며, 최장 공통 프리픽스로 적중하면 재사용하고 적중하지 않으면 LRU 슬롯을 덮어씁니다(텍스트와 VLM 모두 지원하며, VLM은 재사용 시 비전 인코더를 건너뜁니다). `kv_cache_slots` / `kv_cache_slot_location`을 설정하세요. 자세한 내용은 [설정 문서](docs/ko/configuration.md)와 [설계 문서](docs/ko/multi_slot_kv_cache.md)를 참고하세요.

### Embedding(/v1/embeddings) 사용 안내

`axllm`은 `serve` 모드에서 Embedding 모델을 로드하고 OpenAI 호환 `/v1/embeddings` 인터페이스를 제공합니다(Embedding 모델은 `run` 대화형 모드를 **지원하지 않습니다**).

llama.cpp 호환 Embedding 경로도 함께 제공합니다:

- `POST /embedding`: 단일 embedding(llama.cpp 스타일, `{ "embedding": [...], "model": "..." }` 반환)
- `POST /embeddings`: 배치 embedding(llama.cpp 스타일, `[{ "index": 0, "embedding": [...] }, ...]` 반환)

1) Embedding 모델 디렉터리의 `config.json`에서 스위치를 켭니다:

```json
{
  "is_embedding": true
}
```

2) 서비스를 시작합니다:

```shell
axllm serve <embedding_model_dir> --port 8000
```

시작하면 터미널에 이 머신에 접근할 수 있는 전체 API URL이 출력됩니다(`127.0.0.1`과 이 머신의 네트워크 카드 IP 포함).

3) 호출 예시:

```shell
# 헬스 체크
curl -s http://127.0.0.1:8000/health

# 등록된 모델 목록 조회
curl -s http://127.0.0.1:8000/v1/models

# embedding 생성(input은 string 또는 string 배열 지원)
curl -s http://127.0.0.1:8000/v1/embeddings \
  -H 'Content-Type: application/json' \
  -d '{"model":"<model_name>","input":["hello","world"]}'
```

Python 예제도 참고할 수 있습니다: `python3 scripts/openai_embedding_demo.py --model <model_name> --api_url http://127.0.0.1:8000/v1`(먼저 `pip install openai`를 실행해야 합니다).

#### 멀티모달 Embedding(messages 확장)

비전 입력을 지원하는 Embedding 모델(예: `Qwen3-VL-Embedding-2B`)의 경우 `/v1/embeddings`에 `messages`(형식은 `/v1/chat/completions`와 동일)를 추가로 전달할 수 있어 이미지-텍스트 embedding을 구현할 수 있습니다:

```shell
python3 scripts/openai_vl_embedding_demo.py \
  --model <model_name> \
  --api_url http://127.0.0.1:8000/v1 \
  --image /path/to/image.jpg \
  --text "Describe this image"
```

## 실행 예시
### 명령줄 대화
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

### 서비스(OpenAI API 호환)

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
### OpenAI API 테스트

텍스트 전용 대화:
```shell
python scripts/openai_demo.py --model AXERA-TECH/SmolLM2-360M-Instruct --api_url http://127.0.0.1:8000/v1
```

사용자 지정 prompt:
```shell
python scripts/openai_demo.py --model AXERA-TECH/SmolLM2-360M-Instruct --prompt "请介绍一下你自己"
```

이미지-텍스트 대화(VLM, 이미지는 자동으로 base64 인코딩되어 전송됩니다):
```shell
python scripts/openai_demo.py --model AXERA-TECH/Qwen3-VL-2B-Instruct --image /path/to/image.jpg --prompt "描述一下这张图片"
```

오디오 대화(Gemma4, 현재 메시지당 오디오 파일 1개를 지원합니다):
```shell
python scripts/openai_demo.py --model AXERA-TECH/gemma-4-E2B-it --audio /path/to/audio.wav --prompt "Transcribe the speech in its original language. Output only the transcription."
```

오디오 전사(OpenAI Audio API):
```shell
curl http://127.0.0.1:8000/v1/audio/transcriptions \
  -F "model=AXERA-TECH/gemma-4-E2B-it" \
  -F "file=@/path/to/audio.wav" \
  -F "response_format=json"
```

오디오 번역(OpenAI Audio API):
```shell
curl http://127.0.0.1:8000/v1/audio/translations \
  -F "model=AXERA-TECH/gemma-4-E2B-it" \
  -F "file=@/path/to/audio.wav" \
  -F "response_format=text"
```

`response_format`은 현재 `json`, `text`, `srt`, `vtt`를 지원합니다.

> **매개변수 설명**
> | 매개변수 | 설명 | 기본값 |
> |------|------|--------|
> | `--model` | 모델 이름(필수) | — |
> | `--api_url` | API 주소 | `http://127.0.0.1:8000/v1` |
> | `--prompt` | 사용자 prompt 텍스트 | `hello` |
> | `--image` | 이미지 경로(선택, VLM 모드) | — |
> | `--audio` | 오디오 경로(선택, Gemma4 모드) | — |
>
> `--image` 또는 `--audio`를 지정하면 스크립트가 미디어를 읽어 `data:...;base64,...` 형식으로 요청에 포함하고, 서버는 이를 자동으로 임시 파일로 디코딩하여 해당 인코더가 사용할 수 있도록 합니다.

## 기술 토론

- Github issues
- QQ 그룹: 139953715
