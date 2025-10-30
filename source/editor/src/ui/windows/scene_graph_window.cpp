#include "vultra_editor/ui/windows/scene_graph_window.hpp"
#include "vultra_editor/selector.hpp"

#include <vultra/function/scenegraph/logic_scene.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        SceneGraphWindow::SceneGraphWindow() : UIWindow("Scene Graph")
        {
            m_RenamePopupWidget.setRenameCallback([this](const char* newName) {
                if (m_RenamingEntity)
                {
                    m_RenamingEntity.setName(newName);
                }
            });
        }

        SceneGraphWindow::~SceneGraphWindow() = default;

        void SceneGraphWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());

            if (m_LogicScene)
            {
                // --- F2 rename shortcut ---
                if (m_SelectedEntity && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                    ImGui::IsKeyPressed(ImGuiKey_F2))
                {
                    m_RenamingEntity = m_SelectedEntity;
                    m_RenamePopupWidget.open(m_SelectedEntity.getName().c_str());
                }

                if (ImGui::BeginTable("scene_graph_table",
                                      2,
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX |
                                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
                {
                    // Setup columns
                    ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();

                    // Draw all root entities
                    for (auto& root : m_LogicScene->getRootEntities())
                        drawEntityNode(root);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::InvisibleButton("scene_empty_space",
                                           ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
                    // Detect right-click only if it's on this InvisibleButton
                    bool rightClickedOnDropZone =
                        ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                        {
                            // Reparent to root
                            uint32_t draggedID     = *static_cast<const uint32_t*>(payload->Data);
                            Entity   draggedEntity = {static_cast<entt::entity>(draggedID), m_LogicScene};
                            draggedEntity.setParent(CoreUUID());
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // --- Handle right-click context menu on empty space ---
                    if (rightClickedOnDropZone)
                    {
                        ImGui::OpenPopup("scene_graph_context");
                    }

                    if (ImGui::BeginPopup("scene_graph_context"))
                    {
                        if (ImGui::MenuItem("Create Empty Entity"))
                        {
                            auto entity = m_LogicScene->createEntity("New Entity");
                            entity.addComponent<TransformComponent>();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::EndTable();

                    // --- Process pending deletions ---
                    if (!m_PendingDeleteEntities.empty())
                    {
                        for (auto& e : m_PendingDeleteEntities)
                        {
                            m_LogicScene->destroyEntity(e);
                        }
                        m_PendingDeleteEntities.clear();
                    }
                }

                m_RenamePopupWidget.onImGui("Rename Entity");
            }
            else
            {
                ImGui::Text("No LogicScene bound.");
            }

            ImGui::End();
        }

        void SceneGraphWindow::drawEntityNode(Entity& entity)
        {
            // Skip drawing editor camera
            if (entity.hasComponent<CameraComponent>())
            {
                auto& cameraComp = entity.getComponent<CameraComponent>();
                if (cameraComp.isEditorCamera)
                    return;
            }

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
                selectEntity(entity);
            }

            // --- Drag source ---
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("ENTITY_NODE", &id, sizeof(uint32_t));
                ImGui::Text("%s", entity.getName().c_str());
                ImGui::EndDragDropSource();
            }

            // --- Right-click context menu on entity ---
            if (ImGui::BeginPopupContextItem("entity_context"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    m_RenamingEntity = entity;
                    m_RenamePopupWidget.open(entity.getName().c_str());
                }
                if (ImGui::MenuItem("Delete Entity"))
                {
                    m_PendingDeleteEntities.push_back(entity);
                }
                ImGui::EndPopup();
            }

            // --- Drop target ---
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE"))
                {
                    uint32_t draggedID     = *static_cast<const uint32_t*>(payload->Data);
                    Entity   draggedEntity = {static_cast<entt::entity>(draggedID), m_LogicScene};
                    draggedEntity.setParent(entity.getCoreUUID());
                }
                ImGui::EndDragDropTarget();
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

        void SceneGraphWindow::selectEntity(Entity& entity)
        {
            m_SelectedEntity = entity;

            // Single selection for now
            Selector::unselectAll(SelectionCategory::eEntity);
            Selector::select(SelectionCategory::eEntity, entity.getCoreUUID());
        }
    } // namespace editor
} // namespace vultra