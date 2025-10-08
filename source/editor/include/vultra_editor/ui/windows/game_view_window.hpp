#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class GameViewWindow final : public UIWindow
        {
        public:
            GameViewWindow();
            ~GameViewWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra