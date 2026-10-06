# Multi-Slot Prefix KV Cache

**English** | [中文](../multi_slot_kv_cache.md) | [한국어](../ko/multi_slot_kv_cache.md)

> Translated from the Chinese [original](../multi_slot_kv_cache.md). If the two differ, the original is authoritative.

Branch: `ax-prefix-cache-multi-slot`

## Background / motivation

`serve` mode is **single-instance with serial processing** (`setMaxConcurrency(1)`), but multiple users / multiple system prompts send requests in turn.
Currently there is only **one** context (`last_tokens_ids` + `precompute_len` + one copy of K/V on the device), so as soon as a request's prefix differs from the previous one, `ResetKVCache` is triggered and the full prefill is recomputed, which is very slow.

The OpenAI chat protocol is stateless (the full history is sent every time), so **no session id is needed**: the token prefix itself is the key.
Different users/system prompts → different prefixes → land in different slots; multiple turns of the same conversation → the new prefix starts with the old prefix → hit their own slot.

## Goals

- Cache **multiple** copies of prefix tokens and their KV (slots).
- The number of copies is set in config; still only one request is processed at a time.
- Each request is matched to a slot by the token **longest common prefix**; on a hit, that slot's KV is reused and only the increment is prefilled; on a miss, the **least recently used** (LRU) slot is overwritten and the whole sequence is prefilled.
- The KV memory location is configurable:
  - `device`: one device K/V buffer per slot; on activation the engine input tensors are **rebound with zero copy**, so switching is fastest; the cost is N× device CMM.
  - `host` (DDR): a single device K/V plus one host K/V per slot; on activation the old slot is copied D2H and the new slot H2D; **saves CMM** (only 1 device buffer no matter how many slots), but switching has copy overhead.
- Both text LLM and **VLM are supported**. For VLM, the vision encoding result (vision embedding) stays resident in the slot together with the KV, so follow-up questions/reuse **skip re-running the vision encoder**.

## Configuration (model directory `config.json`)

| Key | Type | Default | Description |
|---|---|---|---|
| `kv_cache_slots` | int | `1` | Number of prefix cache slots. `1` = exactly the current behavior (multi-slot off). |
| `kv_cache_slot_location` | string | `device` | `device` (zero-copy pointer switching) / `host` (to be supported later). |

The default is `kv_cache_slots=1`, which has zero impact on existing models. Multi-slot is enabled only when it is explicitly set to `>1`.

### Device/host memory adaptation (downgrade + warn if it does not fit)

When multi-slot is enabled, the runtime **estimates first, then allocates**, to avoid OOM caused by an overly large config:

- device mode: first build slot metadata for each layer and compute the **device CMM usage of one slot** (K+V of every layer, summed per device), query the remaining CMM of each card (`get_remaining_cmm_size` / `axcl_GetCMMRemain`), keep a 256MB margin, and compute the maximum number of slots that fit; take the minimum across devices, capped by the config value. If the actual allocation falls short because of fragmentation, all layers are trimmed to the number of slots that were actually allocated successfully.
- host mode: estimated from free host memory (`sysinfo`, keeping a 512MB margin).
- If the number of slots that can actually be allocated is **< the config request**, a `⚠` warning is logged and multi-slot is enabled with the number that fits; if not even one fits, multi-slot is turned off. For example, if 4 slots are configured but the card only has room for 3, 3 slots are used and a warning is logged.

Example log:
```
kv slot budget: dev=0 per_slot=139MB free=6474MB margin=256MB -> max_slots=44
⚠ kv_cache_slots=9999 requested but device CMM only fits 44; reducing to 44
```

## Design

### What a "slot" contains

It replicates all the state of the existing single context:

