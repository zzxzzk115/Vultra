#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class SceneGraphWindow final : public UIWindow
        {
        public:
            SceneGraphWindow();
            ~SceneGraphWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra