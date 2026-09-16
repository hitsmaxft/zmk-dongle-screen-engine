# Theme development

Pixel-art 素材之抠图、逐区移植、独立双路像素校验及动画验收，须遵循
[pixel-art-porting.md](pixel-art-porting.md)。

## Shared renderer path

Theme 的 firmware、native 与 WASM 应编译同一组 C 源。先以
`scripts/build_preview.py` 生成自包含预览，再以 `scripts/test_preview.py`
验证多分辨率 native/WASM 帧哈希、确定性与触摸语义。浏览器 CSS 缩放后的
截图不得作为 framebuffer 精度依据。

```sh
python3 scripts/build_preview.py \
  --lvgl /path/to/lvgl --theme /path/to/theme \
  --variant default --output .build/default-v001

python3 scripts/test_preview.py .build/default-v001
```

## RGB565 visual comparison

`scripts/compare_reference.py` 接受设计素材与由 WASM/native 内存直接导出的
PNG/PPM。工具按渲染尺寸以 nearest-neighbour 映射参考图，再将参考像素量化
至 RGB565，避免把浏览器插值或 RGB888/RGB565 色阶差误判为 renderer 缺陷。

```sh
python3 scripts/compare_reference.py \
  --reference /path/to/theme/arts/reference.jpg \
  --rendered .build/default-v001/idle.png \
  --output .build/default-v001/visual-diff.png \
  --report .build/default-v001/visual-diff.json \
  --region function_area:0,151,280,240 \
  --strict function_area
```

`--region NAME:X0,Y0,X1,Y1` 可重复；`full_frame` 始终自动生成。报告含差异
像素数、比例、平均绝对误差、生成图独有的高亮白点及局部亮度突变。热图中
红蓝表示通道差，黄色表示新增白点，青色表示新增亮度突变，黑色表示相等。

默认白点阈值为三通道皆不低于 170；亮度突变要求中心亮度不低于 205，且
高于 3×3 邻域中位数至少 96。可用 `--white-floor`、
`--brightness-floor`、`--spike-delta` 调整。严格区域任一指标非零即失败。

Theme 测试亦可导入 `compare_reference.py` 的 `compare()`，传入自身
`regions`。Engine 不内置 theme 坐标、参考图或容差；三者由 theme 仓库维护。

## Pixel assets

预量化 sprite 应以 `dtr_pixel565()` 或等价直接 RGB565 路径写入，避免再次
经过 Bayer 量化。含透明色键的 sprite 与完全不透明 sprite 应分开处理；若
真实 RGB565 像素恰等于透明键，不透明 blit 不得丢弃该像素。

动画比较须固定 snapshot 与 timestamp。先验证静态关键帧，再比较动作序列；
逻辑 60Hz 只约束 deadline，实机 SPI 帧率仍须以固件日志及屏幕结果验证。
