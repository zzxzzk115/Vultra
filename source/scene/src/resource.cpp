#include <vultra/scene/resource.hpp>

#include <stdexcept>
#include <utility>

namespace vultra
{
    Resource::Resource(ResourceId id, AssetId source) :
        m_ResourceId(id),
        m_SourceAsset(source)
    {
        if (!id.value.valid())
        {
            throw std::invalid_argument("Resource requires a stable scene resource ID");
        }
    }

    ResourceId Resource::resourceId() const
    {
        return m_ResourceId;
    }

    AssetId Resource::sourceAsset() const
    {
        return m_SourceAsset;
    }

    std::string_view Resource::typeName() const
    {
        return "Resource";
    }

    void Resource::markChanged(SceneChange change)
    {
        if (m_Changes)
        {
            m_Changes->mark(change);
        }
    }

    ModelResource::ModelResource(AssetId asset, ImportedAsset data, ResourceId id) :
        Resource(id, asset),
        m_Data(std::move(data))
    {
    }

    const ImportedAsset& ModelResource::data() const
    {
        return m_Data;
    }

    std::string_view ModelResource::typeName() const
    {
        return "ModelResource";
    }
} // namespace vultra
