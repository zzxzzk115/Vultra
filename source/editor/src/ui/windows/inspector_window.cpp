#include "vultra_editor/ui/windows/inspector_window.hpp"
#include "vultra_editor/asset/asset_database.hpp"

#include <vultra/function/renderer/imgui_renderer.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        InspectorWindow::InspectorWindow() : UIWindow("Inspector") {}

        InspectorWindow::~InspectorWindow() = default;

        void InspectorWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            auto lastSelectionCategory = Selector::getLastSelectionCategory();
            auto lastSelectionUUID     = Selector::getLastSelectionUUID();

            if (lastSelectionCategory == SelectionCategory::eEntity)
            {
                if (m_LogicScene)
                {
                    Entity entity = m_LogicScene->getEntityWithCoreUUID(lastSelectionUUID);
                    if (entity)
                    {
                        drawEntityProperties(entity);
                    }
                }
            }
            else if (lastSelectionCategory == SelectionCategory::eAsset)
            {
                drawAssetProperties(lastSelectionUUID);
            }
            ImGui::End();
        }

        void InspectorWindow::drawEntityProperties(Entity& entity)
        {
            if (!entity)
                return;

            ImGui::Text("Entity Properties:");
            drawComponentName(entity.getComponent<NameComponent>());
            drawComponentFlags(entity.getComponent<EntityFlagsComponent>());

            if (entity.hasComponent<TransformComponent>())
            {
                drawComponentTransform(entity.getComponent<TransformComponent>());
            }
        }

        void InspectorWindow::drawComponentName(NameComponent& comp) { ImGui::Text("Name: %s", comp.name.c_str()); }

        void InspectorWindow::drawComponentFlags(EntityFlagsComponent& comp) { ImGui::Text("Flags: 0x%X", comp.flags); }

        void InspectorWindow::drawComponentTransform(TransformComponent& comp)
        {
            if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            ImGui::Indent();

            // Position
            ImGuiExt::DrawVec3Control("Position", comp.position);

            // Rotation
            glm::vec3 rotation = comp.getRotationEuler();
            ImGuiExt::DrawVec3Control("Rotation", rotation);
            comp.setRotationEuler(rotation);

            // Scale
            ImGuiExt::DrawVec3Control("Scale", comp.scale, 1.0f);

            ImGui::Unindent();
        }

        void InspectorWindow::drawAssetProperties(const CoreUUID& assetUUID)
        {
            auto* assetDB    = AssetDatabase::get();
            auto  assetEntry = assetDB->getRegistry().lookup(assetUUID);
            if (assetEntry.type == vasset::VAssetType::eTexture)
            {
                ImGui::Text("Texture Asset:");
                ImGui::Text("UUID: %s", assetUUID.toString().c_str());

                auto* texId      = AssetDatabase::get()->getImGuiTextureByUUID(assetUUID);
                auto  imguiTexId = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texId));

                if (imguiTexId)
                {
                    ImGui::Text("Preview:");
                    auto sizeAvail = ImGui::GetContentRegionAvail();
                    auto size      = std::min(sizeAvail.x, sizeAvail.y);
                    ImGui::Image(imguiTexId, ImVec2(size, size));
                }
                else
                {
                    ImGui::Text("Failed to load texture preview.");
                }
            }
            else if (assetEntry.type == vasset::VAssetType::eMaterial)
            {
                ImGui::Text("Material Asset:");
                ImGui::Text("UUID: %s", assetUUID.toString().c_str());

                // TODO: Material Sphere Preview
            }
            else if (assetEntry.type == vasset::VAssetType::eMesh)
            {
                ImGui::Text("Mesh Asset:");
                ImGui::Text("UUID: %s", assetUUID.toString().c_str());

                // TODO: Mesh Preview (Shaded / Wireframe)
            }
        }
    } // namespace editor
} // namespace vultra