# Pixel-art extraction, porting and comparison

## Purpose

本流程用于把设计稿中的 pixel art 移植为 theme sprite、bitmap font 或程序
图元，并证明输出未丢像素、未增噪点、未因 RGB565 或浏览器缩放产生伪差异。
视觉相似、native/WASM 哈希一致，皆不能替代源图对比。

## Non-negotiable priority

**像素类主题以动画质量、逐帧像素稳定性及素材一致性为最高优先。** FPS、
Flash、RAM、dirty rectangle 与代码技巧皆属后置约束，不能用来解释模糊、
错位、背景跳变、孤立像素或劣质动画。效果门禁未完成前，禁止以“性能优化”
改变素材、合并图层或缩小验证范围。

强制实施顺序如下，严禁并行乱改：

1. 冻结逻辑画布、坐标及背景；背景先独立通过。
2. 自后向前逐一实现静态图层；一次只显示一层，其余全黑或透明。
3. 为每个前景元素建立独立 atlas、固定 bbox、anchor、palette 与 mask。
4. 完成静态 `t=0` 组合及逐区像素门禁。
5. 仅以 atlas frame index 实现动画；renderer 不再生成、缩放或抠取图片。
6. 完成逐帧动画门禁与素材对照。
7. 最后才实施 retained rendering、dirty rectangle、缓存与传输优化。

任一阶段失败，须返回该阶段修正，不得在后续 renderer 中以补丁遮掩。

## 1. Freeze the source

原始素材须原样置于 theme `arts/`，记录 SHA-256。先确定最终显示样例、字符
总览、分层示意各自用途；最终纯色小号样例优先于带描边的大号展示字。不得
从一种视觉规格剥色后冒充另一种规格。

坐标、裁片、cell、baseline、advance、缩放方式均写入代码或布局文档。源图
若含 Header/Scene 重叠，须先判定像素归属；隔离预览不得夹带别区内容。

## 2. Isolate one region

每次只启用一个 region variant，其余 framebuffer 恒黑。建议顺序为 Header、
Scene、Status、Footer。静态区域应返回非动画态；动态区域先固定 snapshot 与
`t=0`，取得可比较基准帧。

区域内部仍须自底向上逐层验收：background、static chrome、foreground
sprites、dynamic text、interaction overlays。每加入一层，保存新的 raw
framebuffer 与 diff；不得一次合入多个未验素材后再靠肉眼定位。

## 3. Extract source pixels

先使用紧边界裁片。纯色字形应取得完整颜色覆盖，包括 JPEG 压缩形成的较暗
边缘像素；不可只取高亮核心。连通域过滤只有在源图确认仅有一个连通体时方可
使用，且过滤前后像素数必须记录。

提取必须通过独立双路校验：测试代码从原图独立构造 reference mask；生成器
输出 actual mask；源尺寸统计 `missed = reference - actual` 与
`extra = actual - reference`，两者皆须为零。不得以同一函数同时生成 expected
与 actual。

若需缩放，两路各自按明示规则生成目标 cell，再作 XOR。Pixel art 默认
nearest-neighbour；不得混入 BOX、双线性或浏览器 CSS 插值。证据图应保存
source／generated／XOR 三行，XOR 全黑方可通过。

### Forbidden extraction and scaling

- 有最终纯色／分层素材时，禁止从合成效果图反向抠图。
- 禁止以通用亮度阈值、自动白底移除或“最大连通域”掩盖遗漏；若使用连通域，
  必须先证明源图确仅含该连通体，并记录过滤前后像素数。
- 禁止 `thumbnail()`、BOX、bilinear、bicubic 等隐式缩放 pixel art。
- 非整数缩放须先形成书面采样契约，并由独立 reference path 验证每一目标像素。
- 禁止逐帧单独裁边、缩放、居中；动画所有帧共享 canvas、bbox 与 anchor。
- 禁止运行时缩放、运行时抠图、运行时颜色聚类或从 framebuffer 反采素材。

JPEG 只能作为不得已的参考容器。若源为 JPEG，边缘暗像素也属于待验证覆盖；
不能只取最亮核心，也不能把压缩噪声直接带入最终 palette。

## 4. Layout and text

字符须共享 cell、baseline 与 advance。多字符数字或层名先计算整串宽度，
再生成唯一初始 pen 并顺序前进；禁止逐字居中。三位数字与频谱相碰时，应
移动频谱而非移动任一数字。

阴影、描边、主体皆是独立语义层；theme 不需要的层应在资产生成阶段删除，
而非在渲染末端遮盖。大号展示字与 Status 小号字应有独立 atlas 或明确整数
缩放契约。

## 5. Atlas-first contract

动画代码编写前，必须先产生可独立审阅的机器 atlas 与带标注预览。每类元素
至少声明：frame size、frame count、anchor、palette、透明规则、合法像素 mask
及 source checksum。背景只能有一个 immutable base；角色、图标、电池 fill、
频谱等皆为透明前景或受 mask 约束的覆盖层。

