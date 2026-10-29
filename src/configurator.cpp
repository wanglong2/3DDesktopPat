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

            ~Preview() { unload(); }

            void unload()
            {
                if (animations)
                    UnloadModelAnimations(animations, animationCount);
                if (IsModelValid(model))
                    UnloadModel(model);
                model = {};
                animations = nullptr;
                animationCount = 0;
                components.clear();
                loadedPath.clear();
                frame = 0;
                currentAnimation = -1;
            }

            bool load(const fs::path &path, std::string &error)
            {
                unload();
                if (path.empty())
                {
                    error = "Choose a .glb or .gltf model";
                    return false;
                }
                if (!fs::exists(path))
                {
                    error = "Model file does not exist";
                    return false;
                }
                loadedPath = path.u8string();
                model = LoadModel(loadedPath.c_str());
                if (!IsModelValid(model))
                {
                    error = "The selected model could not be loaded";
                    unload();
                    return false;
                }
                animations = LoadModelAnimations(loadedPath.c_str(), &animationCount);
                components = inspectModelComponents(loadedPath, model.materialCount);

                const BoundingBox bounds = GetModelBoundingBox(model);
                const float centerX = (bounds.min.x + bounds.max.x) * 0.5f;
                const float centerZ = (bounds.min.z + bounds.max.z) * 0.5f;
                model.transform = MatrixMultiply(model.transform, MatrixTranslate(-centerX, -bounds.min.y, -centerZ));
                error = "Loaded " + std::to_string(animationCount) + " animations and " +
                        std::to_string(components.size()) + " components";
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

        std::string animationChoices(const Preview &preview)
        {
            std::string choices = "(none)";
            for (int i = 0; i < preview.animationCount; ++i)
            {
                choices.push_back(';');
                choices += preview.animations[i].name;
            }
            return choices;
        }

        fs::path jsonPath(fs::path path)
        {
            if (!path.empty() && lower(path.extension().string()) != ".json")
                path += ".json";
            return path;
        }

        void drawCaption(const char *text, float x, float y)
        {
            DrawText(text, static_cast<int>(x), static_cast<int>(y), 15, Color{139, 159, 184, 255});
        }

        void drawPreview(Preview &preview, const Config &config, Action action, Rectangle bounds)
        {
            DrawRectangleRounded(bounds, 0.035f, 10, Color{15, 24, 39, 255});
            DrawRectangleRoundedLinesEx(bounds, 0.035f, 10, 1.0f, Color{45, 61, 83, 255});
            const Rectangle viewport = {bounds.x + 12, bounds.y + 46, bounds.width - 24, bounds.height - 58};
            DrawText("LIVE PREVIEW", static_cast<int>(bounds.x + 18), static_cast<int>(bounds.y + 16), 16,
                     Color{137, 168, 205, 255});
            DrawText(actionLabel(action), static_cast<int>(bounds.x + bounds.width - MeasureText(actionLabel(action), 17) - 18),
                     static_cast<int>(bounds.y + 15), 17, Color{102, 178, 255, 255});
            BeginScissorMode(static_cast<int>(viewport.x), static_cast<int>(viewport.y),
                             static_cast<int>(viewport.width), static_cast<int>(viewport.height));
            DrawRectangleRec(viewport, Color{8, 14, 24, 255});
            if (IsModelValid(preview.model))
            {
                const BoundingBox box = GetModelBoundingBox(preview.model);
                const float rawHeight = std::max(0.001f, box.max.y - box.min.y);
                const float scale = 2.3f / rawHeight;
                Camera3D camera{};
                camera.target = {0, 1.15f, 0};
                camera.position = {0, 1.7f, 5.8f};
                camera.up = {0, 1, 0};
                camera.fovy = 36;
                camera.projection = CAMERA_PERSPECTIVE;
                BeginMode3D(camera);
                DrawGrid(10, 0.5f);
                drawConfiguredModel(preview.model, preview.components, config.actions[slot(action)],
                                    {0, 0, 0}, {0, 1, 0}, 0, {scale, scale, scale}, WHITE);
                EndMode3D();
            }
            else
            {
                const char *message = "Select a model to begin";
                DrawText(message, static_cast<int>(viewport.x + (viewport.width - MeasureText(message, 18)) * 0.5f),
                         static_cast<int>(viewport.y + viewport.height * 0.5f), 18, Color{102, 119, 143, 255});
            }
            EndScissorMode();
        }
    }

    bool runConfigurator(Config &config)
    {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
        InitWindow(editorWidth, editorHeight, "Crose Pet Studio");
        SetWindowMinSize(980, 680);
        SetTargetFPS(60);
        setModernStyle();

        Preview preview;
        std::string status = "Choose a model or open a saved JSON profile";
        if (!config.model.empty())
            preview.load(config.model, status);

        char profileName[128]{};
        char modelPath[1024]{};
        copyText(profileName, sizeof(profileName), config.name);
        copyText(modelPath, sizeof(modelPath), config.model.u8string());
        bool editName = false, editModel = false, dropdownOpen = false;
        int page = 0;
        int selectedAction = 0;
        int actionScroll = 0, actionFocus = -1;
        float componentScroll = 0;
        bool runPet = false;

        while (!WindowShouldClose() && !runPet)
        {
            const float dt = std::min(GetFrameTime(), 0.05f);
            const Action action = static_cast<Action>(selectedAction);
            preview.update(config, action, dt);

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
            std::string dropdownChoices;
            int dropdownAnimationIndex = 0;
            bool showAnimationDropdown = false;

            BeginDrawing();
            ClearBackground(Color{10, 16, 27, 255});
            DrawRectangle(0, 0, width, 68, Color{15, 24, 39, 255});
            DrawText("CROSE PET STUDIO", 24, 18, 24, Color{230, 240, 252, 255});
            DrawText("Build a reusable 3D pet profile", 24, 46, 14, Color{117, 139, 167, 255});

            if (GuiButton({static_cast<float>(width - 454), 18, 100, 34}, "Open JSON"))
            {
                const auto files = pfd::open_file("Open pet profile", config.profilePath.parent_path().string(),
                                                  {"Pet profile", "*.json", "All files", "*"}).result();
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
            if (GuiButton({static_cast<float>(width - 346), 18, 82, 34}, "Save"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                fs::path target = config.profilePath;
                if (lower(target.extension().string()) != ".json")
                    target.clear();
                if (target.empty())
                {
                    const std::string chosen = pfd::save_file("Save pet profile", "pet-profile.json",
                                                              {"Pet profile", "*.json"}).result();
                    if (!chosen.empty())
                        target = jsonPath(fs::u8path(chosen));
                }
                if (!target.empty() && saveConfigJson(target, config, status))
                    status = "Saved " + target.filename().u8string();
            }
            if (GuiButton({static_cast<float>(width - 256), 18, 96, 34}, "Save As"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                const std::string chosen = pfd::save_file("Save pet profile", "pet-profile.json",
                                                          {"Pet profile", "*.json"}).result();
                const fs::path target = jsonPath(fs::u8path(chosen));
                if (!chosen.empty() && saveConfigJson(target, config, status))
                    status = "Saved " + target.filename().u8string();
            }
            if (GuiButton({static_cast<float>(width - 152), 18, 128, 34}, "Run Pet  >"))
            {
                config.name = profileName;
                config.model = fs::u8path(modelPath);
                if (!config.model.empty() && !fs::exists(config.model))
                    status = "Select an existing model before running";
                else if (validateConfig(config, status))
                    runPet = true;
            }

            DrawRectangleRounded(leftPanel, 0.025f, 10, Color{15, 24, 39, 255});
            DrawRectangleRoundedLinesEx(leftPanel, 0.025f, 10, 1, Color{45, 61, 83, 255});
            const char *tabs[] = {"MODEL & ACTIONS", "DISPLAY & BEHAVIOR"};
            GuiTabBar({leftPanel.x + 16, leftPanel.y + 14, leftPanel.width - 32, 36}, tabs, 2, &page);

            if (page == 0)
            {
                const float x = leftPanel.x + 20;
                const float contentY = leftPanel.y + 68;
                drawCaption("PROFILE NAME", x, contentY);
                if (GuiTextBox({x, contentY + 20, leftPanel.width - 40, 36}, profileName,
                               static_cast<int>(sizeof(profileName)), editName))
                    editName = !editName;

                drawCaption("3D MODEL", x, contentY + 70);
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
                if (GuiButton({x + leftPanel.width - 40 - browseWidth, contentY + 90, browseWidth, 36}, "Browse..."))
                {
                    const auto files = pfd::open_file("Choose a 3D pet model", config.model.parent_path().string(),
                                                      {"glTF model", "*.glb *.gltf", "All files", "*"}).result();
                    if (!files.empty())
                    {
                        config.model = fs::u8path(files.front());
                        copyText(modelPath, sizeof(modelPath), config.model.u8string());
                        preview.load(config.model, status);
                    }
                }

                const float actionY = contentY + 148;
                drawCaption("ACTION", x, actionY);
                std::array<const char *, actionCount> actionLabels{};
                for (int i = 0; i < actionCount; ++i)
                    actionLabels[i] = actionLabel(static_cast<Action>(i));
                GuiListViewEx({x, actionY + 20, 150, leftPanel.height - 266}, actionLabels.data(), actionCount,
                              &actionScroll, &selectedAction, &actionFocus);

                const float detailX = x + 170;
                const float detailWidth = leftPanel.width - 210;
                ActionConfig &settings = config.actions[selectedAction];
                drawCaption("ANIMATION CLIP", detailX, actionY);
                dropdownChoices = animationChoices(preview);
                dropdownAnimationIndex = resolveAnimation(settings.animation, preview.animations, preview.animationCount) + 1;
                animationDropdownBounds = {detailX, actionY + 20, detailWidth, 36};
                showAnimationDropdown = true;

                bool loop = settings.loop;
                if (GuiCheckBox({detailX, actionY + 70, 22, 22}, "Loop this action", &loop))
                    settings.loop = loop;
                drawCaption("SPEED", detailX, actionY + 108);
                GuiSliderBar({detailX, actionY + 130, detailWidth, 24}, "0.25x", "3.0x", &settings.speed, 0.25f, 3.0f);
                DrawText(TextFormat("%.2fx", settings.speed), static_cast<int>(detailX + detailWidth - 48),
                         static_cast<int>(actionY + 108), 14, Color{102, 178, 255, 255});

                drawCaption("VISIBLE COMPONENTS", detailX, actionY + 172);
                bool modelDefaults = settings.useModelComponents;
                if (GuiCheckBox({detailX, actionY + 194, 22, 22}, "Use visibility stored in model", &modelDefaults))
                {
                    if (settings.useModelComponents && !modelDefaults)
                        fillDefaultComponents(settings, preview.components);
                    settings.useModelComponents = modelDefaults;
                }
                if (GuiButton({detailX, actionY + 226, 64, 28}, "All"))
                {
                    settings.useModelComponents = false;
                    settings.visibleComponents.clear();
                    for (const auto &component : preview.components)
                        settings.visibleComponents.push_back(component.name);
                }
                if (GuiButton({detailX + 70, actionY + 226, 64, 28}, "None"))
                {
                    settings.useModelComponents = false;
                    settings.visibleComponents.clear();
                }
                if (GuiButton({detailX + 140, actionY + 226, 82, 28}, "Defaults"))
                {
                    settings.useModelComponents = true;
                    settings.visibleComponents.clear();
                }

                const Rectangle componentArea = {detailX, actionY + 262, detailWidth,
                                                 std::max(50.0f, leftPanel.y + leftPanel.height - actionY - 282)};
                DrawRectangleRec(componentArea, Color{10, 18, 30, 255});
                DrawRectangleLinesEx(componentArea, 1, Color{38, 53, 73, 255});
                if (CheckCollisionPointRec(GetMousePosition(), componentArea))
                    componentScroll = std::clamp(componentScroll - GetMouseWheelMove() * 28.0f, 0.0f,
                                                 std::max(0.0f, static_cast<float>(preview.components.size() * 30) - componentArea.height + 12));
                BeginScissorMode(static_cast<int>(componentArea.x), static_cast<int>(componentArea.y),
                                 static_cast<int>(componentArea.width), static_cast<int>(componentArea.height));
                float itemY = componentArea.y + 8 - componentScroll;
                for (const auto &component : preview.components)
                {
                    bool visible = settings.useModelComponents ? component.defaultVisible : containsComponent(settings, component.name);
                    if (itemY + 24 >= componentArea.y && itemY <= componentArea.y + componentArea.height &&
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
                drawCaption("PET WINDOW", x, y);
                GuiSpinner({x, y + 24, column, 36}, "Width", &config.width, 160, 1600, false);
                GuiSpinner({x, y + 70, column, 36}, "Height", &config.height, 160, 1600, false);
                GuiSpinner({x, y + 116, column, 36}, "FPS", &config.fps, 15, 240, false);
                bool topmost = config.alwaysOnTop;
                if (GuiCheckBox({x, y + 168, 22, 22}, "Always on top", &topmost))
                    config.alwaysOnTop = topmost;

                const float x2 = x + column + 28;
                drawCaption("MODEL & CAMERA", x2, y);
                drawCaption("Model height", x2, y + 28);
                GuiSliderBar({x2, y + 50, column, 24}, "0.5", "5.0", &config.modelHeight, 0.5f, 5.0f);
                drawCaption("Camera distance", x2, y + 86);
                GuiSliderBar({x2, y + 108, column, 24}, "2", "15", &config.cameraDistance, 2.0f, 15.0f);
                drawCaption("Camera elevation", x2, y + 144);
                GuiSliderBar({x2, y + 166, column, 24}, "-25", "65", &config.cameraElevation, -25.0f, 65.0f);
                drawCaption("Camera target Y", x2, y + 202);
                GuiSliderBar({x2, y + 224, column, 24}, "-1", "4", &config.cameraTargetY, -1.0f, 4.0f);

                const float behaviorY = y + 292;
                drawCaption("AUTONOMOUS MOVEMENT", x, behaviorY);
                bool autoWalk = config.autoWalk;
                if (GuiCheckBox({x, behaviorY + 26, 22, 22}, "Enable automatic movement", &autoWalk))
                    config.autoWalk = autoWalk;
                drawCaption("Speed (pixels/sec)", x, behaviorY + 66);
                GuiSliderBar({x, behaviorY + 88, column, 24}, "0", "150", &config.walkSpeed, 0, 150);
                drawCaption("Interval (seconds)", x, behaviorY + 124);
                GuiSliderBar({x, behaviorY + 146, column, 24}, "0.5", "30", &config.walkInterval, 0.5f, 30);
                drawCaption("Duration (seconds)", x, behaviorY + 182);
                GuiSliderBar({x, behaviorY + 204, column, 24}, "0.2", "10", &config.walkDuration, 0.2f, 10);

                drawCaption("ANIMATION", x2, behaviorY);
                drawCaption("Source sample rate", x2, behaviorY + 30);
                GuiSliderBar({x2, behaviorY + 52, column, 24}, "12", "120", &config.animationFps, 12, 120);
                drawCaption("Blend duration", x2, behaviorY + 88);
                GuiSliderBar({x2, behaviorY + 110, column, 24}, "0", "1.0s", &config.blendSeconds, 0, 1.0f);
            }

            drawPreview(preview, config, action, rightPanel);
            // Draw the expanded list last so it stays above the action controls below it.
            if (showAnimationDropdown &&
                GuiDropdownBox(animationDropdownBounds, dropdownChoices.c_str(), &dropdownAnimationIndex, dropdownOpen))
            {
                dropdownOpen = !dropdownOpen;
                if (!dropdownOpen)
                {
                    ActionConfig &settings = config.actions[selectedAction];
                    settings.animation = dropdownAnimationIndex <= 0 ? std::string{}
                                                                     : preview.animations[dropdownAnimationIndex - 1].name;
                    preview.currentAnimation = -1;
                }
            }
            DrawRectangle(0, height - footer, width, footer, Color{15, 24, 39, 255});
            DrawCircle(22, height - 21, 4, Color{77, 181, 126, 255});
            DrawText(status.c_str(), 34, height - 28, 15, Color{145, 164, 188, 255});
            EndDrawing();
        }

        preview.unload();
        CloseWindow();
        return runPet;
    }
}
