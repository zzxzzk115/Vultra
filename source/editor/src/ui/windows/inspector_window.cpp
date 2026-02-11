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

            if (entity.hasComponent<CameraComponent>())
            {
                drawComponentCamera(entity.getComponent<CameraComponent>());
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

        void InspectorWindow::drawComponentCamera(CameraComponent& comp)
        {
            if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            ImGui::Indent();

            // Clear Flags
            int clearFlags = static_cast<int>(comp.clearFlags);
            if (ImGui::Combo("Clear Flags", &clearFlags, "Color\0Skybox\0"))
            {
                comp.clearFlags = static_cast<CameraClearFlags>(clearFlags);
            }

            // Projection
            int projection = static_cast<int>(comp.projection);
            if (ImGui::Combo("Projection", &projection, "Perspective\0Orthographic\0"))
            {
                comp.projection = static_cast<CameraProjection>(projection);
            }

            // Clear Color
            ImGui::ColorEdit4("Clear Color", glm::value_ptr(comp.clearColor));

            // Viewport Size
            ImGui::InputInt("Viewport Width", reinterpret_cast<int*>(&comp.viewPortWidth));
            ImGui::InputInt("Viewport Height", reinterpret_cast<int*>(&comp.viewPortHeight));

            // FOV
            ImGui::InputFloat("Field of View", &comp.fov);

            // Near and Far Planes
            ImGui::InputFloat("Near Plane", &comp.zNear);
            ImGui::InputFloat("Far Plane", &comp.zFar);

            // Primary and Editor Camera Flags
            ImGui::Checkbox("Primary Camera", &comp.isPrimary);
            ImGui::Checkbox("Editor Camera", &comp.isEditorCamera);

            // Optional Environment Map Path
            ImGui::InputText("Environment Map Path", comp.environmentMapPath.data(), comp.environmentMapPath.size());

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