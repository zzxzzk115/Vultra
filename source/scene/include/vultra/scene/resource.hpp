#pragma once

#include <vultra/assets/imported_asset.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/core/base/api_annotations.hpp>
#include <vultra/scene/object.hpp>
#include <vultra/scene/resource_id.hpp>
#include <vultra/scene/scene_changes.hpp>

#include <string_view>

namespace vultra
{
    // ResourceId identifies authored mutable data; AssetId records its optional external source.
    // ObjectId identifies only this live instance in a world.
    class VULTRA_REFLECT_RESOURCE Resource : public Object
    {
    public:
        explicit Resource(ResourceId id = {StableId::generate()}, AssetId source = {});
        ~Resource() override = default;

        ResourceId       resourceId() const;
        AssetId          sourceAsset() const;
        std::string_view typeName() const override;

    protected:
        void markChanged(SceneChange change);

    private:
        friend class SceneTree;
        ResourceId    m_ResourceId;
        AssetId       m_SourceAsset;
        SceneChanges* m_Changes = nullptr;
    };

    class VULTRA_REFLECT_RESOURCE ModelResource final : public Resource
    {
    public:
        ModelResource(AssetId asset, ImportedAsset data, ResourceId id = {StableId::generate()});
        const ImportedAsset& data() const;
        std::string_view     typeName() const override;

    private:
        ImportedAsset m_Data;
    };
} // namespace vultra
