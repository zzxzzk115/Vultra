#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/renderer/imgui_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>

namespace vultra
{
    class LogicScene;
    class Entity;

    namespace editor
    {
        class SceneGraphWindow final : public UIWindow
        {
        public:
            SceneGraphWindow();
            ~SceneGraphWindow() override;

            void onImGui() override;

        private:
            void drawEntityNode(Entity& entity);
            void selectEntity(Entity& entity);

        private:
            Entity              m_SelectedEntity;
            std::vector<Entity> m_PendingDeleteEntities;
            Entity              m_RenamingEntity;

            ImGuiExt::RenamePopupWidget m_RenamePopupWidget;
        };
    } // namespace editor
} // namespace vultra