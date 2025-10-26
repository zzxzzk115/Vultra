#include "vultra_editor/ui/windows/scene_graph_window.hpp"

#include <vultra/function/scenegraph/logic_scene.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        SceneGraphWindow::SceneGraphWindow() : UIWindow("Scene Graph") {}

        SceneGraphWindow::~SceneGraphWindow() = default;

        void SceneGraphWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());

            if (m_LogicScene)
            {
                ImGui::TextDisabled("Scene: %s", m_LogicScene->getName().c_str());
                ImGui::Separator();

                // Begin one table for the whole scene
                if (ImGui::BeginTable("scene_graph_table",
                                      2,
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX |
                                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
                {
                    // Left column = flexible width, right column = fixed
                    ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                    ImGui::TableHeadersRow();

                    for (auto& root : m_LogicScene->getRootEntities())
                        drawEntityNode(root);

                    ImGui::EndTable();
                }
            }
            else
            {
                ImGui::Text("No LogicScene bound.");
            }

            ImGui::End();
        }

        void SceneGraphWindow::drawEntityNode(Entity& entity)
        {
            // Select icon based on components
            const char* icon = ICON_MDI_CUBE; // Default icon for generic entity
            if (entity.hasComponent<CameraComponent>() || entity.hasComponent<XrCameraComponent>())
                icon = ICON_MDI_CAMERA;
            else if (entity.hasComponent<DirectionalLightComponent>() || entity.hasComponent<PointLightComponent>() ||
                     entity.hasComponent<AreaLightComponent>())
                icon = ICON_MDI_LIGHTBULB_ON;

            // TODO: More icons for different components

            // Combine label (with icon)
            std::string nodeLabel = std::string(icon) + "  " + entity.getName();

            bool               hasChildren = entity.hasChildren();
            ImGuiTreeNodeFlags flags       = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            if (m_SelectedEntity == entity)
                flags |= ImGuiTreeNodeFlags_Selected;

            uint32_t id = entity;

            // Each entity = one table row
            ImGui::PushID(id);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool open = ImGui::TreeNodeEx((void*)(intptr_t)id, flags, "%s", nodeLabel.c_str());
            if (ImGui::IsItemClicked())
            {
                m_SelectedEntity = entity;
            }

            // --- Right column: Status buttons ---
            ImGui::TableNextColumn();

            auto&       entityFlagsComp = entity.getComponent<EntityFlagsComponent>();
            bool        visible         = (entityFlagsComp.flags & static_cast<uint32_t>(EntityFlags::eVisible)) != 0;
            std::string visibleBtnID =
                std::string(visible ? ICON_MDI_EYE : ICON_MDI_EYE_OFF) + "##visible" + std::to_string(id);

            // Align buttons horizontally
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
            if (ImGui::SmallButton(visibleBtnID.c_str()))
            {
                entityFlagsComp.flags ^= static_cast<uint32_t>(EntityFlags::eVisible);
            }

            // Draw children recursively
            if (open && hasChildren)
            {
                for (auto& child : entity.getChildrenEntities())
                    drawEntityNode(child);
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    } // namespace editor
} // namespace vultra