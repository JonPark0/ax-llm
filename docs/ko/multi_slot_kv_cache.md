# 멀티 슬롯 프리픽스 KV 캐시(Multi-Slot Prefix KV Cache)

[English](../en/multi_slot_kv_cache.md) | [中文](../multi_slot_kv_cache.md) | **한국어**

> 중국어 [원문](../multi_slot_kv_cache.md)을 번역한 문서입니다. 내용이 다르면 원문을 기준으로 합니다.

브랜치: `ax-prefix-cache-multi-slot`

## 배경 / 동기

`serve` 모드는 **단일 인스턴스, 직렬 처리**(`setMaxConcurrency(1)`)이지만, 여러 사용자 / 여러 세트의 시스템 프롬프트가 번갈아 가며 요청합니다.
현재는 컨텍스트가 **하나**(`last_tokens_ids` + `precompute_len` + 디바이스상의 K/V 한 벌)뿐이므로, 요청의 프리픽스가 직전 요청과 다르면 곧바로 `ResetKVCache`가 트리거되어 prefill 전체를 다시 계산하게 되고, 이는 매우 느립니다.

OpenAI chat 프로토콜은 stateless(매번 전체 history를 전송)이므로 **session id가 필요하지 않습니다**. token 프리픽스 자체가 key입니다.
다른 사용자/시스템 프롬프트 → 다른 프리픽스 → 다른 슬롯에 배치됩니다. 같은 대화의 여러 턴 → 새 프리픽스가 이전 프리픽스로 시작함 → 자신의 슬롯에 히트합니다.

## 목표

- 프리픽스 token과 그 KV를 **여러 벌** 캐시합니다(슬롯, slot).
- 슬롯 개수는 config로 설정하며, 한 번에 처리하는 요청은 여전히 하나뿐입니다.
- 각 요청은 token의 **최장 공통 프리픽스**로 슬롯을 매칭합니다. 히트하면 해당 KV를 재사용하고 증분에 대해서만 prefill을 수행하며, 히트하지 않으면 **가장 오랫동안 사용되지 않은**(LRU) 슬롯을 덮어쓰고 전체를 prefill합니다.
- KV 메모리 위치를 설정할 수 있습니다:
  - `device`: 슬롯마다 디바이스 K/V 버퍼를 한 벌씩 두고, 활성화 시 엔진 입력 텐서를 **제로 카피로 재바인딩**하므로 전환이 가장 빠릅니다. 대가는 N배의 디바이스 CMM입니다.
  - `host`(DDR): 디바이스 K/V 한 벌 + 슬롯마다 호스트 K/V 한 벌을 두고, 활성화 시 이전 슬롯은 D2H, 새 슬롯은 H2D로 복사합니다. **CMM을 절약**하지만(슬롯 수와 관계없이 디바이스 버퍼 1벌만 차지), 전환 시 복사 오버헤드가 있습니다.
- 텍스트 LLM과 **VLM을 모두 지원**합니다. VLM에서는 비전 인코딩 결과(vision embedding)가 KV와 함께 슬롯에 상주하므로, 후속 질문/재사용 시 **vision encoder 재실행을 건너뜁니다**.

## 설정(모델 디렉터리 `config.json`)

| 키 | 타입 | 기본값 | 설명 |
|---|---|---|---|
| `kv_cache_slots` | int | `1` | 프리픽스 캐시 슬롯 수입니다. `1` = 현재 동작과 완전히 동일(멀티 슬롯 비활성화). |
| `kv_cache_slot_location` | string | `device` | `device`(제로 카피 포인터 전환) / `host`(향후 지원). |

기본값은 `kv_cache_slots=1`이며 기존 모델에는 전혀 영향을 주지 않습니다. 명시적으로 `>1`로 설정한 경우에만 활성화됩니다.

### 디바이스 메모리/호스트 메모리 자동 조정(공간이 부족하면 축소 + 경고)

