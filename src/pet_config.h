#ifndef CROSE_PET_CONFIG_H
#define CROSE_PET_CONFIG_H

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace crose
{
    namespace fs = std::filesystem;

    enum class Action : int
    {
        Startup,
        Idle,
        Drag,
        Move,
        LeftClick,
        RightClick,
        Exit,
        Count
    };

    constexpr int actionCount = static_cast<int>(Action::Count);
    constexpr int slot(Action action) { return static_cast<int>(action); }

    const char *actionKey(Action action);
    const char *actionLabel(Action action);

    struct ActionConfig
    {
        std::string animation;
        bool loop = false;
        float speed = 1.0f;
        bool useModelComponents = true;
        std::vector<std::string> visibleComponents;
    };

    struct Config
    {
        std::string name = "My 3D Pet";
        fs::path model;
        fs::path profilePath;
        int width = 360, height = 400, x = -1, y = -1, fps = 60;
        bool alwaysOnTop = true, autoWalk = true;
        float modelHeight = 2.2f, cameraDistance = 6.2f, cameraElevation = 18.0f, cameraTargetY = -1.0f;
        float animationFps = 60.0f, blendSeconds = 0.20f;
        float walkSpeed = 24.0f, walkInterval = 5.0f, walkDuration = 1.6f;
        std::array<ActionConfig, actionCount> actions;

        Config();
    };

    bool loadConfig(const fs::path &path, Config &config, std::string &error);
    bool saveConfigJson(const fs::path &path, Config &config, std::string &error);
    bool validateConfig(const Config &config, std::string &error);
}

#endif
