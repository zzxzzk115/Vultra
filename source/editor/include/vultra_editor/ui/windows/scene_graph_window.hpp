#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/scenegraph/entity.hpp>

#include <entt/entt.hpp>

namespace vultra
{
    class LogicScene;
    class Entity;

    namespace editor
    {
        class SceneGraphWindow final : public UIWindow, public entt::emitter<SceneGraphWindow>
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
            char                m_RenameBuffer[256] {0};
            bool                m_RequestRenamePopup {false};
        };
    } // namespace editor
} // namespace vultra