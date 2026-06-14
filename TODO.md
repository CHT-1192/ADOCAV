# TODO

## GPU Culling
- [ ] GPU 反馈：GPU tile culling 已写完（shader + pipeline + descriptor），但需要重做 indirect draw 或 stream compaction 才能跳过中间不可见实例。当前默认关闭。

## 多线程
- [x] CPU 剔除多线程化（shape group 分块并行）
- [ ] precalculateTiming 多线程化
- [ ] 线程化 command buffer 录制

## 渲染
- [ ] Per-tile 交叉绘制（tile → icon，由远到近）
- [ ] Multi-draw indirect（一个 draw call 画所有 tile group）
- [ ] Launcher 改成纯 Vulkan ImGui 后端（去掉 OpenGL/glad）
- [ ] Trail 渲染：`computePlanetTrails()` 未在渲染循环中调用

## 优化
- [ ] Branchless angle 计算（`PlaybackEngine::precalculateTiming()`）
- [ ] Hot/cold 数据分离（`TileInstance` 拆热冷）
- [ ] SIMD CPU culling（`--cpu-culling` 回退路径）
- [ ] Trail LOD（缩放时降低 Catmull-Rom 段数）

## 功能
- [ ] MoveCamera（5 种 relativeTo 模式）
- [ ] MoveTrack（tile 位置/旋转/缩放动画）
- [ ] ColorTrack COLOR_FUNCS（Single, Glow, Blink, Switch, Rainbow, Stripes）
- [ ] ColorTrack FLOOR_FUNCS（Standard, Neon, NeonLight, Basic, Gems, Minimal）
- [ ] ColorTrack RecolorTrack 运行时触发
- [ ] PositionTrack: relativeTo, rotation, scale, opacity, stickToFloors
- [ ] Bloom / Flash 特效
- [ ] Decoration 系统

## 跨平台
- [ ] Linux 实际测试（CI 有编译但未实测运行）
