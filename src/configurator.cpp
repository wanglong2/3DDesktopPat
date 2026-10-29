#include "configurator.h"

#include "model_render.h"

#include "raylib.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wenum-compare"
#endif
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "portable-file-dialogs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace crose
{
    namespace
    {
        constexpr int editorWidth = 1180;
        constexpr int editorHeight = 780;

        void copyText(char *destination, std::size_t capacity, const std::string &source)
        {
            if (!capacity)
                return;
            std::strncpy(destination, source.c_str(), capacity - 1);
            destination[capacity - 1] = '\0';
        }

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        // 界面使用中文字体。收集全部静态文案和动态文本（模型路径、组件名、动画名等）
        // 涉及的码点，按需重建字体子集，避免加载完整 CJK 字体集。
        Font uiFont{};
        std::string uiGlyphSource;

        const char *const uiStrings[] = {
            u8"桌宠配置工作室", u8"构建可复用的 3D 桌宠配置",
            u8"打开 JSON", u8"保存", u8"另存为", u8"启动桌宠  >",
            u8"模型与动作", u8"显示与行为",
            u8"配置名称", u8"3D 模型", u8"浏览...", u8"动作",
            u8"启动", u8"待机", u8"拖动", u8"移动", u8"左键点击", u8"右键点击", u8"退出",
            u8"动画片段", u8"（无）", u8"循环播放", u8"播放速度",
            u8"可见组件", u8"使用模型自带的可见性", u8"全选", u8"全不选", u8"默认",
            u8"宠物窗口", u8"宽度", u8"高度", u8"帧率", u8"窗口置顶",
            u8"模型与相机", u8"模型高度", u8"相机距离", u8"相机俯仰角", u8"相机目标高度",
            u8"自动移动", u8"启用自动移动", u8"移动速度（像素/秒）", u8"间隔（秒）", u8"持续（秒）",
            u8"动画", u8"源采样率", u8"混合时长",
            u8"实时预览", u8"请选择模型开始预览",
            u8"请选择模型或打开已保存的 JSON 配置",
            u8"已加载 ", u8" 个动画、", u8" 个组件", u8"已保存 ",
            u8"请先选择存在的模型文件", u8"请选择 .glb 或 .gltf 模型",
            u8"模型文件不存在", u8"无法加载所选模型",
            u8"打开宠物配置", u8"保存宠物配置", u8"选择 3D 宠物模型",
            u8"宠物配置", u8"所有文件", u8"glTF 模型",
        };

        // Ubuntu 的 Noto CJK 是 CFF 轮廓，stb_truetype 不支持；而 Droid Sans Fallback
        // 是纯 CJK 字体，不含 ASCII。因此分别加载拉丁字体和中文字体，再合并为一张图集。
        Font tryLoadFont(const char *path, const int *codepoints, int count)
        {
            if (!fs::exists(path))
                return {};
            Font font = LoadFontEx(path, 32, const_cast<int *>(codepoints), count);
            // 加载失败时 raylib 会静默回退成默认字体，视为不可用（且不能卸载默认字体）
            if (!IsFontValid(font) || font.texture.id == GetFontDefault().texture.id)
                return {};
            return font;
        }

        // 合并两个字体的字形与图集；源字体的字形数据所有权移交，其图集纹理被释放。
        Font mergeFontGlyphs(Font &latin, Font &cjk)
        {
            const int total = latin.glyphCount + cjk.glyphCount;
            const int atlasWidth = latin.texture.width + cjk.texture.width;
            const int atlasHeight = std::max(latin.texture.height, cjk.texture.height);
            Image latinImage = LoadImageFromTexture(latin.texture);
            Image cjkImage = LoadImageFromTexture(cjk.texture);
            Image atlas = GenImageColor(atlasWidth, atlasHeight, BLANK);
            ImageDrawImageRec(&atlas, latinImage,
                              {0, 0, static_cast<float>(latinImage.width), static_cast<float>(latinImage.height)},
                              {0, 0}, WHITE);
            ImageDrawImageRec(&atlas, cjkImage,
                              {0, 0, static_cast<float>(cjkImage.width), static_cast<float>(cjkImage.height)},
                              {static_cast<float>(latinImage.width), 0}, WHITE);
            UnloadImage(latinImage);
            UnloadImage(cjkImage);

            Font merged{};
            merged.baseSize = latin.baseSize;
            merged.glyphCount = total;
            merged.glyphPadding = latin.glyphPadding;
            merged.glyphs = static_cast<GlyphInfo *>(RL_CALLOC(total, sizeof(GlyphInfo)));
            merged.recs = static_cast<Rectangle *>(RL_CALLOC(total, sizeof(Rectangle)));
            std::memcpy(merged.glyphs, latin.glyphs, latin.glyphCount * sizeof(GlyphInfo));
            std::memcpy(merged.recs, latin.recs, latin.glyphCount * sizeof(Rectangle));
            for (int i = 0; i < cjk.glyphCount; ++i)
            {
                merged.glyphs[latin.glyphCount + i] = cjk.glyphs[i];
                Rectangle region = cjk.recs[i];
                region.x += static_cast<float>(latin.texture.width);
                merged.recs[latin.glyphCount + i] = region;
            }
            merged.texture = LoadTextureFromImage(atlas);
            UnloadImage(atlas);

            UnloadTexture(latin.texture);
            UnloadTexture(cjk.texture);
            RL_FREE(latin.glyphs);
            RL_FREE(latin.recs);
            RL_FREE(cjk.glyphs);
            RL_FREE(cjk.recs);
            latin = {};
            cjk = {};
            return merged;
        }

        Font loadUiFont(const std::vector<int> &codepoints)
        {
            static const std::array<const char *, 5> latinCandidates = {
                "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",            // Ubuntu 默认 UI 字体
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
            };
            static const std::array<const char *, 6> cjkCandidates = {
                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                "/usr/share/fonts/truetype/arphic/uming.ttc",
                "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",   // CFF，stb 不支持，仅兜底尝试
                "C:/Windows/Fonts/msyh.ttc",                                // 微软雅黑
                "C:/Windows/Fonts/simhei.ttf",
            };

            std::vector<int> latinCodepoints, cjkCodepoints;
            for (int cp : codepoints)
                (cp < 127 ? latinCodepoints : cjkCodepoints).push_back(cp);

            Font latin{};
            for (const char *path : latinCandidates)
            {
                latin = tryLoadFont(path, latinCodepoints.data(), static_cast<int>(latinCodepoints.size()));
                if (IsFontValid(latin))
                    break;
            }
            if (cjkCodepoints.empty())
                return latin;

            Font cjk{};
            int bestCoverage = 0;
            for (const char *path : cjkCandidates)
            {
                Font font = tryLoadFont(path, cjkCodepoints.data(), static_cast<int>(cjkCodepoints.size()));
                if (!IsFontValid(font))
                    continue;
                if (font.glyphCount == static_cast<int>(cjkCodepoints.size()))
                {
                    if (IsFontValid(cjk))
                        UnloadFont(cjk);
                    cjk = font;
                    break;
                }
                if (font.glyphCount > bestCoverage)
                {
                    bestCoverage = font.glyphCount;
                    if (IsFontValid(cjk))
                        UnloadFont(cjk);
                    cjk = font;
                }
                else
                    UnloadFont(font);
            }

            if (IsFontValid(latin) && IsFontValid(cjk))
                return mergeFontGlyphs(latin, cjk);
            if (IsFontValid(latin))
                return latin;
            return cjk;
        }

        void refreshUiFont(const std::string &dynamicTexts)
        {
            std::string source;
            for (const char *text : uiStrings)
                source += text;
            source += dynamicTexts;
            if (source == uiGlyphSource)
                return;
            std::set<int> glyphs;
            for (int cp = 32; cp < 127; ++cp)
                glyphs.insert(cp);
            for (std::size_t i = 0; i < source.size();)
            {
                int processed = 0;
                const int cp = GetCodepoint(source.c_str() + i, &processed);
                if (cp > 0 && processed > 0)
                {
                    glyphs.insert(cp);
                    i += static_cast<std::size_t>(processed);
                }
                else
                    ++i;
            }
            std::vector<int> codepoints(glyphs.begin(), glyphs.end());
            Font font = loadUiFont(codepoints);
            if (!IsFontValid(font))
                return;
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            if (IsFontValid(uiFont))
                UnloadFont(uiFont);
            uiFont = font;
            GuiSetFont(uiFont);
            uiGlyphSource = source;
        }

        void unloadUiFont()
        {
            if (IsFontValid(uiFont))
            {
                UnloadFont(uiFont);
                uiFont = {};
            }
            GuiSetFont(GetFontDefault());
        }

        void uiText(const char *text, int x, int y, int size, Color color)
        {
            if (IsFontValid(uiFont))
                DrawTextEx(uiFont, text, {static_cast<float>(x), static_cast<float>(y)},
                           static_cast<float>(size), 1.0f, color);
            else
                DrawText(text, x, y, size, color);
        }

        int uiMeasure(const char *text, int size)
        {
            if (IsFontValid(uiFont))
                return static_cast<int>(MeasureTextEx(uiFont, text, static_cast<float>(size), 1.0f).x);
            return MeasureText(text, size);
        }

        const char *actionLabelZh(Action action)
        {
            switch (action)
            {
            case Action::Startup: return u8"启动";
            case Action::Idle: return u8"待机";
            case Action::Drag: return u8"拖动";
            case Action::Move: return u8"移动";
            case Action::LeftClick: return u8"左键点击";
            case Action::RightClick: return u8"右键点击";
            case Action::Exit: return u8"退出";
            default: return "?";
            }
        }

        int resolveAnimation(const std::string &alternatives, const ModelAnimation *animations, int count)
        {
            std::istringstream stream(alternatives);
            std::string candidate;
            while (std::getline(stream, candidate, '|'))
            {
                const auto first = candidate.find_first_not_of(" \t");
                const auto last = candidate.find_last_not_of(" \t");
                candidate = first == std::string::npos ? std::string{} : candidate.substr(first, last - first + 1);
                if (candidate.empty())
                    continue;
                if (candidate.front() == '#')
                {
                    try
                    {
                        const int index = std::stoi(candidate.substr(1));
                        if (index >= 0 && index < count)
                            return index;
                    }
                    catch (const std::exception &)
                    {
                    }
                }
                for (int i = 0; i < count; ++i)
                    if (lower(animations[i].name) == lower(candidate))
                        return i;
            }
            return -1;
        }

        struct Preview
        {
            Model model{};
            ModelAnimation *animations = nullptr;
            int animationCount = 0;
            std::vector<ModelComponent> components;
            float frame = 0;
            int currentAnimation = -1;
            std::string loadedPath;
            RenderTexture2D target{};

            ~Preview() { unload(); }

            void unload()
            {
                if (animations)
                    UnloadModelAnimations(animations, animationCount);
                if (IsModelValid(model))
                    UnloadModel(model);
                if (target.id)
                    UnloadRenderTexture(target);
                model = {};
                target = {};
                animations = nullptr;
                animationCount = 0;
                components.clear();
                loadedPath.clear();
                frame = 0;
                currentAnimation = -1;
            }

            void ensureTarget(int width, int height)
            {
                width = std::max(1, width);
                height = std::max(1, height);
                if (target.id && (target.texture.width != width || target.texture.height != height))
                {
                    UnloadRenderTexture(target);
                    target = {};
                }
                if (!target.id)
                    target = LoadRenderTexture(width, height);
            }

            bool load(const fs::path &path, std::string &error)
            {
                unload();
                if (path.empty())
                {
                    error = u8"请选择 .glb 或 .gltf 模型";
                    return false;
                }
                if (!fs::exists(path))
                {
                    error = u8"模型文件不存在";
                    return false;
                }
                loadedPath = path.u8string();
                model = LoadModel(loadedPath.c_str());
                if (!IsModelValid(model))
                {
                    error = u8"无法加载所选模型";
                    unload();
                    return false;
                }
                animations = LoadModelAnimations(loadedPath.c_str(), &animationCount);
                components = inspectModelComponents(loadedPath, model.materialCount);

                const BoundingBox bounds = GetModelBoundingBox(model);
                const float centerX = (bounds.min.x + bounds.max.x) * 0.5f;
                const float centerZ = (bounds.min.z + bounds.max.z) * 0.5f;
                model.transform = MatrixMultiply(model.transform, MatrixTranslate(-centerX, -bounds.min.y, -centerZ));
                error = u8"已加载 " + std::to_string(animationCount) + u8" 个动画、" +
                        std::to_string(components.size()) + u8" 个组件";
                return true;
            }

            void update(const Config &config, Action action, float dt)
            {
                if (!IsModelValid(model) || !animations)
                    return;
                const ActionConfig &settings = config.actions[slot(action)];
                const int selected = resolveAnimation(settings.animation, animations, animationCount);
                if (selected != currentAnimation)
                {
                    currentAnimation = selected;
                    frame = 0;
                }
                if (selected < 0 || !IsModelAnimationValid(model, animations[selected]))
                    return;
                frame += dt * config.animationFps * settings.speed;
                const int count = animations[selected].keyframeCount;
                if (count > 0)
                    frame = settings.loop ? std::fmod(frame, static_cast<float>(count))
                                          : std::min(frame, static_cast<float>(count - 1));
                UpdateModelAnimation(model, animations[selected], frame);
            }
        };

        void setModernStyle()
        {
            GuiSetStyle(DEFAULT, TEXT_SIZE, 17);
            GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
            GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);
            GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x334155ff);
            GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x182235ff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xdbe7f5ff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x5aa7ffff);
            GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x263a55ff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xffffffff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x68b0ffff);
            GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x31517aff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xffffffff);
            GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x293548ff);
            GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0x151d2aff);
            GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0x64748bff);
            GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x0c1320ff);
        }

        void fillDefaultComponents(ActionConfig &action, const std::vector<ModelComponent> &components)
        {
            action.visibleComponents.clear();
            for (const auto &component : components)
                if (component.defaultVisible)
                    action.visibleComponents.push_back(component.name);
        }

        bool containsComponent(const ActionConfig &action, const std::string &name)
        {
            return std::find(action.visibleComponents.begin(), action.visibleComponents.end(), name) !=
                   action.visibleComponents.end();
        }

        void setComponent(ActionConfig &action, const std::string &name, bool visible)
        {
            const auto found = std::find(action.visibleComponents.begin(), action.visibleComponents.end(), name);
            if (visible && found == action.visibleComponents.end())
                action.visibleComponents.push_back(name);
            else if (!visible && found != action.visibleComponents.end())
                action.visibleComponents.erase(found);
        }

        fs::path jsonPath(fs::path path)
        {
            if (!path.empty() && lower(path.extension().string()) != ".json")
                path += ".json";
            return path;
        }

        void drawCaption(const char *text, float x, float y)
        {
            uiText(text, static_cast<int>(x), static_cast<int>(y), 15, Color{139, 159, 184, 255});
        }

        // 自绘的动画片段下拉：raygui 的 GuiDropdownBox 条目多时会画出屏幕且无法滚动。
        constexpr int dropdownItemHeight = 30;
        constexpr int dropdownVisibleItems = 8;

        Rectangle dropdownPanelBounds(Rectangle bounds, int itemCount)
        {
            const int visible = std::min(itemCount, dropdownVisibleItems);
            Rectangle panel = {bounds.x, bounds.y + bounds.height + 4, bounds.width,
                               static_cast<float>(visible * dropdownItemHeight + 8)};
            const float screenBottom = static_cast<float>(GetScreenHeight()) - 46;
            if (panel.y + panel.height > screenBottom)
                panel.y = bounds.y - 4 - panel.height;
            return panel;
        }

        void animationClipDropdown(Rectangle bounds, Preview &preview, ActionConfig &settings, bool &open, int &scroll)
        {
            const int itemCount = preview.animationCount + 1;
            const int current = resolveAnimation(settings.animation, preview.animations, preview.animationCount);
            const char *label = current >= 0 ? preview.animations[current].name : u8"（无）";
            if (GuiButton(bounds, label))
            {
                open = !open;
                scroll = 0;
            }
            if (!open)
                return;

            const Rectangle panel = dropdownPanelBounds(bounds, itemCount);
            const int maxScroll = std::max(0, itemCount * dropdownItemHeight - (static_cast<int>(panel.height) - 8));
            const Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, panel))
                scroll = std::clamp(scroll - static_cast<int>(GetMouseWheelMove() * dropdownItemHeight), 0, maxScroll);
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
                !CheckCollisionPointRec(mouse, panel) && !CheckCollisionPointRec(mouse, bounds))
            {
                open = false;
                return;
            }

            DrawRectangleRec(panel, Color{13, 21, 35, 255});
            DrawRectangleLinesEx(panel, 1, Color{63, 84, 110, 255});
            BeginScissorMode(static_cast<int>(panel.x) + 1, static_cast<int>(panel.y) + 1,
                             static_cast<int>(panel.width) - 2, static_cast<int>(panel.height) - 2);
            for (int i = 0; i < itemCount; ++i)
            {
                const float itemY = panel.y + 4 + i * dropdownItemHeight - scroll;
                if (itemY + dropdownItemHeight < panel.y || itemY > panel.y + panel.height)
                    continue;
                const Rectangle item = {panel.x + 4, itemY, panel.width - 8, dropdownItemHeight - 2.0f};
                const char *name = i == 0 ? u8"（无）" : preview.animations[i - 1].name;
                const bool selected = (i - 1) == current;
                if (CheckCollisionPointRec(mouse, item) && CheckCollisionPointRec(mouse, panel))
                {
                    DrawRectangleRec(item, Color{38, 58, 85, 255});
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    {
                        settings.animation = i == 0 ? std::string{} : preview.animations[i - 1].name;
                        preview.currentAnimation = -1;
                        open = false;
                    }
                }
                if (selected)
                    DrawRectangleLinesEx(item, 1, Color{104, 176, 255, 255});
                uiText(name, static_cast<int>(item.x) + 10, static_cast<int>(item.y) + 6, 16,
                       selected ? Color{170, 214, 255, 255} : Color{219, 231, 245, 255});
            }
            EndScissorMode();
            if (maxScroll > 0)
            {
                const float track = panel.height - 8;
                const float thumb = std::max(20.0f, track * track / (itemCount * dropdownItemHeight));
                const float thumbY = panel.y + 4 + (track - thumb) * (static_cast<float>(scroll) / maxScroll);
                DrawRectangleRec({panel.x + panel.width - 8, thumbY, 4, thumb}, Color{90, 122, 165, 255});
            }
        }

        void drawPreview(Preview &preview, const Config &config, Action action, Rectangle bounds)
        {
            DrawRectangleRounded(bounds, 0.035f, 10, Color{15, 24, 39, 255});
            DrawRectangleRoundedLinesEx(bounds, 0.035f, 10, 1.0f, Color{45, 61, 83, 255});
            const Rectangle viewport = {bounds.x + 12, bounds.y + 46, bounds.width - 24, bounds.height - 58};
            DrawRectangleRec(viewport, Color{8, 14, 24, 255});
            uiText(u8"实时预览", static_cast<int>(bounds.x + 18), static_cast<int>(bounds.y + 16), 16,
                   Color{137, 168, 205, 255});
            const char *label = actionLabelZh(action);
            uiText(label, static_cast<int>(bounds.x + bounds.width - uiMeasure(label, 17) - 18),
                   static_cast<int>(bounds.y + 15), 17, Color{102, 178, 255, 255});
            if (IsModelValid(preview.model))
            {
                // BeginMode3D 始终围绕整个窗口中心投影，必须离屏渲染到视口尺寸的
                // 纹理再贴回面板，否则窗口宽高比变化时模型会偏离预览区。
                preview.ensureTarget(static_cast<int>(viewport.width), static_cast<int>(viewport.height));
                const BoundingBox box = GetModelBoundingBox(preview.model);
                const float rawHeight = std::max(0.001f, box.max.y - box.min.y);
                const float scale = 2.3f / rawHeight;
                Camera3D camera{};
                camera.target = {0, 1.15f, 0};
                camera.position = {0, 1.7f, 5.8f};
                camera.up = {0, 1, 0};
                camera.fovy = 36;
                camera.projection = CAMERA_PERSPECTIVE;
                BeginTextureMode(preview.target);
                ClearBackground(Color{8, 14, 24, 255});
                BeginMode3D(camera);
                DrawGrid(10, 0.5f);
                drawConfiguredModel(preview.model, preview.components, config.actions[slot(action)],
                                    {0, 0, 0}, {0, 1, 0}, 0, {scale, scale, scale}, WHITE);
                EndMode3D();
                EndTextureMode();
                DrawTextureRec(preview.target.texture, {0, 0, viewport.width, -viewport.height},
                               {viewport.x, viewport.y}, WHITE);
            }
            else
            {
                const char *message = u8"请选择模型开始预览";
                uiText(message, static_cast<int>(viewport.x + (viewport.width - uiMeasure(message, 18)) * 0.5f),
                       static_cast<int>(viewport.y + viewport.height * 0.5f), 18, Color{102, 119, 143, 255});
            }
        }
    }

    bool runConfigurator(Config &config)
    {
        // SetConfigFlags 只置位且 CloseWindow 不清除，这里的 MSAA 标志会泄漏到之后
        // 创建的透明桌宠窗口；X11 上会选到无 alpha 的 24 位 visual，画出黑框。
#if defined(__linux__)
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#else
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
#endif
        InitWindow(editorWidth, editorHeight, u8"Crose 桌宠工作室");
        SetWindowMinSize(980, 680);
        SetTargetFPS(60);
        setModernStyle();

        Preview preview;
        std::string status = u8"请选择模型或打开已保存的 JSON 配置";
        if (!config.model.empty())
            preview.load(config.model, status);

        char profileName[128]{};
        char modelPath[1024]{};
        copyText(profileName, sizeof(profileName), config.name);
        copyText(modelPath, sizeof(modelPath), config.model.u8string());
        bool editName = false, editModel = false, dropdownOpen = false;
        int page = 0;
        int selectedAction = 0, previousAction = -1;
        int actionScroll = 0, actionFocus = -1;
        int dropdownScroll = 0;
        float componentScroll = 0;
        bool runPet = false;

        while (!WindowShouldClose() && !runPet)
        {
            const float dt = std::min(GetFrameTime(), 0.05f);
            const Action action = static_cast<Action>(selectedAction);
            preview.update(config, action, dt);

            if (selectedAction != previousAction)
            {
                dropdownOpen = false;
                dropdownScroll = 0;
                previousAction = selectedAction;
            }
            if (page != 0)
                dropdownOpen = false;

            std::string dynamicTexts = status + profileName + modelPath + config.name + config.model.u8string();
            for (const auto &component : preview.components)
                dynamicTexts += component.name;
            for (int i = 0; i < preview.animationCount; ++i)
                dynamicTexts += preview.animations[i].name;
            refreshUiFont(dynamicTexts);

            const int width = GetScreenWidth();
            const int height = GetScreenHeight();
            const float margin = 24;
            const float top = 84;
            const float footer = 42;
            const float rightWidth = std::clamp(width * 0.37f, 390.0f, 500.0f);
            const Rectangle leftPanel = {margin, top, width - rightWidth - margin * 3, height - top - footer - margin};
            const Rectangle rightPanel = {leftPanel.x + leftPanel.width + margin, top,
                                          rightWidth, leftPanel.height};
            Rectangle animationDropdownBounds{};
            bool dropdownCoversMouse = false;

            BeginDrawing();
            ClearBackground(Color{10, 16, 27, 255});
            DrawRectangle(0, 0, width, 68, Color{15, 24, 39, 255});
            uiText(u8"桌宠配置工作室", 24, 18, 24, Color{230, 240, 252, 255});
            uiText(u8"构建可复用的 3D 桌宠配置", 24, 46, 14, Color{117, 139, 167, 255});

            if (GuiButton({static_cast<float>(width - 454), 18, 100, 34}, u8"打开 JSON"))
            {
                const auto files = pfd::open_file(u8"打开宠物配置", config.profilePath.parent_path().string(),
                                                  {u8"宠物配置", "*.json", u8"所有文件", "*"}).result();
                if (!files.empty())
                {
                    Config loaded;
                    if (loadConfig(fs::u8path(files.front()), loaded, status))
                    {
                        config = std::move(loaded);
                        copyText(profileName, sizeof(profileName), config.name);
                        copyText(modelPath, sizeof(modelPath), config.model.u8string());
                        preview.load(config.model, status);
                    }
                }
            }
            if (GuiButton({static_cast<float>(width - 346), 18, 82, 34}, u8"保存"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                fs::path target = config.profilePath;
                if (lower(target.extension().string()) != ".json")
                    target.clear();
                if (target.empty())
                {
                    const std::string chosen = pfd::save_file(u8"保存宠物配置", "pet-profile.json",
                                                              {u8"宠物配置", "*.json"}).result();
                    if (!chosen.empty())
                        target = jsonPath(fs::u8path(chosen));
                }
                if (!target.empty() && saveConfigJson(target, config, status))
                    status = u8"已保存 " + target.filename().u8string();
            }
            if (GuiButton({static_cast<float>(width - 256), 18, 96, 34}, u8"另存为"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                const std::string chosen = pfd::save_file(u8"保存宠物配置", "pet-profile.json",
                                                          {u8"宠物配置", "*.json"}).result();
                const fs::path target = jsonPath(fs::u8path(chosen));
                if (!chosen.empty() && saveConfigJson(target, config, status))
                    status = u8"已保存 " + target.filename().u8string();
            }
            if (GuiButton({static_cast<float>(width - 152), 18, 128, 34}, u8"启动桌宠  >"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                if (!config.model.empty() && !fs::exists(config.model))
                    status = u8"请先选择存在的模型文件";
                else if (validateConfig(config, status))
                    runPet = true;
            }

            DrawRectangleRounded(leftPanel, 0.025f, 10, Color{15, 24, 39, 255});
            DrawRectangleRoundedLinesEx(leftPanel, 0.025f, 10, 1, Color{45, 61, 83, 255});
            const char *tabs[] = {u8"模型与动作", u8"显示与行为"};
            GuiTabBar({leftPanel.x + 16, leftPanel.y + 14, leftPanel.width - 32, 36}, tabs, 2, &page);

            if (page == 0)
            {
                const float x = leftPanel.x + 20;
                const float contentY = leftPanel.y + 68;
                drawCaption(u8"配置名称", x, contentY);
                if (GuiTextBox({x, contentY + 20, leftPanel.width - 40, 36}, profileName,
                               static_cast<int>(sizeof(profileName)), editName))
                    editName = !editName;

                drawCaption(u8"3D 模型", x, contentY + 70);
                const float browseWidth = 92;
                if (GuiTextBox({x, contentY + 90, leftPanel.width - 48 - browseWidth, 36}, modelPath,
                               static_cast<int>(sizeof(modelPath)), editModel))
                {
                    editModel = !editModel;
                    if (!editModel)
                    {
                        config.model = fs::u8path(modelPath);
                        preview.load(config.model, status);
                    }
                }
                if (GuiButton({x + leftPanel.width - 40 - browseWidth, contentY + 90, browseWidth, 36}, u8"浏览..."))
                {
                    const auto files = pfd::open_file(u8"选择 3D 宠物模型", config.model.parent_path().string(),
                                                      {u8"glTF 模型", "*.glb *.gltf", u8"所有文件", "*"}).result();
                    if (!files.empty())
                    {
                        config.model = fs::u8path(files.front());
                        copyText(modelPath, sizeof(modelPath), config.model.u8string());
                        preview.load(config.model, status);
                    }
                }

                const float actionY = contentY + 148;
                drawCaption(u8"动作", x, actionY);
                std::array<const char *, actionCount> actionLabels{};
                for (int i = 0; i < actionCount; ++i)
                    actionLabels[i] = actionLabelZh(static_cast<Action>(i));
                GuiListViewEx({x, actionY + 20, 150, leftPanel.height - 266}, actionLabels.data(), actionCount,
                              &actionScroll, &selectedAction, &actionFocus);
                // GuiListViewEx 再次点击已选中项会将其置为 -1，桌宠配置必须始终有一个有效动作
                if (selectedAction < 0 || selectedAction >= actionCount)
                    selectedAction = (previousAction >= 0 && previousAction < actionCount) ? previousAction : 0;

                const float detailX = x + 170;
                const float detailWidth = leftPanel.width - 210;
                ActionConfig &settings = config.actions[selectedAction];
                drawCaption(u8"动画片段", detailX, actionY);
                animationDropdownBounds = {detailX, actionY + 20, detailWidth, 36};
                // 下拉展开盖住下层控件时，屏蔽它们的交互，避免选择条目时误触
                if (dropdownOpen)
                {
                    const Rectangle panel = dropdownPanelBounds(animationDropdownBounds, preview.animationCount + 1);
                    const Vector2 mouse = GetMousePosition();
                    dropdownCoversMouse = CheckCollisionPointRec(mouse, panel) ||
                                          CheckCollisionPointRec(mouse, animationDropdownBounds);
                }

                bool loop = settings.loop;
                if (!dropdownCoversMouse && GuiCheckBox({detailX, actionY + 70, 22, 22}, u8"循环播放", &loop))
                    settings.loop = loop;
                drawCaption(u8"播放速度", detailX, actionY + 108);
                if (!dropdownCoversMouse)
                    GuiSliderBar({detailX, actionY + 130, detailWidth, 24}, "0.25x", "3.0x", &settings.speed, 0.25f, 3.0f);
                uiText(TextFormat("%.2fx", settings.speed), static_cast<int>(detailX + detailWidth - 48),
                       static_cast<int>(actionY + 108), 14, Color{102, 178, 255, 255});

                drawCaption(u8"可见组件", detailX, actionY + 172);
                bool modelDefaults = settings.useModelComponents;
                if (!dropdownCoversMouse &&
                    GuiCheckBox({detailX, actionY + 194, 22, 22}, u8"使用模型自带的可见性", &modelDefaults))
                {
                    if (settings.useModelComponents && !modelDefaults)
                        fillDefaultComponents(settings, preview.components);
                    settings.useModelComponents = modelDefaults;
                }
                if (!dropdownCoversMouse && GuiButton({detailX, actionY + 226, 64, 28}, u8"全选"))
                {
                    settings.useModelComponents = false;
                    settings.visibleComponents.clear();
                    for (const auto &component : preview.components)
                        settings.visibleComponents.push_back(component.name);
                }
                if (!dropdownCoversMouse && GuiButton({detailX + 70, actionY + 226, 64, 28}, u8"全不选"))
                {
                    settings.useModelComponents = false;
                    settings.visibleComponents.clear();
                }
                if (!dropdownCoversMouse && GuiButton({detailX + 140, actionY + 226, 82, 28}, u8"默认"))
                {
                    settings.useModelComponents = true;
                    settings.visibleComponents.clear();
                }

                const Rectangle componentArea = {detailX, actionY + 262, detailWidth,
                                                 std::max(50.0f, leftPanel.y + leftPanel.height - actionY - 282)};
                DrawRectangleRec(componentArea, Color{10, 18, 30, 255});
                DrawRectangleLinesEx(componentArea, 1, Color{38, 53, 73, 255});
                if (!dropdownCoversMouse && CheckCollisionPointRec(GetMousePosition(), componentArea))
                    componentScroll = std::clamp(componentScroll - GetMouseWheelMove() * 28.0f, 0.0f,
                                                 std::max(0.0f, static_cast<float>(preview.components.size() * 30) - componentArea.height + 12));
                BeginScissorMode(static_cast<int>(componentArea.x), static_cast<int>(componentArea.y),
                                 static_cast<int>(componentArea.width), static_cast<int>(componentArea.height));
                float itemY = componentArea.y + 8 - componentScroll;
                for (const auto &component : preview.components)
                {
                    bool visible = settings.useModelComponents ? component.defaultVisible : containsComponent(settings, component.name);
                    if (!dropdownCoversMouse &&
                        itemY + 24 >= componentArea.y && itemY <= componentArea.y + componentArea.height &&
                        GuiCheckBox({componentArea.x + 10, itemY, 20, 20}, component.name.c_str(), &visible))
                    {
                        if (settings.useModelComponents)
                        {
                            fillDefaultComponents(settings, preview.components);
                            settings.useModelComponents = false;
                        }
                        setComponent(settings, component.name, visible);
                    }
                    itemY += 30;
                }
                EndScissorMode();
            }
            else
            {
                const float x = leftPanel.x + 24;
                const float y = leftPanel.y + 76;
                const float column = (leftPanel.width - 70) * 0.5f;
                drawCaption(u8"宠物窗口", x, y);
                drawCaption(u8"宽度", x, y + 26);
                GuiSpinner({x, y + 46, column, 30}, NULL, &config.width, 160, 1600, false);
                drawCaption(u8"高度", x, y + 88);
                GuiSpinner({x, y + 108, column, 30}, NULL, &config.height, 160, 1600, false);
                drawCaption(u8"帧率", x, y + 150);
                GuiSpinner({x, y + 170, column, 30}, NULL, &config.fps, 15, 240, false);
                bool topmost = config.alwaysOnTop;
                if (GuiCheckBox({x, y + 214, 22, 22}, u8"窗口置顶", &topmost))
                    config.alwaysOnTop = topmost;

                const float x2 = x + column + 28;
                drawCaption(u8"模型与相机", x2, y);
                drawCaption(u8"模型高度", x2, y + 26);
                GuiSliderBar({x2, y + 48, column, 24}, "0.5", "5.0", &config.modelHeight, 0.5f, 5.0f);
                drawCaption(u8"相机距离", x2, y + 84);
                GuiSliderBar({x2, y + 106, column, 24}, "2", "15", &config.cameraDistance, 2.0f, 15.0f);
                drawCaption(u8"相机俯仰角", x2, y + 142);
                GuiSliderBar({x2, y + 164, column, 24}, "-25", "65", &config.cameraElevation, -25.0f, 65.0f);
                drawCaption(u8"相机目标高度", x2, y + 200);
                GuiSliderBar({x2, y + 222, column, 24}, "-1", "4", &config.cameraTargetY, -1.0f, 4.0f);

                const float behaviorY = y + 268;
                drawCaption(u8"自动移动", x, behaviorY);
                bool autoWalk = config.autoWalk;
                if (GuiCheckBox({x, behaviorY + 26, 22, 22}, u8"启用自动移动", &autoWalk))
                    config.autoWalk = autoWalk;
                drawCaption(u8"移动速度（像素/秒）", x, behaviorY + 62);
                GuiSliderBar({x, behaviorY + 84, column, 24}, "0", "150", &config.walkSpeed, 0, 150);
                drawCaption(u8"间隔（秒）", x, behaviorY + 120);
                GuiSliderBar({x, behaviorY + 142, column, 24}, "0.5", "30", &config.walkInterval, 0.5f, 30);
                drawCaption(u8"持续（秒）", x, behaviorY + 178);
                GuiSliderBar({x, behaviorY + 200, column, 24}, "0.2", "10", &config.walkDuration, 0.2f, 10);

                drawCaption(u8"动画", x2, behaviorY);
                drawCaption(u8"源采样率", x2, behaviorY + 30);
                GuiSliderBar({x2, behaviorY + 52, column, 24}, "12", "120", &config.animationFps, 12, 120);
                drawCaption(u8"混合时长", x2, behaviorY + 88);
                GuiSliderBar({x2, behaviorY + 110, column, 24}, "0", "1.0s", &config.blendSeconds, 0, 1.0f);
            }

            drawPreview(preview, config, action, rightPanel);
            // 下拉面板最后绘制，保证盖过下方控件与预览
            if (page == 0)
                animationClipDropdown(animationDropdownBounds, preview, config.actions[selectedAction],
                                      dropdownOpen, dropdownScroll);
            DrawRectangle(0, height - footer, width, footer, Color{15, 24, 39, 255});
            DrawCircle(22, height - 21, 4, Color{77, 181, 126, 255});
            uiText(status.c_str(), 34, height - 28, 15, Color{145, 164, 188, 255});
            EndDrawing();
        }

        preview.unload();
        unloadUiFont();
        CloseWindow();
        return runPet;
    }
}