멀티 슬롯을 켜면 **먼저 평가한 뒤 할당**하여, 설정값이 너무 커서 OOM이 발생하는 것을 방지합니다:

- device 모드: 먼저 레이어마다 슬롯 메타데이터를 만들고 **슬롯 한 벌의 디바이스 CMM 사용량**(각 레이어의 K+V를 디바이스별로 합산)을 계산한 다음, 각 카드의 남은 CMM(`get_remaining_cmm_size` / `axcl_GetCMMRemain`)을 조회하고 256MB 여유분을 남긴 상태에서 최대 몇 개의 슬롯을 열 수 있는지 계산합니다. 디바이스별 값 중 최솟값을 취하고 config 값을 상한으로 둡니다. 실제 할당 시 단편화로 인해 공간이 부족하면 모든 레이어를 실제로 할당에 성공한 개수로 잘라 냅니다.
- host 모드: 사용 가능한 호스트 메모리(`sysinfo`, 512MB 여유분 유지)를 기준으로 추정합니다.
- 실제로 열 수 있는 슬롯 수가 **config 요청값보다 작으면** `⚠` 경고를 출력하고 열 수 있는 개수만큼 활성화합니다. 한 개도 열 수 없으면 멀티 슬롯을 끕니다. 예를 들어 4개로 설정했지만 카드에 3개만 들어간다면 3개를 사용하고 경고를 출력합니다.

예시 로그:
```
kv slot budget: dev=0 per_slot=139MB free=6474MB margin=256MB -> max_slots=44
⚠ kv_cache_slots=9999 requested but device CMM only fits 44; reducing to 44
```

## 설계

### 하나의 "슬롯"에 포함되는 것

기존 단일 컨텍스트의 모든 상태를 복제합니다:

- 디바이스 측: 레이어마다 `K_cache` / `V_cache` 디바이스 버퍼 한 벌(device 모드. slot0은 엔진 고유의 버퍼를 재사용하고, slot1..N-1은 추가로 할당합니다).
- 호스트 측(슬롯마다 한 벌씩, `LLM::Impl`에 둡니다):
  - `last_tokens_ids`, `precompute_len`
  - `linear_state_snapshots_`(linear attention 롤백 스냅샷)
  - `cached_mrope_next_pos`, `full_cache_valid_slots_` / `full_cache_has_sparse_slots_`
  - VLM: `vision_state` 관련 기록(v1은 주로 텍스트 LLM을 보장하며, VLM은 신중하게 포함합니다)
  - LRU 타임스탬프

### 핵심 아이디어: 활성화가 곧 재바인딩이며, KV는 슬롯 간에 절대 복사하지 않음

두 백엔드의 KV 레이아웃은 동일합니다. 모든 shape group이 `K_cache`/`V_cache` 디바이스 버퍼 **한 벌을 공유**합니다(group0이 할당하고 나머지는 alias).
- 엔진은 `io_data[grp].pInputs[]`(AX650) / `axcl_EngineSetInputBufferByIndex`(AXCL)에서 버퍼 주소를 읽습니다.
- `LLM.cpp`는 `get_input(grpid,"K_cache").{phyAddr,pVirAddr}`에서 주소를 읽습니다(AXCL은 `phyAddr`, AX650은 `pVirAddr`를 사용).

따라서 "슬롯 S 활성화" = 각 group의 `K_cache`/`V_cache` 입력을 **슬롯 S의 버퍼로 재바인딩**하는 것입니다(동시에 `io_data` / SetInputBuffer와 `mgroup_input_tensors` 디스크립터를 갱신하고 map을 재구성합니다).
이후 **기존의 모든 prefill/decode/SetKVCache 코드는 현재 바인딩된 버퍼를 그대로 다루므로**, 자연스럽게 슬롯 S에 기록되며 슬롯 간에는 제로 카피입니다.

### 백엔드 신규 인터페이스(`ax_runner_base`, 기본값 `-1`=미지원)

