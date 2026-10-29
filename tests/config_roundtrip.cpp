#include "pet_config.h"

#include <filesystem>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    crose::Config source;
    source.name = "Round trip pet";
    source.model = fs::temp_directory_path() / "models" / "pet.glb";
    source.actions[crose::slot(crose::Action::Drag)].animation = "Drag_Test";
    source.actions[crose::slot(crose::Action::Drag)].useModelComponents = false;
    source.actions[crose::slot(crose::Action::Drag)].visibleComponents = {"Body", "Eyes"};
    source.actions[crose::slot(crose::Action::Drag)].speed = 1.75f;

    const fs::path file = fs::temp_directory_path() / "crose-pet-config-roundtrip.json";
    std::string error;
    if (!crose::saveConfigJson(file, source, error))
    {
        std::cerr << error << '\n';
        return 1;
    }

    crose::Config loaded;
    if (!crose::loadConfig(file, loaded, error))
    {
        std::cerr << error << '\n';
        fs::remove(file);
        return 2;
    }
    fs::remove(file);

    const auto &drag = loaded.actions[crose::slot(crose::Action::Drag)];
    if (loaded.name != source.name || loaded.model.lexically_normal() != source.model.lexically_normal() ||
        drag.animation != "Drag_Test" || drag.useModelComponents || drag.visibleComponents.size() != 2 ||
        drag.speed != 1.75f)
    {
        std::cerr << "JSON profile did not survive a save/load round trip\n";
        return 3;
    }
    return 0;
}
