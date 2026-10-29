#ifndef CROSE_MODEL_RENDER_H
#define CROSE_MODEL_RENDER_H

#include "pet_config.h"
#include "raylib.h"

#include <string>
#include <vector>

namespace crose
{
    struct ModelComponent
    {
        int materialIndex = 0;
        std::string name;
        bool defaultVisible = true;
    };

    std::vector<ModelComponent> inspectModelComponents(const std::string &modelPath, int materialCount);
    bool componentVisible(const ActionConfig &action, const std::vector<ModelComponent> &components, int materialIndex);
    void drawConfiguredModel(Model &model, const std::vector<ModelComponent> &components,
                             const ActionConfig &action, Vector3 position, Vector3 rotationAxis,
                             float rotationAngle, Vector3 scale, Color tint);
}

#endif
