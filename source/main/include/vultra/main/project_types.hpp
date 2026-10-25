#pragma once

#include <vultra/scene/scene_types.hpp>

#include <memory>
#include <vector>

namespace vultra
{
    class AssetSource;
    class NativeExtension;
    class ProjectManifest;

    // Project-owned type records/code. Keep this owner and its AssetSource alive until all worlds retire.
    class ProjectTypes
    {
    public:
        ProjectTypes(const ProjectManifest& project, const AssetSource& source);
        ~ProjectTypes();
        ProjectTypes(const ProjectTypes&)            = delete;
        ProjectTypes& operator=(const ProjectTypes&) = delete;

        const SceneTypeCatalog& types() const;

    private:
        void clearExtensions();

        SceneTypeCatalog                              m_Types;
        std::vector<std::unique_ptr<NativeExtension>> m_Extensions;
    };
} // namespace vultra