Atlas 必须先证明：

- 动画所有帧在 union alpha mask 外的背景差异为零；
- 电池／进度 fill 在合法 mask 外的着色像素为零；
- 固定外壳不含动态颜色残留，端子、圆角及边缘像素完整；
- 网格类动画的 off-grid pixel 为零；
- 每帧不含意外白点、孤立色点或未声明 palette 颜色。

Atlas 未通过这些门禁，禁止接入 theme renderer。

## 6. RGB565 transport

参考图与 framebuffer 比较前须进入同一 RGB565 域。预量化 sprite 使用
`dtr_pixel565()`／opaque blit，避免再次抖动；矢量边缘才使用 coverage 与
Bayer 量化。

注意两种量化语义：sprite pack 可采用四舍五入，而 `dtr_rgb()` 是位截断。
测试目标须匹配实际路径。含透明色键的 sprite 不得用 opaque blit；完全不透明
sprite 应用 opaque blit，避免真实 RGB565 像素恰等于色键时被误删。

## 7. Framebuffer comparison

使用 `scripts/compare_reference.py` 对 raw WASM/native framebuffer 比较，
不比较浏览器截图。关键 region 至少检查 `changed_pixels`、平均绝对误差、
生成图独有的白点、3×3 邻域亮度突变及 hidden region 是否全黑。

静态构件与动态字形可分开门禁：静态框体对原 UI 裁片，字形对独立 atlas
specimen。不得用整块清黑矩形擦除与边框合法重叠的字形，再误报底部像素。

## 8. Animation acceptance

静态 `t=0` 通过后，动画状态机只能选择已验证的 frame index，并更新显式位置
或可枚举 palette；不得在动画循环中重新生成图像。每帧必须由代码与素材做
严格分析，而非只看 GIF：

- 保存 raw RGB565 frame、frame index、timestamp 与 snapshot；
- 对连续帧计算 changed-pixel bbox；变化不得越出声明的 animated region；
- 在角色 union mask 外，背景 diff 必须为零；
- 固定 anchor／特征点漂移必须为零，或符合明示位移动画轨迹；
- 禁止以 opacity 闪烁替代像素帧，禁止 alpha 混合生成模糊中间色；
- MP4 必须记录准确 fps、帧数与时长；GIF 仅供便览。

至少覆盖 idle、状态切换、最高输入速度、最长数字、全部交互按钮及循环首尾。
循环首帧与末帧的静态像素必须可解释，不得发生背景跳变。

## 9. Modular implementation

主题代码应按职责拆分，避免一个 renderer 同时处理资产生成、状态机与传输：

- `scripts/`：仅离线生成 atlas、manifest 与验证证据；
- `assets/` 或生成头文件：只保存不可变 sprite、mask、palette；
- `scene`：绘制 immutable background；
- `widgets`：电池、数字、频谱、按钮等独立模块；
- `animation`：把时间与状态映射到 frame index，不绘制素材；
- `theme render`：按稳定 z-order 组合模块；
- `tests`：使用独立 reference path，不能导入生成器作为 expected。

跨模块共享的只有明示状态与坐标契约，禁止通过 framebuffer 取色、扫描旧图或
以隐藏全局变量耦合图层。

## 10. Optimization comes last

第一版合格 renderer 应采用简单、确定性的完整组合，以建立视觉基准。只有
静态与动画全部通过后，才可引入 retained framebuffer、dirty rectangles、
tile hash、缓存、RLE／palette 压缩及局部传输。

每项优化必须与优化前保存的逐帧基准再比较；除事先声明的 timing metadata 外，
像素输出须完全相同。不得为了省 Flash 保存带噪声整块截图，也不得为了省重绘
让背景 patch、旧 sprite 残影或越界 fill 留在 framebuffer。

## 11. Recent failure patterns

| 失败 | 根因 | 禁止的补救 |
|---|---|---|
| 换猫帧时背景变化 | idle 使用合成场景，其他帧另铺异源 clean patch | 继续调 patch 颜色 |
| 电池 100% 有绿块、端子缺失 | frame 裁片过窄且静态 frame 残留动态绿色 | 在 renderer 多画黑块 |
| 频谱模糊、闪现 | JPEG 抠图加 alpha fade | 增加帧率或抖动 |
| 数字丢边、底部残片 | 从错误的大号合成字剥色 | 放宽视觉阈值 |
| 测试始终通过 | expected 与 actual 共用同一提取函数 | 宣称 native/WASM parity 即正确 |
| Flash 突然溢出 | 为匹配暗背景噪声保存整块 RGB565 screenshot | 降低动画质量 |

## 12. Required gates

提交前至少通过：source mask `missed=0/extra=0`、target XOR 全黑、theme region
严格差分、native/WASM parity、静态抖动确定性与 `git diff --check`。失败应
修正源选择、坐标、量化或 z-order；不得放宽阈值掩盖。
