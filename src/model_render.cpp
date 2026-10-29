#include "model_render.h"

#include "external/cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "raymath.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "rlgl.h"

#include <algorithm>
#include <map>
#include <regex>

namespace crose
{
    std::vector<ModelComponent> inspectModelComponents(const std::string &modelPath, int materialCount)
    {
        std::vector<ModelComponent> result;
        if (materialCount > 0)
            result.push_back({0, "Default", true});

        int fileSize = 0;
        unsigned char *fileData = LoadFileData(modelPath.c_str(), &fileSize);
        if (!fileData)
            return result;
        cgltf_options options{};
        cgltf_data *gltf = nullptr;
        const cgltf_result parsed = cgltf_parse(&options, fileData, static_cast<cgltf_size>(fileSize), &gltf);
        UnloadFileData(fileData);
        if (parsed != cgltf_result_success || !gltf)
            return result;

        static const std::regex hiddenPattern(R"("visible"\s*:\s*false)", std::regex::icase);
        std::map<std::string, int> occurrences;
        const int count = std::min<int>(static_cast<int>(gltf->materials_count), std::max(0, materialCount - 1));
        for (int i = 0; i < count; ++i)
        {
            const cgltf_material &material = gltf->materials[i];
            std::string name = material.name && material.name[0] ? material.name : "Material " + std::to_string(i + 1);
            const int occurrence = ++occurrences[name];
            if (occurrence > 1)
                name += " #" + std::to_string(occurrence);
            const char *extras = material.extras.data;
            const bool visible = !(extras && std::regex_search(extras, hiddenPattern));
            result.push_back({i + 1, name, visible});
        }
        cgltf_free(gltf);

        for (int material = static_cast<int>(result.size()); material < materialCount; ++material)
            result.push_back({material, "Material " + std::to_string(material), true});
        return result;
    }

    bool componentVisible(const ActionConfig &action, const std::vector<ModelComponent> &components, int materialIndex)
    {
        const auto component = std::find_if(components.begin(), components.end(), [materialIndex](const ModelComponent &item)
                                            { return item.materialIndex == materialIndex; });
        if (component == components.end())
            return true;
        if (action.useModelComponents)
            return component->defaultVisible;
        return std::find(action.visibleComponents.begin(), action.visibleComponents.end(), component->name) !=
               action.visibleComponents.end();
    }

    void drawConfiguredModel(Model &model, const std::vector<ModelComponent> &components,
                             const ActionConfig &action, Vector3 position, Vector3 rotationAxis,
                             float rotationAngle, Vector3 scale, Color tint)
    {
        const Matrix original = model.transform;
        const Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
        const Matrix matRotation = MatrixRotate(rotationAxis, rotationAngle * DEG2RAD);
        const Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);
        const Matrix transform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
        model.transform = MatrixMultiply(model.transform, transform);

        for (int mesh = 0; mesh < model.meshCount; ++mesh)
        {
            const int material = model.meshMaterial[mesh];
            if (material >= 0 && material < model.materialCount && componentVisible(action, components, material))
            {
                Material tinted = model.materials[material];
                const Color diffuse = tinted.maps[MATERIAL_MAP_DIFFUSE].color;
                tinted.maps[MATERIAL_MAP_DIFFUSE].color = {
                    static_cast<unsigned char>(static_cast<int>(diffuse.r) * tint.r / 255),
                    static_cast<unsigned char>(static_cast<int>(diffuse.g) * tint.g / 255),
                    static_cast<unsigned char>(static_cast<int>(diffuse.b) * tint.b / 255),
                    static_cast<unsigned char>(static_cast<int>(diffuse.a) * tint.a / 255)};
                if (tinted.shader.locs && tinted.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS] != -1 && model.boneMatrices)
                {
                    rlEnableShader(tinted.shader.id);
                    rlSetUniformMatrices(tinted.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS],
                                         model.boneMatrices, model.skeleton.boneCount);
                }
                DrawMesh(model.meshes[mesh], tinted, model.transform);
            }
        }
        model.transform = original;
    }
}
