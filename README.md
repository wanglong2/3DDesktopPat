# Crose 3D 桌面宠物

使用 C++17 与 [raylib 6.2](https://github.com/raysan5/raylib/releases/tag/6.2) 实现的可配置 3D 桌宠。源码可在 Windows、macOS 和 Linux 构建，无 Qt 依赖。未设置模型时会显示内置演示角色。

## 构建与启动

需要 CMake 3.20+、C++17 编译器，以及首次配置时用于下载 raylib 的网络连接。Linux 还需要 raylib 所需的 X11/OpenGL 开发包。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

构建时会将 `config/pet.ini` 复制到 `build/pet.ini`。从构建目录启动，或显式指定配置：

```sh
./build/crose_pet --config ./build/pet.ini
```

Windows 下可双击 `build/crose_pet.exe`。MinGW 构建的程序不会额外打开控制台窗口。修改 `config/pet.ini` 后重新配置 CMake，或直接修改 `build/pet.ini`。

## 导入小小英雄模型

1. 准备包含网格、骨骼、贴图和动画的 `.glb`。也支持 `.gltf`，但需保留它引用的外部文件。
2. 将文件放在 `pet.ini` 旁边，并设置 `model = 文件名.glb`；也可以填绝对路径。
3. 执行 `crose_pet --config pet.ini --list-animations` 查看模型里的动画名称。
4. 将 `idle`、`move`、`left_click`、`right_click`、`exit` 映射到实际动画名称，或使用从零开始的 `#索引`。名称不区分大小写，`|` 表示依次尝试多个名称。

程序会根据模型边界盒自动居中，并用 `model_height` 控制显示高度。`animation_fps` 默认 60，与 raylib 对 glTF 的采样速率一致。`blend_seconds` 控制骨骼姿态之间的交叉淡入时长。缺少动作动画时会做一个小幅程序动作；缺少静息动画时会使用第一个可用动画。

对于 Khada 导出的 GLB，程序会读取材质 `extras.visible`，不绘制默认隐藏的备用表情和配件。`团子.glb` 中 16 个材质部件有 13 个标为隐藏；旧版本曾把它们一起画出来，造成脸部叠加并出现鱼和碗。

## 交互

| 操作 | 效果 |
| --- | --- |
| 无操作 | 静息动画，可配置间歇自动移动 |
| 鼠标左键单击 | `left_click` 动画 |
| 鼠标左键拖动 | 移动桌宠窗口 |
| 鼠标右键单击 | `right_click` 动画 |
| 鼠标右键拖动 | 水平旋转模型，垂直调整观察角度 |
| 鼠标滚轮 | 在桌宠窗口内平滑放大或缩小模型 |
| Esc 或 Q | 播放 `exit` 动画并关闭 |

`auto_walk`、`walk_interval`、`walk_duration`、`walk_speed` 控制自动移动。透明无边框窗口默认置顶，可通过 `always_on_top = false` 关闭置顶。

工程不附带 Riot Games 的模型资源。请导入自己有权使用的模型。部分《英雄联盟》/《云顶之弈》资源使用 glTF 标准之外的材质、序列帧和特效；这类模型需要额外转换或专用着色器才能达到参考网站完全相同的视觉效果。动画衔接使用骨骼姿态混合，最终效果还取决于导出的模型与动画片段。
