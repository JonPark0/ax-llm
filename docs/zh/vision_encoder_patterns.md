# 视觉/VLM 分支模式（笔记）

[English](../vision_encoder_patterns.md) | **中文** | [한국어](../ko/vision_encoder_patterns.md)

> 本文译自英文[原文](../vision_encoder_patterns.md)，如有出入以原文为准。

本仓库的 `axllm` 分支目前按如下方式运行**纯文本**模型：

- `tokenizer->encode(history)` 得到 token id
- 相对 `last_tokens_ids` 计算 `tokens_diff`（仅追加的快速路径）
- `SetKVCache(k/v, precompute_len, input_num_token)`
- 通过 `embed_selector.getByIndex(id)` 为 `tokens_diff` 构建 `out_embed`
- `Run(out_embed)` 执行 prefill/decode
- `GetKVCache(...)`，追加 assistant 回复，更新 `last_tokens_ids`

因此 `axllm` 上的"上下文"（KV cache）支持基于 **token-id diff** + 缓存。

以下是从各 VLM 分支（Qwen/InternVL/FastVLM/SmolVLM2，包括多帧/视频变体）中提炼出的模式。

## 1) 图像/视频如何进入 prompt

所有 VLM 分支都遵循相同的总体思路：

1. tokenizer 的 chat template 为每个媒体项生成**占位 token**。
2. 视觉编码器生成**逐占位符的 embedding 向量**。
3. 代码在 `input_ids` 中找到占位符的位置，并**替换** `out_embed` 中对应的 token embedding。

差异在于：(a) 使用哪些占位 token；(b) 如何定位它们。

### A. Qwen3/Qwen2.5-VL 风格

- prompt 使用 `<|vision_start|> ... <|vision_end|>` 包裹。
- 在视觉块内部，占位符会重复出现：
  - 图像：`<|image_pad|>` 重复 `num_media * num_media_tokens` 次
  - 视频：`<|video_pad|>` 重复 `num_media * num_media_tokens` 次
- 注入偏移通常通过扫描 `vision_start_token_id` 并取 `offset=i+1`（start 之后的第一个占位符）得到。

### B. InternVL 风格

- prompt 使用 `<img> ... </img>` 包裹。
- 占位 token 通常是 `<IMG_CONTEXT>`，重复 `num_media_tokens` 次。
- 有些模板把 `content.data` 放在占位符之前，有些放在之后。
- 注入偏移通过在 `input_ids` 中扫描 `IMAGE_CONTEXT_TOKEN` 得到（或先找到 `<|vision_start|>` 再取其后一个 token）。

### C. FastVLM 风格

- 与 InternVL 类似：使用一个简单的占位 token（如 `"<image>"`）重复出现。
- 代码在 `input_ids` 中扫描固定的 `IMAGE_CONTEXT_TOKEN` id，并覆盖这些槽位。

### D. SmolVLM2 风格

- 占位 token 是 `"<image>"`（通常是单个 token id）。
- 模板可能在图像 token 周围加入额外的"header token"。
- 注入偏移的查找方式是检测由连续 `IMAGE_CONTEXT_TOKEN` id 组成的**连续段（run）**，并压入每段的第一个 id。

## 2) 视觉编码器的输出形状与预处理

编码器的 IO 大致分为两种风格：

### A. "经典图像编码器"（单张图像 -> embedding 序列）

常见于 `ax-fastvlm`、`ax-internvl`：

- 确定输入布局：
  - NCHW float 输入：按 `(x/255 - mean) / std` 归一化，以 `float` 写入输入张量。
  - NHWC u8 输入：缩放 + RGB，memcpy 到输入张量。
- 确定输出 dtype：
  - 若输出大小等于 `elem_count * 2` => bf16 输出
  - 若输出大小等于 `elem_count * 4` => fp32 输出（随后转换为 bf16）
- 结果是一个扁平的 bf16 数组，其长度（概念上）为 `(num_media_tokens * tokens_embed_size)`。

### B. Qwen-VL "视频处理器"（帧 -> patch -> embedding 序列）

常见于 `ax-qwen2_5-vl`、`ax-qwen3-vl`、`axcl-qwen3-vl`：

- 通过"类视频"的 patch 化流水线预处理帧（即使是图像也如此）：
  - 缩放到 `(vision_config.height, vision_config.width)`
  - RGB
  - 时间维 patch 化（`temporal_patch_size`）
  - 空间合并（`spatial_merge_size`）
  - patch 大小（`patch_size`）
- 为每个 grid 段生成 `pixel_values`（视频则有多个段）。
- 每个段送入 `image_encoder`，生成一个 embedding 块。
- 记录 `cfg.image_grid_thw` 和/或 `cfg.video_grid_thw`，供 mRoPE 使用。

## 3) 部分 VLM 使用的额外辅助输入

### A. mRoPE / position ids（Qwen-VL）

Qwen-VL 分支基于以下内容计算 `position_ids`（3 x seq_len）：

