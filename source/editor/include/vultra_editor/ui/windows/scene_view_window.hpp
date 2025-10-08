#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class SceneViewWindow final : public UIWindow
        {
        public:
            SceneViewWindow();
            ~SceneViewWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra