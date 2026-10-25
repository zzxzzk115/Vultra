#include <vultra/api/native_extension.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/main/project_types.hpp>

#include <stdexcept>

namespace vultra
{
    ProjectTypes::ProjectTypes(const ProjectManifest& project, const AssetSource& source)
    {
        m_Extensions.reserve(project.extensions.size());
        try
        {
            for (const auto id : project.extensions)
            {
                const auto& path = project.asset(id).path;
                try
                {
                    m_Extensions.push_back(
                        std::make_unique<NativeExtension>(project.materializeModule(source, path), m_Types));
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error("Load project type module " + id.value.toString() + " (" +
                                             path.generic_string() + "): " + error.what());
                }
            }
        }
        catch (...)
        {
            clearExtensions();
            throw;
        }
    }

    ProjectTypes::~ProjectTypes()
    {
        clearExtensions();
    }

    void ProjectTypes::clearExtensions()
    {
        // Later modules may derive earlier types; vector destruction alone does not guarantee reverse order.
        while (!m_Extensions.empty())
        {
            m_Extensions.pop_back();
        }
    }

    const SceneTypeCatalog& ProjectTypes::types() const
    {
        return m_Types;
    }
} // namespace vultra
