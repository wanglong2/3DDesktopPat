#include "pet_config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace crose
{
    using json = nlohmann::json;

    namespace
    {
        std::string trim(std::string value)
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        fs::path resolvePath(const fs::path &profile, const fs::path &value)
        {
            return value.is_relative() ? (profile.parent_path() / value).lexically_normal() : value;
        }

        std::string portableModelPath(const fs::path &profile, const fs::path &model)
        {
            if (model.empty())
                return {};
            std::error_code ec;
            const fs::path relative = fs::relative(model, profile.parent_path(), ec);
            return (!ec && !relative.empty()) ? relative.generic_u8string() : model.generic_u8string();
        }

        bool loadJson(const fs::path &path, Config &config, std::string &error)
        {
            try
            {
                std::ifstream input(path);
                if (!input)
                    throw std::runtime_error("Cannot read profile");
                const json root = json::parse(input);
                config = Config{};
                config.profilePath = fs::absolute(path);
                config.name = root.value("name", config.name);
                const std::string model = root.value("model", std::string{});
                if (!model.empty())
                    config.model = resolvePath(config.profilePath, fs::u8path(model));

                const auto &window = root.value("window", json::object());
                config.width = window.value("width", config.width);
                config.height = window.value("height", config.height);
                config.x = window.value("x", config.x);
                config.y = window.value("y", config.y);
                config.fps = window.value("fps", config.fps);
                config.alwaysOnTop = window.value("always_on_top", config.alwaysOnTop);

                const auto &display = root.value("display", json::object());
                config.modelHeight = display.value("model_height", config.modelHeight);
                config.cameraDistance = display.value("camera_distance", config.cameraDistance);
                config.cameraElevation = display.value("camera_elevation", config.cameraElevation);
                config.cameraTargetY = display.value("camera_target_y", config.cameraTargetY);

                const auto &animation = root.value("animation", json::object());
                config.animationFps = animation.value("fps", config.animationFps);
                config.blendSeconds = animation.value("blend_seconds", config.blendSeconds);

                const auto &behavior = root.value("behavior", json::object());
                config.autoWalk = behavior.value("auto_walk", config.autoWalk);
                config.walkSpeed = behavior.value("walk_speed", config.walkSpeed);
                config.walkInterval = behavior.value("walk_interval", config.walkInterval);
                config.walkDuration = behavior.value("walk_duration", config.walkDuration);

                const auto actions = root.value("actions", json::object());
                for (int i = 0; i < actionCount; ++i)
                {
                    const Action action = static_cast<Action>(i);
                    const auto it = actions.find(actionKey(action));
                    if (it == actions.end() || !it->is_object())
                        continue;
                    auto &target = config.actions[i];
                    target.animation = it->value("animation", target.animation);
                    target.loop = it->value("loop", target.loop);
                    target.speed = it->value("speed", target.speed);
                    target.useModelComponents = it->value("use_model_components", target.useModelComponents);
                    if (it->contains("visible_components"))
                        target.visibleComponents = it->at("visible_components").get<std::vector<std::string>>();
                }
                return validateConfig(config, error);
            }
            catch (const std::exception &exception)
            {
                error = std::string("Invalid JSON profile: ") + exception.what();
                return false;
            }
        }

        bool loadLegacyIni(const fs::path &path, Config &config, std::string &error)
        {
            std::ifstream file(path);
            if (!file)
            {
                error = "Cannot read configuration: " + path.string();
                return false;
            }
            config = Config{};
            config.profilePath = fs::absolute(path);
            std::map<std::string, std::string> values;
            std::string line;
            while (std::getline(file, line))
            {
                const auto comment = line.find_first_of("#;");
                if (comment != std::string::npos && trim(line.substr(0, comment)).empty())
                    continue;
                const auto equals = line.find('=');
                if (equals != std::string::npos)
                    values[lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
            }
            auto get = [&](const char *key) -> std::optional<std::string>
            {
                const auto it = values.find(key);
                return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
            };
            auto number = [&](const char *key, auto &target) -> bool
            {
                const auto value = get(key);
                if (!value)
                    return true;
                try
                {
                    std::size_t used = 0;
                    if constexpr (std::is_integral_v<std::decay_t<decltype(target)>>)
                        target = std::stoi(*value, &used);
                    else
                        target = std::stof(*value, &used);
                    if (used != value->size())
                        throw std::invalid_argument("trailing characters");
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
                const auto value = get(key);
                if (!value)
                    return true;
                const std::string normalized = lower(*value);
                if (normalized == "true" || normalized == "1" || normalized == "yes")
                    target = true;
                else if (normalized == "false" || normalized == "0" || normalized == "no")
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
                !number("animation_fps", config.animationFps) || !number("blend_seconds", config.blendSeconds) ||
                !number("walk_speed", config.walkSpeed) || !number("walk_interval", config.walkInterval) ||
                !number("walk_duration", config.walkDuration) || !boolean("always_on_top", config.alwaysOnTop) ||
                !boolean("auto_walk", config.autoWalk))
                return false;

            if (const auto value = get("model"); value && !value->empty())
                config.model = resolvePath(config.profilePath, fs::u8path(*value));
            const std::array<std::pair<const char *, Action>, 5> mappings = {{
                {"idle", Action::Idle}, {"move", Action::Move}, {"left_click", Action::LeftClick},
                {"right_click", Action::RightClick}, {"exit", Action::Exit}}};
            for (const auto &[key, action] : mappings)
                if (const auto value = get(key))
                    config.actions[slot(action)].animation = *value;
            return validateConfig(config, error);
        }
    }

    Config::Config()
    {
        actions[slot(Action::Startup)] = {"Greeting|Spawn|Intro|Idle_Base", false, 1.0f, true, {}};
        actions[slot(Action::Idle)] = {"Idle_Base|Idle_Var2|Idle|Idle1", true, 1.0f, true, {}};
        actions[slot(Action::Drag)] = {"Into_Run|Run_Base|Walk|Move", true, 1.0f, true, {}};
        actions[slot(Action::Move)] = {"Run_Base|Run_Haste|Walk|Move|Run", true, 1.0f, true, {}};
        actions[slot(Action::LeftClick)] = {"Interact|Joke|Laugh|Taunt", false, 1.0f, true, {}};
        actions[slot(Action::RightClick)] = {"Celebrate|Dance_In|Dance|Cheer|Emote", false, 1.0f, true, {}};
        actions[slot(Action::Exit)] = {"Death|Exit|Recall", false, 1.0f, true, {}};
    }

    const char *actionKey(Action action)
    {
        static constexpr std::array<const char *, actionCount> keys = {
            "startup", "idle", "drag", "move", "left_click", "right_click", "exit"};
        return keys[slot(action)];
    }

    const char *actionLabel(Action action)
    {
        static constexpr std::array<const char *, actionCount> labels = {
            "Startup", "Idle", "Dragging", "Auto move", "Left click", "Right click", "Exit"};
        return labels[slot(action)];
    }

    bool validateConfig(const Config &config, std::string &error)
    {
        if (config.width < 160 || config.height < 160 || config.width > 1600 || config.height > 1600 ||
            config.fps < 15 || config.fps > 240 || config.modelHeight <= 0 || config.cameraDistance <= 0 ||
            config.cameraTargetY < -1 || config.animationFps <= 0 || config.blendSeconds < 0 ||
            config.walkSpeed < 0 || config.walkInterval < 0 || config.walkDuration < 0)
        {
            error = "One or more settings are outside their valid range";
            return false;
        }
        for (const auto &action : config.actions)
        {
            if (!std::isfinite(action.speed) || action.speed <= 0 || action.speed > 8.0f)
            {
                error = "Animation speed must be between 0 and 8";
                return false;
            }
        }
        return true;
    }

    bool loadConfig(const fs::path &path, Config &config, std::string &error)
    {
        if (!fs::exists(path))
        {
            error = "Configuration file not found: " + path.string();
            return false;
        }
        return lower(path.extension().string()) == ".json" ? loadJson(path, config, error)
                                                            : loadLegacyIni(path, config, error);
    }

    bool saveConfigJson(const fs::path &path, Config &config, std::string &error)
    {
        if (!validateConfig(config, error))
            return false;
        try
        {
            const fs::path absolute = fs::absolute(path);
            json actions = json::object();
            for (int i = 0; i < actionCount; ++i)
            {
                const auto &source = config.actions[i];
                actions[actionKey(static_cast<Action>(i))] = {
                    {"animation", source.animation}, {"loop", source.loop}, {"speed", source.speed},
                    {"use_model_components", source.useModelComponents},
                    {"visible_components", source.visibleComponents}};
            }
            const json root = {
                {"schema_version", 1},
                {"name", config.name},
                {"model", portableModelPath(absolute, config.model)},
                {"window", {{"width", config.width}, {"height", config.height}, {"x", config.x},
                            {"y", config.y}, {"fps", config.fps}, {"always_on_top", config.alwaysOnTop}}},
                {"display", {{"model_height", config.modelHeight}, {"camera_distance", config.cameraDistance},
                             {"camera_elevation", config.cameraElevation}, {"camera_target_y", config.cameraTargetY}}},
                {"animation", {{"fps", config.animationFps}, {"blend_seconds", config.blendSeconds}}},
                {"behavior", {{"auto_walk", config.autoWalk}, {"walk_speed", config.walkSpeed},
                              {"walk_interval", config.walkInterval}, {"walk_duration", config.walkDuration}}},
                {"actions", actions}};
            std::ofstream output(absolute);
            if (!output)
                throw std::runtime_error("Cannot create profile file");
            output << root.dump(2) << '\n';
            config.profilePath = absolute;
            return true;
        }
        catch (const std::exception &exception)
        {
            error = std::string("Could not save JSON profile: ") + exception.what();
            return false;
        }
    }
}
