#include <vultra/scene/render_components.hpp>
#include <vultra/scene/scene_mesh_instance.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <array>
#include <map>

namespace vultra
{
    bool sceneMeshTopologyMatches(const SceneTree& tree, std::span<const SceneMeshInstance> instances)
    {
        size_t                                                      count = 0;
        std::map<std::array<uint8_t, 16>, const SceneMeshInstance*> imported;
        for (const auto& instance : instances)
        {
            if (!imported.emplace(instance.node.value.bytes, &instance).second)
            {
                return false;
            }
        }
        for (const auto& [handle, node, mesh] : tree.registry().view<const SceneNode, const SceneMesh>().each())
        {
            const auto found = imported.find(node.persistentId.value.bytes);
            if (found == imported.end() || found->second->model != mesh.model)
            {
                return false;
            }
            ++count;
        }
        return count == instances.size();
    }

} // namespace vultra
