# LocateAnything-3B（grounding / 检测 VLM）

[English](../locate_anything.md) | **中文** | [한국어](../ko/locate_anything.md)

> 本文译自英文[原文](../locate_anything.md)，如有出入以原文为准。

`serve`/`run` 已支持 [nvidia/LocateAnything-3B](https://huggingface.co/nvidia/LocateAnything-3B)
（AXERA 版本：`AXERA-TECH/LocateAnything-3B`）。这是一个基于 Qwen2.5-3B 的视觉 grounding 模型，可零样本完成
**目标检测、短语 grounding、OCR / 场景文字检测、文档版面分析、
GUI grounding 以及点定位（pointing）**。本实现直接移植自该模型自带的
`infer_locateanything_axengine.py` 参考实现。

## 工作原理

- **视觉**：图像缩放到 560×560（Pillow bicubic），按 `pixel/127.5 - 1` 归一化，
  patchify 为 `[1600, 3, 14, 14]`，经 `image_encoder_mlp.axmodel` → `400×2048`
  个视觉 token（`VLMType::LocateAnythingVL`，复用 PaddleOCR-VL 的 `encode_block_normalized_float`
  路径，并配合固定 560 尺寸的 `LocateAnythingImageProcessor`）。
- **Prompt**（`tokenizer_type = LocateAnything`）：`<image N><img><IMG_CONTEXT>×400</img>` +
  用户指令；400 个图像 embedding 注入到 `<IMG_CONTEXT>`（id 151665）所在的位置。
- **输出**：模型以特殊 token 的形式输出几何信息，这些 token 会渲染为纯文本：
  - 框（box）：`<box><x1><y1><x2><y2></box>`
  - 点（point）：`<box><x><y></box>`
  - 可选标签：位于一组 box 之前的 `<ref>...</ref>`
  - 每个 `<N>` 都是**归一化到 0–1000** 的坐标；像素坐标 = `N / 1000 * image_dim`。

## config.json

随模型发布的 `config.json` 缺少视觉相关字段，需要手动补上：

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

## 使用方式

```shell
axllm serve /path/to/LocateAnything-3B --port 8010
```

通过 OpenAI chat API 发送一张图片 + 一条任务指令（每个请求一张图片）：

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

### 任务 prompt（来自参考实现）

| 任务 | 指令模板 | 输出 |
|--|--|--|
| 目标检测 | `Locate all the instances that matches the following description:{categories}` | 多个 box |
| 短语 grounding（单个） | `Locate a single instance that matches the following description: {phrase}.` | 单个 box |
| 短语 grounding（多个） | `Locate all the instances that match the following description: {phrase}.` | 多个 box |
| 文本 grounding | `Please locate the text referred as {phrase}.` | 多个 box |
| 场景文字检测 / OCR | `Detect all the text in box format.` | `<ref>text</ref>` + 多个 box |
| 文档版面分析 | `Detect all the objects in the image that belong to the category set: {categories}.` | 多个 box |
| GUI grounding（box） | `Locate the region that matches the following description: {phrase}.` | 单个 box |
| GUI grounding / 点定位（pointing） | `Point to: {phrase}.` | 单个点 |

## WebUI 演示

`scripts/locateanything_webui.py` 是一个仅依赖标准库（无额外依赖）的小型 Web 前端，用于连接
正在运行的 `serve` 实例——支持实时检测 / 短语 grounding，并逐个增量绘制 box。

```shell
AXLLM_SERVE_URL=http://127.0.0.1:8010 \
AXLLM_IMAGE_DIR=/path/to/sample_images \
python3 scripts/locateanything_webui.py --port 7861
```

打开 `http://<host>:7861`。参数：`--host`、`--port`、`--serve-url`、`--image-dir`、`--model`。

- 自动滚动的缩略图横幅（悬停暂停、滚轮滚动、点击加载）；**Upload** 按钮。
- **Task** = *Object detection*（编辑彩色类别标签；每个类别发起一次查询）或
  *Phrase grounding*（输入一段描述，例如 `the dog on the left`）。
- **Max targets** 滑块（16 / 64 / 256）；prefill 与图像编码期间播放扫描动画，
  随后 box 逐个流式出现；状态指示灯 + **Detect** / **Stop**。

可选的逐图预设——在 `AXLLM_IMAGE_DIR` 中与图片同级放置一个 `tags.json`：

```json
{
  "dogs.jpg":   { "tags": ["dog"],               "phrase": "the dog in the center" },
  "safari.jpg": { "tags": ["zebra", "elephant"], "phrase": "the elephant" }
}
```

`tags` 是检测模式使用的类别，`phrase` 是 grounding 模式使用的句子；
点击缩略图会同时加载两者，由任务选择器决定实际使用哪一个。也接受纯列表形式
（`"dogs.jpg": ["dog"]`，仅包含类别）。

## 注意事项

- **使用 greedy 解码**（temperature 0 / 关闭采样），以获得最可靠、格式最规范的
  几何输出——与其他 ≤4B 模型的建议相同。
- 几何信息以文本形式返回（`<box>…</box>`）；请在客户端解析，并按图像
  尺寸缩放（`N/1000*dim`）。服务端输出结构化 box 可能作为后续工作支持。
- 仅支持基于 Qwen2.5 的 LocateAnything；每个请求一张图片。
