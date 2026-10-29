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
#include "external/cgltf.h"
#include "system_tray.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

    // Khada stores default submesh visibility in glTF material extras. raylib
    // imports the geometry but does not interpret those site-specific fields.
    int hideInvisibleGlbParts(Model &model, const std::string &path)
    {
        int fileSize = 0;
        unsigned char *fileData = LoadFileData(path.c_str(), &fileSize);
        if (!fileData)
            return 0;
        cgltf_options options{};
        cgltf_data *gltf = nullptr;
        const cgltf_result parsed = cgltf_parse(&options, fileData, static_cast<cgltf_size>(fileSize), &gltf);
        UnloadFileData(fileData);
        if (parsed != cgltf_result_success || !gltf)
            return 0;

        std::vector<bool> visible(static_cast<std::size_t>(model.materialCount), true);
        static const std::regex hiddenPattern(R"("visible"\s*:\s*false)", std::regex::icase);
        for (cgltf_size i = 0; i < gltf->materials_count && i + 1 < visible.size(); ++i)
        {
            const char *extras = gltf->materials[i].extras.data;
            if (extras && std::regex_search(extras, hiddenPattern))
                visible[i + 1] = false;
        }
        cgltf_free(gltf);

        int kept = 0;
        int hidden = 0;
        const int originalCount = model.meshCount;
        for (int read = 0; read < originalCount; ++read)
        {
            const int material = model.meshMaterial[read];
            if (material >= 0 && material < static_cast<int>(visible.size()) && !visible[material])
            {
                UnloadMesh(model.meshes[read]);
                ++hidden;
            }
            else
            {
                if (kept != read)
                {
                    model.meshes[kept] = model.meshes[read];
                    model.meshMaterial[kept] = material;
                }
                ++kept;
            }
        }
        model.meshCount = kept;
        return hidden;
    }

    struct Config
    {
        fs::path model;
        int width = 360, height = 400, x = -1, y = -1, fps = 60;
        bool alwaysOnTop = true, autoWalk = true;
        float modelHeight = 2.2f, cameraDistance = 6.2f, cameraElevation = 18.0f, cameraTargetY = -1.0f;
        float animationFps = 60.0f, blendSeconds = 0.20f;
        float walkSpeed = 24.0f, walkInterval = 5.0f, walkDuration = 1.6f;
        std::array<std::string, 5> clips = {
            "Idle|Idle1|idle", "Walk|Move|Run", "Joke|Laugh|Taunt",
            "Dance|Cheer|Emote", "Death|Exit|Recall"};
    };

    enum class Action : int
    {
        Idle,
        Move,
        Left,
        Right,
        Exit
    };
    constexpr int slot(Action action) { return static_cast<int>(action); }

    bool loadConfig(const fs::path &path, Config &config, std::string &error)
    {
        if (!fs::exists(path))
        {
            error = "Configuration file not found: " + path.string();
            return false;
        }
        std::ifstream file(path);
        if (!file)
        {
            error = "Cannot read configuration: " + path.string();
            return false;
        }
        std::map<std::string, std::string> values;
        std::string line;
        while (std::getline(file, line))
        {
            const auto comment = line.find_first_of("#;");
            // #12 is a valid animation index, so comments only start at line beginning.
            if (comment != std::string::npos && trim(line.substr(0, comment)).empty())
                continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos)
                continue;
            values[lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
        }
        auto get = [&](const char *key) -> std::optional<std::string>
        {
            const auto it = values.find(key);
            if (it == values.end())
                return std::nullopt;
            return it->second;
        };
        auto number = [&](const char *key, auto &target) -> bool
        {
            auto value = get(key);
            if (!value)
                return true;
            try
            {
                std::size_t used = 0;
                if constexpr (std::is_integral_v<std::decay_t<decltype(target)>>)
                {
                    int parsed = std::stoi(*value, &used);
                    if (used != value->size())
                        throw std::invalid_argument("trailing characters");
                    target = parsed;
                }
                else
                {
                    float parsed = std::stof(*value, &used);
                    if (used != value->size() || !std::isfinite(parsed))
                        throw std::invalid_argument("invalid float");
                    target = parsed;
                }
                return true;
            }
            catch (const std::exception &)
            {
                error = std::string("Invalid value for ") + key + ": " + *value;
                return false;
            }
        };
        auto boolean = [&](const char *key, bool &target) -> bool
        {
            auto value = get(key);
            if (!value)
                return true;
            const auto v = lower(*value);
            if (v == "true" || v == "1" || v == "yes")
                target = true;
            else if (v == "false" || v == "0" || v == "no")
                target = false;
            else
            {
                error = std::string("Invalid value for ") + key + ": " + *value;
                return false;
            }
            return true;
        };
        if (!number("window_width", config.width) || !number("window_height", config.height) ||
            !number("window_x", config.x) || !number("window_y", config.y) || !number("fps", config.fps) ||
            !number("model_height", config.modelHeight) || !number("camera_distance", config.cameraDistance) ||
            !number("camera_elevation", config.cameraElevation) || !number("camera_target_y", config.cameraTargetY) ||
            !number("animation_fps", config.animationFps) ||
            !number("blend_seconds", config.blendSeconds) || !number("walk_speed", config.walkSpeed) ||
            !number("walk_interval", config.walkInterval) || !number("walk_duration", config.walkDuration) ||
            !boolean("always_on_top", config.alwaysOnTop) || !boolean("auto_walk", config.autoWalk))
            return false;
        if (auto value = get("model"); value && !value->empty())
        {
            config.model = fs::path(*value);
            if (config.model.is_relative())
                config.model = path.parent_path() / config.model;
        }
        constexpr std::array<const char *, 5> keys = {"idle", "move", "left_click", "right_click", "exit"};
        for (int i = 0; i < 5; ++i)
            if (auto value = get(keys[i]))
                config.clips[i] = *value;
        if (config.width < 160 || config.height < 160 || config.fps < 15 || config.fps > 240 ||
            config.modelHeight <= 0 || config.cameraDistance <= 0 || config.cameraTargetY < -1 || config.animationFps <= 0 ||
            config.blendSeconds < 0 || config.walkSpeed < 0 || config.walkInterval < 0 || config.walkDuration < 0)
        {
            error = "One or more configuration values are outside their valid range";
            return false;
        }
        return true;
    }

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

    class AnimationPlayer
    {
    public:
        AnimationPlayer(Model &model, ModelAnimation *animations, int count, const Config &config)
            : model_(model), animations_(animations), count_(count), config_(config)
        {
            for (int i = 0; i < 5; ++i)
                clips_[i] = resolveClip(config.clips[i], model_, animations_, count_);
            if (clips_[0] < 0 && count_ > 0)
            {
                for (int i = 0; i < count_; ++i)
                    if (IsModelAnimationValid(model_, animations_[i]))
                    {
                        clips_[0] = i;
                        break;
                    }
            }
            current_ = clips_[0];
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
            return index >= 0 ? animations_[index].keyframeCount / config_.animationFps : 0.65f;
        }
        bool finished() const { return current_ < 0 ? elapsed_ >= 0.65f : frame_ >= animations_[current_].keyframeCount - 1; }

        void play(Action action)
        {
            const int next = clip(action);
            if (next == current_ && action != Action::Left && action != Action::Right && action != Action::Exit)
                return;
            if (next >= 0 && model_.currentPose && snapshot_.boneCount > 0)
            {
                std::copy_n(model_.currentPose, snapshot_.boneCount, snapshotPose_.begin());
                blending_ = current_ >= 0 && config_.blendSeconds > 0;
                blendElapsed_ = 0;
            }
            else
                blending_ = false;
            current_ = next;
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
            frame_ += dt * config_.animationFps;
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
        std::array<int, 5> clips_{};
        int current_ = -1;
        float frame_ = 0, elapsed_ = 0, blendElapsed_ = 0;
        bool blending_ = false;
        std::vector<Transform> snapshotPose_;
        ModelAnimPose snapshotPointer_ = nullptr;
        ModelAnimation snapshot_{};
    };

    void drawDemoPet(Action action, float time, float elapsed, float yaw, float fade)
    {
        const float bounce = action == Action::Move ? std::abs(std::sin(time * 10)) * 0.15f : action == Action::Left ? std::sin(std::min(elapsed, 0.6f) * 14) * 0.18f
                                                                                          : action == Action::Right  ? std::abs(std::sin(elapsed * 13)) * 0.3f
                                                                                                                     : std::sin(time * 2.5f) * 0.04f;
        const float sway = action == Action::Right ? std::sin(elapsed * 14) * 18.0f : 0;
        const Color body = {128, 209, 226, static_cast<unsigned char>(255 * fade)};
        const Color belly = {237, 250, 248, static_cast<unsigned char>(255 * fade)};
        const Color dark = {34, 55, 74, static_cast<unsigned char>(255 * fade)};
        const Color pink = {255, 172, 190, static_cast<unsigned char>(255 * fade)};
        rlPushMatrix();
        rlRotatef(yaw + sway, 0, 1, 0);
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
    fs::path configPath = "pet.ini";
    bool listAnimations = false, smokeTest = false, configExplicit = false;
    // 从参数判断应该执行什么操作
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
        else if (arg == "--help")
        {
            std::cout << "crose_pet [--config path/to/pet.ini] [--list-animations] [--smoke-test]\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 2;
        }
    }
    if (!configExplicit && !fs::exists(configPath))
    {
        const fs::path besideExecutable = fs::absolute(argv[0]).parent_path() / "pet.ini";
        if (fs::exists(besideExecutable))
            configPath = besideExecutable;
    }
    Config config;
    std::string error;
    if (!loadConfig(fs::absolute(configPath), config, error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    if (!config.model.empty() && !fs::exists(config.model))
    {
        std::cerr << "Model file not found: " << config.model << '\n';
        return 2;
    }
    unsigned int flags = FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TRANSPARENT | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT;
    if (config.alwaysOnTop)
        flags |= FLAG_WINDOW_TOPMOST;
    if (listAnimations || smokeTest)
        flags |= FLAG_WINDOW_HIDDEN;
    if (listAnimations || smokeTest)
        SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(flags);
    InitWindow(config.width, config.height, "Crose 3D Pet");
    SetLoadFileDataCallback(loadUtf8FileData);
    SetLoadFileTextCallback(loadUtf8FileText);
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
        const int hiddenParts = hideInvisibleGlbParts(model, path);
        if (hiddenParts > 0)
            std::cerr << "Hidden " << hiddenParts << " model parts marked invisible in glTF materials.\n";
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
    for (int i = 0; i < 5; ++i)
    {
        if (animations && player.clip(static_cast<Action>(i)) < 0)
            std::cerr << "No matching clip for action " << i << ": " << config.clips[i] << '\n';
    }
    CroseTray *tray = smokeTest ? nullptr : crose_tray_create(GetWindowHandle());
    if (!smokeTest && !tray) std::cerr << "System tray is unavailable; using the normal window.\n";
    RenderTexture2D trayTexture{};
    if (tray) trayTexture = LoadRenderTexture(128, 128);
    bool petHidden = false;
    float trayIconClock = 0.2f;
    Action action = Action::Idle;
    float stateTime = 0, worldTime = 0, wanderClock = 0;
    float walkRemaining = 0, direction = -1;
    float windowX = GetWindowPosition().x;
    float yaw = 0, pitch = config.cameraElevation;
    float cameraDistance = config.cameraDistance;
    float targetCameraDistance = config.cameraDistance;
    int smokeStage = 0;
    bool leftDown = false, rightDown = false, leftDragging = false, rightDragging = false;
    Vector2 leftOrigin{}, rightOrigin{}, windowOrigin{};
    std::mt19937 random(std::random_device{}());
    std::uniform_int_distribution<int> coin(0, 1);
    auto setAction = [&](Action next)
    {
        if (action == Action::Exit || (action == next && next != Action::Left && next != Action::Right))
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
                setAction(Action::Left);
                smokeStage++;
            }
            else if (smokeStage == 2 && worldTime >= 1.2f)
            {
                setAction(Action::Right);
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

        Vector2 mouse = GetMousePosition();
        Vector2 window = GetWindowPosition();
        Vector2 globalMouse = {mouse.x + window.x, mouse.y + window.y};
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
                if (Vector2Length(delta) > 6)
                    leftDragging = true;
                if (leftDragging)
                {
                    windowX = windowOrigin.x + delta.x;
                    SetWindowPosition(static_cast<int>(std::round(windowX)), static_cast<int>(windowOrigin.y + delta.y));
                }
            }
            if (leftDown && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            {
                if (!leftDragging)
                {
                    setAction(Action::Left);
                    walkRemaining = 0;
                }
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
                    setAction(Action::Right);
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
            if (walkRemaining <= 0)
                setAction(Action::Idle);
        }
        if ((action == Action::Left || action == Action::Right) &&
            (player.clip(action) < 0 ? stateTime >= 0.65f : player.finished()))
            setAction(Action::Idle);
        player.update(dt, action == Action::Idle || action == Action::Move);

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
                DrawModelEx(model, {0, 0, 0}, {0, 1, 0}, 0,
                            {modelScale, modelScale, modelScale}, Fade(WHITE, fade));
            else drawDemoPet(action, worldTime, stateTime, 0, fade);
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
            const float reaction = player.clip(action) < 0 && (action == Action::Left || action == Action::Right)
                                       ? std::sin(stateTime * 18) * 0.08f * (1.0f - smooth(stateTime / 0.65f))
                                       : 0;
            DrawModelEx(model, {0, reaction, 0}, {0, 1, 0}, yaw,
                        {modelScale, modelScale, modelScale}, Fade(WHITE, fade));
        }
        else
            drawDemoPet(action, worldTime, stateTime, yaw, fade);
        EndMode3D();
        EndDrawing();
    }
    if (animations)
        UnloadModelAnimations(animations, animationCount);
    if (IsModelValid(model))
        UnloadModel(model);
    if (trayTexture.id) UnloadRenderTexture(trayTexture);
    crose_tray_destroy(tray);
    CloseWindow();
    return 0;
}
