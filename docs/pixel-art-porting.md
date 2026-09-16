# Pixel-art extraction, porting and comparison

## Purpose

本流程用于把设计稿中的 pixel art 移植为 theme sprite、bitmap font 或程序
图元，并证明输出未丢像素、未增噪点、未因 RGB565 或浏览器缩放产生伪差异。
视觉相似、native/WASM 哈希一致，皆不能替代源图对比。

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

## 4. Layout and text

字符须共享 cell、baseline 与 advance。多字符数字或层名先计算整串宽度，
再生成唯一初始 pen 并顺序前进；禁止逐字居中。三位数字与频谱相碰时，应
移动频谱而非移动任一数字。

阴影、描边、主体皆是独立语义层；theme 不需要的层应在资产生成阶段删除，
而非在渲染末端遮盖。大号展示字与 Status 小号字应有独立 atlas 或明确整数
缩放契约。

## 5. RGB565 transport

参考图与 framebuffer 比较前须进入同一 RGB565 域。预量化 sprite 使用
`dtr_pixel565()`／opaque blit，避免再次抖动；矢量边缘才使用 coverage 与
Bayer 量化。

注意两种量化语义：sprite pack 可采用四舍五入，而 `dtr_rgb()` 是位截断。
测试目标须匹配实际路径。含透明色键的 sprite 不得用 opaque blit；完全不透明
sprite 应用 opaque blit，避免真实 RGB565 像素恰等于色键时被误删。

## 6. Framebuffer comparison

使用 `scripts/compare_reference.py` 对 raw WASM/native framebuffer 比较，
不比较浏览器截图。关键 region 至少检查 `changed_pixels`、平均绝对误差、
生成图独有的白点、3×3 邻域亮度突变及 hidden region 是否全黑。

静态构件与动态字形可分开门禁：静态框体对原 UI 裁片，字形对独立 atlas
specimen。不得用整块清黑矩形擦除与边框合法重叠的字形，再误报底部像素。

## 7. Animation acceptance

先验证 `t=0` 静态帧，再启用反射、角色帧和按键反馈。动画 sprite 的共同
anchor 由稳定特征点校准；清除旧角色时覆盖完整旧 bbox，并按 z-order 重新
覆回不可清除的前景。

演示须直接读取 `theme.wasm` framebuffer。MP4 保存准确帧率、帧数与时长；
GIF 仅作便览，不作为颜色或帧率证据。

## 8. Required gates

提交前至少通过：source mask `missed=0/extra=0`、target XOR 全黑、theme region
严格差分、native/WASM parity、静态抖动确定性与 `git diff --check`。失败应
修正源选择、坐标、量化或 z-order；不得放宽阈值掩盖。
