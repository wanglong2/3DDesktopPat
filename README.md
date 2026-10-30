# Crose 3D 桌面宠物

使用 C++17 与 [raylib 6.2](https://github.com/raysan5/raylib/releases/tag/6.2) 实现的可配置 3D 桌宠。源码可在 Windows、macOS 和 Linux 构建，无 Qt 依赖。未设置模型时会显示内置演示角色。

## 构建与启动

需要 CMake 3.22+ 和 C++17 编译器。第三方库源码已放在 `thirdparty/`，配置和构建不需要下载依赖。Linux 还需要 raylib 所需的 X11/OpenGL 开发包，并建议安装 `zenity` 以使用原生文件选择框。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

构建时会将 `config/pet.json` 和旧版 `config/pet.ini` 复制到构建目录。直接启动会先打开配置器：

```sh
./build/crose_pet
```

配置器可以选择 `.glb`/`.gltf` 模型并即时预览，分别设置启动、静息、拖动、自动移动、左右键和退出动画。每个动作还可单独设置播放速度、是否循环，以及要显示的材质组件。窗口尺寸、镜头、动画混合与自动移动参数位于第二个页面。使用 **Open JSON** 导入配置，使用 **Save/Save As** 保存可移植的 JSON；模型路径会相对于 JSON 文件保存。

跳过配置器直接运行已有配置：

```sh
./build/crose_pet --config ./build/pet.json --run
```

Windows 下可双击 `build/crose_pet.exe`。MinGW 构建的程序不会额外打开控制台窗口。旧版 INI 仍可导入，建议在配置器中另存为 JSON。

Windows 版启动后会在通知区域显示团子的动态图标，桌宠窗口不会出现在任务栏或 Alt+Tab 中。托盘图标取自当前动画姿态；左键点击托盘图标可显示或隐藏桌宠，右键菜单可隐藏/显示或退出。图标若未直接出现在任务栏通知区域，请在 `^` 的隐藏图标面板中查找。其他平台目前保留普通窗口运行方式。

Windows 版会按团子的实际轮廓处理鼠标：透明区域允许点击、滚轮等事件传给后方窗口，团子本体仍可响应原有交互。托盘菜单始终可用于退出。

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