- `input_ids`
- `cfg.image_grid_thw` / `cfg.video_grid_thw`
- 视觉设置：`spatial_merge_size`，有时还包括视频时间缩放（`second_per_grid_ts`）

随后在 prefill 中使用这些 `position_ids`：将其写入模型的 `indices` 输入，
以取代简单的单调递增索引。

### B. deepstack 特征（部分 AXCL Qwen-VL 变体）

部分图像编码器会输出额外的张量（例如 3 个"deepstack 特征"）。
在 prefill 期间，对于 `visual_pos_mask[j] == 1` 的 token，代码会把这些特征
加到中间 embedding 流上（bf16->fp32 相加 -> bf16）。

### C. visual_pos_mask

由 `input_ids` 计算得到：标记出值等于 `image_token_id` 或 `video_token_id` 的位置。
用于把 deepstack 特征仅对齐到视觉占位符所在的位置。

## 4) 多图 / 多帧的表示方式

所有分支都将多图编码为：

- `Content{ role=USER, type=IMAGE, data=prompt, num_media=N, num_media_tokens=T }`
- tokenizer 将占位 token 重复 `N*T` 次。
- 视觉编码器返回 `N` 个块，每块长度为 `T * tokens_embed_size`。

对于视频：

- 有些分支把每个时间维 grid 段视为一个"媒体块"。
- 有些分支计算 `cfg.video_grid_thw = {{grid_t, grid_h, grid_w}}`，然后在内部展开。

## 5) 与 `axllm` 的关键差距（上下文支持）

上述 VLM 分支通常为完整 prompt 构建 `input_ids`，然后一次性构建完整的
`out_embed`（文本 token embedding + 注入的视觉 embedding）。

`axllm` 则不同：

- 为支持 KV cache 上下文，它只为 `tokens_diff`（增量尾部）生成 embedding。
- 它目前**没有 hook** 可用于把占位 token 的 embedding 替换为视觉 embedding。

因此，`axllm` 的可插拔图像编码器必须解决：

- 当 `tokens_diff` 包含占位 id 时，生成的 embedding 需包含：
  - 文本 token 的普通 token embedding
  - 占位槽位的视觉 embedding
- 同时保持现有的"token-id diff + KV cache"逻辑不变。

实际影响：

- "媒体 -> 占位槽位"的映射必须能够从 `(history, token_ids)` 和/或
  持久化状态中复现，这样增量编码才能只为对话中新追加的部分
  注入正确的视觉 embedding。

## 6) 可插拔视觉模块的抽象维度（提案）

为了在不改变 `axllm` 主控制流的前提下统一各分支，可插拔模块需要
承担以下职责：

- `Tokenizer 侧`：
  - 定义占位 token，以及每个媒体项对应多少个 token（`num_media_tokens`）
  - 定义如何在 `input_ids` 中定位占位符偏移
  - （可选）提供 `position_ids` 的生成规则（mRoPE）
- `视觉侧`：
  - 加载/初始化图像编码器 axmodel（一个或多个）
  - 预处理图像/视频帧
  - 生成逐媒体的 embedding 块（bf16，按 `tokens_embed_size` 对齐）
  - （可选）deepstack 特征
- `注入侧`：
  - 给定 `input_ids` 和媒体块，生成一个"embedding 流"，其中占位槽位
    被视觉 embedding 覆盖
  - 对于 `axllm` 上下文模式：支持**仅对尾部**（`tokens_diff`）执行上述操作，
    同时保证占位符对齐正确。

这些笔记有意与具体实现无关，以便在重构 `axllm`、使其支持带上下文的
LLM + VLM 时作为检查清单使用。

## 7) 当前 `axllm` 实现说明（本分支）

本分支新增了一个可插拔视觉模块，由运行时配置开关控制（没有编译期开关）：

- `config.json`：`vlm_type`（或 `VLM_TYPE`）用于选择视觉模块。
- 若 `vlm_type != "None"(0)`，则必须设置 `filename_image_encoder_axmodel`。
 - 视觉预处理后端在 **CMake 配置阶段**选定：
   - 若找到 OpenCV，则优先使用。
   - 否则回退到 `third_party/SimpleCV`，并打印一条 CMake 警告（结果可能与 OpenCV 存在细微差异）。

VLM 运行时数据流（保留现有的 token-diff + KV cache 逻辑）：

- tokenizer 仍根据 `Content.num_media` 和 `Content.num_media_tokens` 生成占位 token。
- 视觉模块准备一份已填好这两个字段的 `history` 副本，然后：
  - 用 `image_encoder.axmodel` 编码图像/视频
  - 为 `input_ids` 中的占位符位置构建 `pos2vision` 映射
  - （Qwen-VL）计算 `position_ids`（mRoPE）以及 decode 起始位置的覆盖值
- LLM 循环只为 `tokens_diff` 构建 embedding，并且只替换该尾部中的视觉占位槽位。
