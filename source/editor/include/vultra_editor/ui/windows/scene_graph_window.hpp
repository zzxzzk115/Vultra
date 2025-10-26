#pragma once

#include "vultra_editor/ui/ui_window.hpp"

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

            void bindLogicScene(LogicScene* scene) { m_LogicScene = scene; }

            void onImGui() override;

        private:
            void drawEntityNode(Entity& entity);

        private:
            LogicScene* m_LogicScene {nullptr};

            Entity m_SelectedEntity;
        };
    } // namespace editor
} // namespace vultra