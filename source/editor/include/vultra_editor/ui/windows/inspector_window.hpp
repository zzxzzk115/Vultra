#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class InspectorWindow final : public UIWindow
        {
        public:
            InspectorWindow();
            ~InspectorWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra