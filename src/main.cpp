#include "raylib.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "raymath.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "rlgl.h"
#include "system_tray.h"
#include "configurator.h"
#include "model_render.h"
#include "pet_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{

    std::string trim(std::string s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // raylib's default Windows loader uses fopen(), which cannot open UTF-8 paths
    // with Chinese characters under a non-UTF-8 system code page.
    unsigned char *loadUtf8FileData(const char *name, int *dataSize)
    {
        *dataSize = 0;
        if (!name)
            return nullptr;
        try
        {
            std::ifstream file(fs::u8path(name), std::ios::binary | std::ios::ate);
            if (!file)
                return nullptr;
            const auto length = file.tellg();
            if (length <= std::streampos(0) || length > std::streampos(INT_MAX))
                return nullptr;
            file.seekg(0);
            const auto size = static_cast<std::streamsize>(length);
            auto *data = static_cast<unsigned char *>(MemAlloc(static_cast<unsigned int>(size)));
            if (!data)
                return nullptr;
            if (!file.read(reinterpret_cast<char *>(data), size))
            {
                MemFree(data);
                return nullptr;
            }
            *dataSize = static_cast<int>(size);
            return data;
        }
        catch (const std::exception &)
        {
            return nullptr;
        }
    }

    char *loadUtf8FileText(const char *name)
    {
        if (!name)
            return nullptr;
        try
        {
            std::ifstream file(fs::u8path(name), std::ios::binary | std::ios::ate);
            if (!file)
                return nullptr;
            const auto length = file.tellg();
            if (length < std::streampos(0) || length >= std::streampos(INT_MAX))
                return nullptr;
            file.seekg(0);
            const auto size = static_cast<std::streamsize>(length);
            auto *data = static_cast<char *>(MemAlloc(static_cast<unsigned int>(size + 1)));
            if (!data)
                return nullptr;
            if (size && !file.read(data, size))
            {
                MemFree(data);
                return nullptr;
            }
            data[size] = '\0';
            return data;
        }
        catch (const std::exception &)
        {
            return nullptr;
        }
    }


    using crose::Action;
    using crose::Config;
    using crose::actionCount;
    using crose::slot;

    int resolveClip(const std::string &alternatives, Model model, const ModelAnimation *animations, int count)
    {
        std::istringstream stream(alternatives);
        std::string candidate;
        while (std::getline(stream, candidate, '|'))
        {
            candidate = trim(candidate);
            if (candidate.empty())
                continue;
            if (candidate[0] == '#')
            {
                try
                {
                    const int index = std::stoi(candidate.substr(1));
                    if (index >= 0 && index < count && IsModelAnimationValid(model, animations[index]))
                        return index;
                }
                catch (const std::exception &)
                {
                }
            }
            for (int i = 0; i < count; ++i)
            {
                if (lower(animations[i].name) == lower(candidate) && IsModelAnimationValid(model, animations[i]))
                    return i;
            }
        }
        return -1;
    }

    float smooth(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // 把水平朝向与俯仰合成为单一旋转轴与角度，供 drawConfiguredModel 使用。
    Vector3 facingAxis(float yawDegrees, float pitchDegrees, float &angleDegrees)
    {
        const Quaternion turn = QuaternionMultiply(QuaternionFromAxisAngle({0, 1, 0}, yawDegrees * DEG2RAD),
                                                   QuaternionFromAxisAngle({1, 0, 0}, pitchDegrees * DEG2RAD));
        Vector3 axis = {0, 1, 0};
        float angle = 0;
        QuaternionToAxisAngle(turn, &axis, &angle);
        angleDegrees = angle * RAD2DEG;
        return axis;
    }

    class AnimationPlayer
    {
    public:
        AnimationPlayer(Model &model, ModelAnimation *animations, int count, const Config &config)
            : model_(model), animations_(animations), count_(count), config_(config)
        {
            for (int i = 0; i < actionCount; ++i)
                clips_[i] = resolveClip(config.actions[i].animation, model_, animations_, count_);
            if (clips_[slot(Action::Idle)] < 0 && count_ > 0)
            {
                for (int i = 0; i < count_; ++i)
                    if (IsModelAnimationValid(model_, animations_[i]))
                    {
                        clips_[slot(Action::Idle)] = i;
                        break;
                    }
            }
            currentAction_ = clips_[slot(Action::Startup)] >= 0 ? Action::Startup : Action::Idle;
            current_ = clips_[slot(currentAction_)];
            if (current_ >= 0)
                UpdateModelAnimation(model_, animations_[current_], 0);
            snapshot_.boneCount = model_.skeleton.boneCount;
            snapshot_.keyframeCount = 1;
            snapshotPose_.resize(snapshot_.boneCount);
            snapshotPointer_ = snapshotPose_.data();
            snapshot_.keyframePoses = &snapshotPointer_;
        }

        int clip(Action action) const { return clips_[slot(action)]; }
        float duration(Action action) const
        {
            const int index = clip(action);
            return index >= 0 ? animations_[index].keyframeCount /
                                    (config_.animationFps * config_.actions[slot(action)].speed)
                              : 0.65f;
        }
        bool finished() const { return current_ < 0 ? elapsed_ >= 0.65f : frame_ >= animations_[current_].keyframeCount - 1; }

        void play(Action action)
        {
            const int next = clip(action);
            if (next == current_ && action != Action::LeftClick && action != Action::RightClick && action != Action::Exit)
            {
                currentAction_ = action;
                return;
            }
            if (next >= 0 && model_.currentPose && snapshot_.boneCount > 0)
            {
                std::copy_n(model_.currentPose, snapshot_.boneCount, snapshotPose_.begin());
                blending_ = current_ >= 0 && config_.blendSeconds > 0;
                blendElapsed_ = 0;
            }
            else
                blending_ = false;
            current_ = next;
            currentAction_ = action;
            frame_ = 0;
            elapsed_ = 0;
        }

        void update(float dt, bool loop)
        {
            elapsed_ += dt;
            if (current_ < 0)
                return;
            const int count = animations_[current_].keyframeCount;
            if (count <= 0)
                return;
            frame_ += dt * config_.animationFps * config_.actions[slot(currentAction_)].speed;
            if (loop)
                frame_ = std::fmod(frame_, static_cast<float>(count));
            else
                frame_ = std::min(frame_, static_cast<float>(count - 1));
            if (blending_)
            {
                blendElapsed_ += dt;
                const float amount = smooth(blendElapsed_ / config_.blendSeconds);
                UpdateModelAnimationEx(model_, snapshot_, 0, animations_[current_], frame_, amount);
                if (amount >= 1.0f)
                    blending_ = false;
            }
            else
                UpdateModelAnimation(model_, animations_[current_], frame_);
        }

    private:
        Model &model_;
        ModelAnimation *animations_;
        int count_;
        const Config &config_;
        std::array<int, actionCount> clips_{};
        Action currentAction_ = Action::Idle;
        int current_ = -1;
        float frame_ = 0, elapsed_ = 0, blendElapsed_ = 0;
        bool blending_ = false;
        std::vector<Transform> snapshotPose_;
        ModelAnimPose snapshotPointer_ = nullptr;
        ModelAnimation snapshot_{};
    };

    void drawDemoPet(Action action, float time, float elapsed, float yaw, float tilt, float fade)
    {
        const float bounce = (action == Action::Move || action == Action::Drag) ? std::abs(std::sin(time * 10)) * 0.15f : action == Action::LeftClick ? std::sin(std::min(elapsed, 0.6f) * 14) * 0.18f
                                                                                          : action == Action::RightClick  ? std::abs(std::sin(elapsed * 13)) * 0.3f
                                                                                                                     : std::sin(time * 2.5f) * 0.04f;
        const float sway = action == Action::RightClick ? std::sin(elapsed * 14) * 18.0f : 0;
        const Color body = {128, 209, 226, static_cast<unsigned char>(255 * fade)};
        const Color belly = {237, 250, 248, static_cast<unsigned char>(255 * fade)};
        const Color dark = {34, 55, 74, static_cast<unsigned char>(255 * fade)};
        const Color pink = {255, 172, 190, static_cast<unsigned char>(255 * fade)};
        rlPushMatrix();
        rlRotatef(yaw + sway, 0, 1, 0);
        rlRotatef(tilt, 1, 0, 0);
        rlTranslatef(0, bounce, 0);
        DrawSphereEx({0, 1.12f, 0}, 0.78f, 20, 28, body);
        DrawSphereEx({0, 0.68f, 0.52f}, 0.44f, 16, 20, belly);
        DrawSphereEx({-0.48f, 1.79f, 0}, 0.33f, 12, 16, body);
        DrawSphereEx({0.48f, 1.79f, 0}, 0.33f, 12, 16, body);
        DrawSphereEx({-0.48f, 1.79f, 0.19f}, 0.18f, 12, 16, pink);
        DrawSphereEx({0.48f, 1.79f, 0.19f}, 0.18f, 12, 16, pink);
        DrawSphereEx({-0.26f, 1.29f, 0.67f}, 0.12f, 12, 16, dark);
        DrawSphereEx({0.26f, 1.29f, 0.67f}, 0.12f, 12, 16, dark);
        DrawSphereEx({0, 1.05f, 0.75f}, 0.10f, 12, 16, pink);
        DrawSphereEx({-0.38f, 0.29f, 0.22f}, 0.22f, 12, 16, body);
        DrawSphereEx({0.38f, 0.29f, 0.22f}, 0.22f, 12, 16, body);
        rlPopMatrix();
    }

} // namespace

int main(int argc, char **argv)
{
    fs::path configPath;
    bool listAnimations = false, smokeTest = false, configExplicit = false, runDirectly = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
        {
            configPath = argv[++i];
            configExplicit = true;
        }
        else if (arg == "--list-animations")
            listAnimations = true;
        else if (arg == "--smoke-test")
            smokeTest = true;
        else if (arg == "--run")
            runDirectly = true;
        else if (arg == "--help")
        {
            std::cout << "crose_pet [--config profile.json] [--run] [--list-animations] [--smoke-test]\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 2;
        }
    }
    Config config;
    std::string error;
    if (!configExplicit)
    {
        const fs::path executableDirectory = fs::absolute(argv[0]).parent_path();
        const std::array<fs::path, 8> candidates = {
            "pet-profile.json", "pet.json", fs::path("config") / "pet.json", "pet.ini", fs::path("config") / "pet.ini",
            executableDirectory / "pet-profile.json", executableDirectory / "pet.json", executableDirectory / "pet.ini"};
        for (const auto &candidate : candidates)
            if (fs::exists(candidate))
            {
                configPath = candidate;
                break;
            }
    }
    if (!configPath.empty() && !crose::loadConfig(fs::absolute(configPath), config, error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    if (configPath.empty())
    {
        const fs::path bundledModel = fs::current_path() / fs::u8path("团子.glb");
        if (fs::exists(bundledModel))
            config.model = bundledModel;
    }

    SetLoadFileDataCallback(loadUtf8FileData);
    SetLoadFileTextCallback(loadUtf8FileText);
    if (!listAnimations && !smokeTest && !runDirectly && !crose::runConfigurator(config))
        return 0;
    if (!config.model.empty() && !fs::exists(config.model))
    {
        std::cerr << "Model file not found: " << config.model << '\n';
        return 2;
    }
    unsigned int flags = FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TRANSPARENT | FLAG_VSYNC_HINT;
#if !defined(__linux__)
    // On X11, an MSAA framebuffer may select a 24-bit visual without alpha.
    flags |= FLAG_MSAA_4X_HINT;
#endif
    if (config.alwaysOnTop)
        flags |= FLAG_WINDOW_TOPMOST;
    if (listAnimations || smokeTest)
        flags |= FLAG_WINDOW_HIDDEN;
    if (listAnimations || smokeTest)
        SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(flags);
    InitWindow(config.width, config.height, "Crose 3D Pet");
    SetExitKey(KEY_NULL);
    SetTargetFPS(config.fps);
    if (!listAnimations)
    {
        const int monitor = GetCurrentMonitor();
        const Vector2 monitorPosition = GetMonitorPosition(monitor);
        const int x = config.x >= 0 ? config.x : static_cast<int>(monitorPosition.x) + GetMonitorWidth(monitor) - config.width - 60;
        const int y = config.y >= 0 ? config.y : static_cast<int>(monitorPosition.y) + GetMonitorHeight(monitor) - config.height - 70;
        SetWindowPosition(x, y);
    }

    Model model{};
    ModelAnimation *animations = nullptr;
    int animationCount = 0;
    float modelScale = 1.0f;
    std::vector<crose::ModelComponent> components;
    if (!config.model.empty())
    {
        const auto path = config.model.u8string();
        model = LoadModel(path.c_str());
        if (!IsModelValid(model))
        {
            std::cerr << "Could not load model: " << path << '\n';
            CloseWindow();
            return 3;
        }
        components = crose::inspectModelComponents(path, model.materialCount);
        const BoundingBox bounds = GetModelBoundingBox(model);
        const float modelHeight = std::max(0.001f, bounds.max.y - bounds.min.y);
        modelScale = config.modelHeight / modelHeight;
        const float centerX = (bounds.min.x + bounds.max.x) * 0.5f;
        const float centerZ = (bounds.min.z + bounds.max.z) * 0.5f;
        model.transform = MatrixMultiply(model.transform, MatrixTranslate(-centerX, -bounds.min.y, -centerZ));
        animations = LoadModelAnimations(path.c_str(), &animationCount);
    }
    if (listAnimations)
    {
        for (int i = 0; i < animationCount; ++i)
            std::cout << '#' << i << "  " << animations[i].name << "  (" << animations[i].keyframeCount << " frames)\n";
        if (animationCount == 0)
            std::cout << "No animation clips found.\n";
        if (animations)
            UnloadModelAnimations(animations, animationCount);
        if (IsModelValid(model))
            UnloadModel(model);
        CloseWindow();
        return 0;
    }

    AnimationPlayer player(model, animations, animationCount, config);
    for (int i = 0; i < actionCount; ++i)
    {
        if (animations && player.clip(static_cast<Action>(i)) < 0)
            std::cerr << "No matching clip for action " << crose::actionKey(static_cast<Action>(i))
                      << ": " << config.actions[i].animation << '\n';
    }
    CroseTray *tray = smokeTest ? nullptr : crose_tray_create(GetWindowHandle());
    if (!smokeTest && !tray) std::cerr << "System tray is unavailable; using the normal window.\n";
    RenderTexture2D trayTexture{};
    if (tray) trayTexture = LoadRenderTexture(128, 128);
    void *nativeWindow = GetWindowHandle();
    int cursorX = 0, cursorY = 0;
    const bool perPixelHitTest = crose_cursor_in_window(nativeWindow, &cursorX, &cursorY) >= 0;
    RenderTexture2D hitTexture{};
    if (perPixelHitTest && !smokeTest) hitTexture = LoadRenderTexture(config.width, config.height);
    std::vector<unsigned char> hitAlpha(static_cast<std::size_t>(config.width) * config.height, 0);
    bool hitMaskReady = false, mousePassthrough = false;
    float hitMaskClock = 0.2f;
    bool petHidden = false;
    float trayIconClock = 0.2f;
    Action action = player.clip(Action::Startup) >= 0 ? Action::Startup : Action::Idle;
    float stateTime = 0, worldTime = 0, wanderClock = 0;
    float walkRemaining = 0, direction = -1;
    float windowX = GetWindowPosition().x;
    float yaw = 0, pitch = config.cameraElevation;
    // 拖拽/自动移动时的朝向偏转，yaw 为水平角度，pitch 为俯仰角度。
    float turnYaw = 0, turnPitch = 0;
    float targetTurnYaw = 0, targetTurnPitch = 0;
    float cameraDistance = config.cameraDistance;
    float targetCameraDistance = config.cameraDistance;
    int smokeStage = 0;
    bool leftDown = false, rightDown = false, leftDragging = false, rightDragging = false;
    Vector2 leftOrigin{}, rightOrigin{}, windowOrigin{};
    std::mt19937 random(std::random_device{}());
    std::uniform_int_distribution<int> coin(0, 1);
    auto setAction = [&](Action next)
    {
        if (action == Action::Exit || (action == next && next != Action::LeftClick && next != Action::RightClick))
            return;
        action = next;
        stateTime = 0;
        player.play(next);
    };

    while (true)
    {
        const float dt = std::min(GetFrameTime(), 0.05f);
        worldTime += dt;
        stateTime += dt;
        if (smokeTest)
        {
            if (smokeStage == 0 && worldTime >= 0.4f)
            {
                setAction(Action::Move);
                smokeStage++;
            }
            else if (smokeStage == 1 && worldTime >= 0.8f)
            {
                setAction(Action::LeftClick);
                smokeStage++;
            }
            else if (smokeStage == 2 && worldTime >= 1.2f)
            {
                setAction(Action::RightClick);
                smokeStage++;
            }
            else if (smokeStage == 3 && worldTime >= 1.6f)
            {
                setAction(Action::Exit);
                smokeStage++;
            }
            if (worldTime >= 3.0f)
                break;
        }
        const bool shouldClose = WindowShouldClose();
        if ((shouldClose || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_Q)) && action != Action::Exit)
        {
            setAction(Action::Exit);
            walkRemaining = 0;
        }
        const int trayRequests = crose_tray_take_requests(tray);
        if (trayRequests & CROSE_TRAY_TOGGLE)
        {
            petHidden = !petHidden;
            if (petHidden) SetWindowState(FLAG_WINDOW_HIDDEN);
            else { ClearWindowState(FLAG_WINDOW_HIDDEN); SetWindowFocused(); }
        }
        if (trayRequests & CROSE_TRAY_EXIT)
        {
            setAction(Action::Exit);
            walkRemaining = 0;
        }
        if (action == Action::Exit && stateTime >= std::max(0.5f, player.duration(Action::Exit)))
            break;

        const int cursorInside = crose_cursor_in_window(nativeWindow, &cursorX, &cursorY);
        if (hitTexture.id && hitMaskReady && cursorInside >= 0)
        {
            bool onPet = false;
            if (cursorInside == 1 && !petHidden)
            {
                for (int dy = -2; dy <= 2 && !onPet; ++dy)
                {
                    const int y = cursorY + dy;
                    if (y < 0 || y >= config.height) continue;
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int x = cursorX + dx;
                        if (x >= 0 && x < config.width &&
                            hitAlpha[static_cast<std::size_t>(y) * config.width + x] > 24)
                        { onPet = true; break; }
                    }
                }
            }
            const bool passThrough = !onPet && !leftDown && !rightDown;
            if (passThrough != mousePassthrough)
            {
                if (passThrough) SetWindowState(FLAG_WINDOW_MOUSE_PASSTHROUGH);
                else ClearWindowState(FLAG_WINDOW_MOUSE_PASSTHROUGH);
                mousePassthrough = passThrough;
            }
        }

        Vector2 mouse = GetMousePosition();
        Vector2 window = GetWindowPosition();
        Vector2 globalMouse = {mouse.x + window.x, mouse.y + window.y};
        targetTurnYaw = 0;
        targetTurnPitch = 0;
        if (action != Action::Exit)
        {
            const float wheel = GetMouseWheelMove();
            if (wheel != 0.0f && mouse.x >= 0 && mouse.y >= 0 &&
                mouse.x < config.width && mouse.y < config.height)
            {
                targetCameraDistance = std::clamp(targetCameraDistance * std::pow(0.88f, wheel),
                                                  config.cameraDistance * 0.45f,
                                                  config.cameraDistance * 2.5f);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                leftDown = true;
                leftDragging = false;
                leftOrigin = globalMouse;
                windowOrigin = window;
            }
            if (leftDown && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            {
                const Vector2 delta = Vector2Subtract(globalMouse, leftOrigin);
                if (!leftDragging && Vector2Length(delta) > 6)
                {
                    leftDragging = true;
                    setAction(Action::Drag);
                    walkRemaining = 0;
                }
                if (leftDragging)
                {
                    windowX = windowOrigin.x + delta.x;
                    SetWindowPosition(static_cast<int>(std::round(windowX)), static_cast<int>(windowOrigin.y + delta.y));
                    // 按拖拽偏移量让整个模型转向拖拽方向。
                    targetTurnYaw = std::clamp(delta.x * 0.5f, -90.0f, 90.0f);
                    targetTurnPitch = std::clamp(delta.y * 0.35f, -40.0f, 40.0f);
                }
            }
            if (leftDown && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            {
                if (!leftDragging)
                {
                    setAction(Action::LeftClick);
                    walkRemaining = 0;
                }
                else
                    setAction(Action::Idle);
                leftDown = false;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
            {
                rightDown = true;
                rightDragging = false;
                rightOrigin = globalMouse;
            }
            if (rightDown && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            {
                if (Vector2Distance(globalMouse, rightOrigin) > 5)
                    rightDragging = true;
                if (rightDragging)
                {
                    const Vector2 delta = GetMouseDelta();
                    yaw += delta.x * 0.65f;
                    pitch = std::clamp(pitch - delta.y * 0.35f, -25.0f, 65.0f);
                    walkRemaining = 0;
                    if (action == Action::Move)
                        setAction(Action::Idle);
                }
            }
            if (rightDown && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
            {
                if (!rightDragging)
                {
                    setAction(Action::RightClick);
                    walkRemaining = 0;
                }
                rightDown = false;
            }
        }

        const bool interacting = leftDown || rightDown;
        if (action == Action::Idle && config.autoWalk && !interacting && !smokeTest)
        {
            wanderClock += dt;
            if (wanderClock >= config.walkInterval)
            {
                wanderClock = 0;
                walkRemaining = config.walkDuration;
                direction = coin(random) ? 1.0f : -1.0f;
                setAction(Action::Move);
            }
        }
        if (action == Action::Move && !interacting && !smokeTest)
        {
            walkRemaining -= dt;
            const Vector2 current = GetWindowPosition();
            const int monitor = GetCurrentMonitor();
            const float left = static_cast<float>(GetMonitorPosition(monitor).x);
            const float right = left + GetMonitorWidth(monitor) - config.width;
            windowX += direction * config.walkSpeed * dt;
            if (windowX < left || windowX > right)
            {
                direction = -direction;
                windowX = std::clamp(windowX, left, right);
            }
            SetWindowPosition(static_cast<int>(std::round(windowX)), static_cast<int>(current.y));
            // 自动走动时面向移动方向。
            targetTurnYaw = direction > 0 ? 90.0f : -90.0f;
            if (walkRemaining <= 0)
                setAction(Action::Idle);
        }
        if (action == Action::Startup && (player.clip(action) < 0 || player.finished()))
            setAction(Action::Idle);
        if ((action == Action::LeftClick || action == Action::RightClick) &&
            (player.clip(action) < 0 ? stateTime >= 0.65f : player.finished()))
            setAction(Action::Idle);
        player.update(dt, config.actions[slot(action)].loop);

        const float turnLerp = 1.0f - std::exp(-12.0f * dt);
        turnYaw += (targetTurnYaw - turnYaw) * turnLerp;
        turnPitch += (targetTurnPitch - turnPitch) * turnLerp;

        cameraDistance += (targetCameraDistance - cameraDistance) * (1.0f - std::exp(-12.0f * dt));

        const float elevation = pitch * DEG2RAD;
        Camera3D camera{};
        camera.target = {0, config.cameraTargetY >= 0 ? config.cameraTargetY : config.modelHeight * 0.50f, 0};
        camera.position = {0, camera.target.y + std::sin(elevation) * cameraDistance,
                           std::cos(elevation) * cameraDistance};
        camera.up = {0, 1, 0};
        camera.fovy = 35;
        camera.projection = CAMERA_PERSPECTIVE;
        const float fade = action == Action::Exit ? 1.0f - smooth(stateTime / std::max(0.5f, player.duration(Action::Exit))) : 1.0f;
        float turnAngle = 0;
        const Vector3 turnAxis = facingAxis(yaw + turnYaw, turnPitch, turnAngle);
        hitMaskClock += dt;
        if (hitTexture.id && hitMaskClock >= 0.08f)
        {
            hitMaskClock = 0;
            BeginTextureMode(hitTexture);
            ClearBackground(BLANK);
            BeginMode3D(camera);
            if (IsModelValid(model))
                crose::drawConfiguredModel(model, components, config.actions[slot(action)],
                                           {0, 0, 0}, turnAxis, turnAngle,
                                           {modelScale, modelScale, modelScale}, Fade(WHITE, fade));
            else drawDemoPet(action, worldTime, stateTime, yaw + turnYaw, turnPitch, fade);
            EndMode3D();
            EndTextureMode();
            Image hitImage = LoadImageFromTexture(hitTexture.texture);
            if (IsImageValid(hitImage))
            {
                ImageFlipVertical(&hitImage);
                ImageFormat(&hitImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                const auto *rgba = static_cast<const unsigned char *>(hitImage.data);
                for (std::size_t i = 0; i < hitAlpha.size(); ++i) hitAlpha[i] = rgba[4*i + 3];
                hitMaskReady = true;
                UnloadImage(hitImage);
            }
        }
        trayIconClock += dt;
        if (tray && trayTexture.id && trayIconClock >= 0.125f)
        {
            trayIconClock = 0;
            Camera3D iconCamera = camera;
            const float iconDistance = config.cameraDistance * 0.67f;
            iconCamera.position = {0, iconCamera.target.y + std::sin(elevation) * iconDistance,
                                   std::cos(elevation) * iconDistance};
            BeginTextureMode(trayTexture);
            ClearBackground(BLANK);
            BeginMode3D(iconCamera);
            if (IsModelValid(model))
                crose::drawConfiguredModel(model, components, config.actions[slot(action)],
                                           {0, 0, 0}, {0, 1, 0}, 0,
                                           {modelScale, modelScale, modelScale}, Fade(WHITE, fade));
            else drawDemoPet(action, worldTime, stateTime, 0, 0, fade);
            EndMode3D();
            EndTextureMode();
            Image iconImage = LoadImageFromTexture(trayTexture.texture);
            if (IsImageValid(iconImage))
            {
                ImageFlipVertical(&iconImage);
                ImageResize(&iconImage, 64, 64);
                ImageFormat(&iconImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                crose_tray_update_icon(tray, static_cast<const unsigned char *>(iconImage.data),
                                       iconImage.width, iconImage.height);
                UnloadImage(iconImage);
            }
        }
        BeginDrawing();
        ClearBackground(BLANK);
        BeginMode3D(camera);
        if (IsModelValid(model))
        {
            const float reaction = player.clip(action) < 0 && (action == Action::LeftClick || action == Action::RightClick)
                                       ? std::sin(stateTime * 18) * 0.08f * (1.0f - smooth(stateTime / 0.65f))
                                       : 0;
            crose::drawConfiguredModel(model, components, config.actions[slot(action)],
                                       {0, reaction, 0}, turnAxis, turnAngle,
                                       {modelScale, modelScale, modelScale}, Fade(WHITE, fade));
        }
        else
            drawDemoPet(action, worldTime, stateTime, yaw + turnYaw, turnPitch, fade);
        EndMode3D();
        EndDrawing();
    }
    if (animations)
        UnloadModelAnimations(animations, animationCount);
    if (IsModelValid(model))
        UnloadModel(model);
    if (hitTexture.id) UnloadRenderTexture(hitTexture);
    if (trayTexture.id) UnloadRenderTexture(trayTexture);
    crose_tray_destroy(tray);
    CloseWindow();
    return 0;
}
