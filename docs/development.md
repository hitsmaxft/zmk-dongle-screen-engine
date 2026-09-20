# Theme development

Pixel-art 素材之抠图、逐区移植、独立双路像素校验及动画验收，须遵循
[pixel-art-porting.md](pixel-art-porting.md)。

像素主题的开发顺序是强制的：背景 → 静态前景 → 独立 atlas → 静态组合 →
离散帧动画 → 局部重绘。不得在效果门禁之前实现 retained rendering、
dirty rectangle、压缩或运行时素材生成。动画质量与逐帧像素稳定性优先于
刷新率、Flash 与 RAM 优化。

## Shared renderer path

Theme 的 firmware、native 与 WASM 应编译同一组 C 源。先以
`scripts/build_preview.py` 生成自包含预览，再以 `scripts/test_preview.py`
验证多分辨率 native/WASM 帧哈希、确定性与触摸语义。浏览器 CSS 缩放后的
截图不得作为 framebuffer 精度依据。

构建器会在写出最终 HTML 前，先以 native 与 WASM 双路检验 ABI 1.3：版本、
required-prefix 尾界、dirty rect 边界、frame/draw 时间戳配对及 buffer 容量。
此快速门禁失败即终止；不得以能加载页面代替接口可靠性证明。完整 replay 则仍
由 `test_preview.py` 执行。

```sh
python3 scripts/build_preview.py \
  --lvgl /path/to/lvgl --theme /path/to/theme \
  --variant default --output .build/default-v001

python3 scripts/test_preview.py .build/default-v001

node scripts/render_wasm.cjs .build/default-v001 \
  --time 1000 --output .build/default-v001/frame-1000.png
```

日常视觉迭代以 `render_wasm.cjs` 直接读取 WASM RGB565 framebuffer 并导出
PNG；应先检 PNG、frame hash 与自动像素门禁。仅当修改交互控件、多语言或响应式
布局时，方启浏览器作最终验收，勿以浏览器截图替代 framebuffer 证据。

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

旧 raster Theme 迁移 ABI 1.3 时，可先用 `DTE_THEME_RASTER_ADAPTER` 保持视觉
代码不变；WASM 闸门通过后，再按性能需要把 `frame` 与 `draw` 拆成原生区域实现。
固件不得自行保存完整 RGB565 framebuffer；预览全帧仅供取图、hash 与比对。

## GitHub Actions preview

公开主题仓可调用 `.github/actions/build-preview`，以主题目录、LVGL checkout
及输出目录为输入。Action 仅于 native/WASM parity 通过后公布自含式
`index.html` 路径；GitHub Pages workflow 只须汇集各主题输出并上传静态目录。
发布流程应锁定 Engine tag 与 LVGL commit，勿从下游复制编译命令。

Action 同时生成 `budget-report.json` 与 `budget-summary.md`。默认模型为
280×240 RGB565、24 FPS、32 MHz SPI、20% dirty area 与 4,480 像素 strip；
主题应按实机覆盖这些输入。若能提供 Zephyr final map，报告会读取
`_flash_used` 与 `_image_ram_size`；无 map 时固件 RAM/Flash 明示为 `n/a`，
不得以 WASM/native 文件大小代替。

```sh
python3 scripts/report_budget.py \
  --preview-manifest .build/default/preview-manifest.json \
  --firmware-map /path/to/zephyr/zmk.map \
  --baseline .build/baseline/budget-report.json \
  --output .build/default/budget-report.json \
  --markdown .build/default/budget-summary.md \
  --width 280 --height 240 --fps 24 --spi-mhz 32 \
  --dirty-percent 20 --max-spi-utilization-percent 85
```

比较报告须使用相同 board、shield、Kconfig 与工具链。SPI 数字是 RGB565
payload 估算，含 bytes/frame、bytes/s、传输时间及帧预算占比；窗口命令、DMA
间隙、任务竞争与实体屏时序仍须另作硬件测量。