```cpp
virtual int kv_cache_slots_init(int num_slots);   // K_cache/V_cache 각각에 대해 같은 크기의 버퍼를 num_slots-1개 추가 할당
virtual int kv_cache_slots_activate(int slot);    // 모든 group의 K_cache/V_cache를 해당 슬롯으로 재바인딩
virtual int kv_cache_slots_count() const;         // 할당된 슬롯 수를 반환(기본값 1)
```

- AX650: `AX_SYS_MemAllocCached`로 슬롯 버퍼를 할당하고, activate는 `io_data[grp].pInputs[*]`와 `mgroup_input_tensors[grp][*]`의 `phyAddr`+`pVirAddr`를 다시 씁니다.
- AXCL: `axcl_Malloc`으로 디바이스 버퍼를 할당하고, activate는 `mgroup_input_tensors[grp][*].phyAddr`를 다시 쓴 뒤 각 group에 대해 `axcl_EngineSetInputBufferByIndex`를 호출합니다.
- 레이어마다 runner가 하나씩 있으며, 각자 자신의 K/V N벌을 관리합니다.

### 엔진 측(`LLM::Impl`)

- `struct KvSlot { ... }`와 `std::vector<KvSlot> slots_`, `int active_slot_`, 단조 증가하는 `lru_tick_`를 추가합니다.
- 요청 진입점(`Run(history,...)`)에서 tokenize한 후, 기존 프리픽스 재사용 로직에 들어가기 전에:
  1. 각 슬롯에 대해 `diff_token_ids`로 `new_tokens`와의 공통 프리픽스 길이를 구하고, 가장 길면서 `>0`인 슬롯을 히트 슬롯으로 선택합니다. 모두 0이면 LRU 슬롯을 선택하고 `ResetKVCache`를 수행합니다(해당 슬롯만 리셋).
  2. 각 레이어에 대해 `runner.kv_cache_slots_activate(slot)`를 호출하고, 해당 슬롯의 호스트 상태를 작업 변수(`last_tokens_ids/precompute_len/...`)로 로드합니다.
  3. 기존 append/rollback/prefix-reuse/recompute 로직을 수행합니다.
  4. 실행이 끝나면 갱신된 호스트 상태를 해당 슬롯에 다시 기록하고 LRU를 갱신합니다.
- 한 번도 활성화된 적 없는 `b_os_kvcache` 우회 경로를 삭제하고, `Get/SetKVCache`를 디바이스 경로로 단순화합니다.

### 제약 / 경계 조건(v1)

- 당분간 `dynamic_load_enable`과 동시에 활성화하지 않습니다(handle을 동적으로 교체하면 IO 바인딩이 리셋됩니다). 둘 다 설정하면 경고를 출력하고 단일 슬롯으로 되돌아갑니다.
- 멀티 카드 TP: 슬롯 버퍼는 해당 레이어가 위치한 카드에 할당합니다.
- VLM: 우선 텍스트와 "텍스트 프리픽스" 재사용이 올바르게 동작하도록 보장합니다. 비전 주입이 포함된 요청은 현재 로직대로 동작하지만, 비전 결과를 슬롯 간에 재사용하지는 않습니다.

## 테스트

- AXCL: `10.126.126.1`(8× AX650N PCIe), `build_axcl_x86.sh`.
- AX650 보드: `10.126.35.191` / `10.126.35.234`, `build_ax650.sh`로 크로스 컴파일한 뒤 보드에 푸시하여 실행합니다.
- 테스트 케이스: 시스템 프롬프트 A/B/C 세트로 번갈아 serve 요청 → 히트한 슬롯의 TTFT가 크게 감소합니다. 슬롯 수를 초과하면 LRU에 따라 가장 오랫동안 사용되지 않은 슬롯을 덮어씁니다. 두 백엔드 모두에서 정확성과 수치가 일치합니다.
