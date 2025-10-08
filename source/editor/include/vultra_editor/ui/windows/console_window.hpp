#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class ConsoleWindow final : public UIWindow
        {
        public:
            ConsoleWindow();
            ~ConsoleWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra