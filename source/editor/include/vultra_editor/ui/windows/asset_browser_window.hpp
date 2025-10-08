#pragma once

#include "vultra_editor/ui/ui_window.hpp"

namespace vultra
{
    namespace editor
    {
        class AssetBrowserWindow final : public UIWindow
        {
        public:
            AssetBrowserWindow();
            ~AssetBrowserWindow() override;

            void onImGui() override;
        };
    } // namespace editor
} // namespace vultra