- Device side: one `K_cache` / `V_cache` device buffer per layer (device mode; slot0 reuses the engine's native buffer, slot1..N-1 are allocated additionally).
- Host side (one copy per slot, stored in `LLM::Impl`):
  - `last_tokens_ids`, `precompute_len`
  - `linear_state_snapshots_` (linear attention rollback snapshots)
  - `cached_mrope_next_pos`, `full_cache_valid_slots_` / `full_cache_has_sparse_slots_`
  - VLM: records related to `vision_state` (v1 mainly guarantees text LLM; VLM is included with caution)
  - LRU timestamp

### Key idea: activation is rebinding, KV is never copied between slots

Both backends have the same KV layout: all shape groups **share the same** `K_cache`/`V_cache` device buffer (allocated by group0, the others alias it).
- The engine reads the buffer address from `io_data[grp].pInputs[]` (AX650) / `axcl_EngineSetInputBufferByIndex` (AXCL).
- `LLM.cpp` reads the address from `get_input(grpid,"K_cache").{phyAddr,pVirAddr}` (AXCL uses `phyAddr`, AX650 uses `pVirAddr`).

Therefore "activate slot S" = take the `K_cache`/`V_cache` inputs of every group and **rebind them to slot S's buffer** (while also updating `io_data` / SetInputBuffer and the `mgroup_input_tensors` descriptors, and rebuilding the map).
After that, **all existing prefill/decode/SetKVCache code works unchanged on the currently bound buffer**, so it naturally writes into slot S, with zero copy between slots.

### New backend interface (`ax_runner_base`, default `-1` = not supported)

```cpp
virtual int kv_cache_slots_init(int num_slots);   // allocate num_slots-1 extra buffers of equal size for each of K_cache/V_cache
virtual int kv_cache_slots_activate(int slot);    // rebind K_cache/V_cache of all groups to this slot
virtual int kv_cache_slots_count() const;         // return the number of allocated slots (default 1)
```

- AX650: `AX_SYS_MemAllocCached` allocates the slot buffers; activate rewrites `phyAddr`+`pVirAddr` in `io_data[grp].pInputs[*]` and `mgroup_input_tensors[grp][*]`.
- AXCL: `axcl_Malloc` allocates the device buffers; activate rewrites `mgroup_input_tensors[grp][*].phyAddr` and calls `axcl_EngineSetInputBufferByIndex` for each group.
- One runner per layer, each managing its own N copies of K/V.

### Engine side (`LLM::Impl`)

- Add `struct KvSlot { ... }` plus `std::vector<KvSlot> slots_`, `int active_slot_`, and a monotonically increasing `lru_tick_`.
- At the request entry point (`Run(history,...)`), after tokenization and before entering the existing prefix-reuse logic:
  1. For each slot, use `diff_token_ids` to compute the common prefix length with `new_tokens`, and pick the longest one that is `>0` as the hit slot; if all are 0, pick the LRU slot and `ResetKVCache` (resetting only that slot).
  2. For each layer, call `runner.kv_cache_slots_activate(slot)`; load that slot's host state into the working variables (`last_tokens_ids/precompute_len/...`).
  3. Go through the existing append/rollback/prefix-reuse/recompute logic.
  4. When the run finishes, write the updated host state back to that slot and refresh the LRU.
- Remove the never-enabled `b_os_kvcache` bypass; `Get/SetKVCache` is simplified to the device path.

### Constraints / limits (v1)

- Not enabled together with `dynamic_load_enable` for now (dynamically swapping handles resets the IO bindings); if both are configured, a warning is logged and it falls back to a single slot.
- Multi-card TP: slot buffers are allocated on the card that holds each layer.
- VLM: first make sure text and "text prefix" reuse are correct; requests with vision injection work with the current logic, but vision results are not reused across slots.

## Testing

- AXCL: `10.126.126.1` (8× AX650N PCIe), `build_axcl_x86.sh`.
- AX650 board: `10.126.35.191` / `10.126.35.234`, cross-compile with `build_ax650.sh`, then push and run.
- Cases: multiple system prompts A/B/C send serve requests in turn → TTFT drops significantly on slot hits; once the number of slots is exceeded, LRU overwrites the least recently used one; on both backends, correctness and numerical results are consistent.
