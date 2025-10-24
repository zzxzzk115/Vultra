#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <filesystem>

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

        private:
            void drawDirectoryRecursive(const std::filesystem::path& dirPath);
            void drawRightPanel();

        private:
            std::filesystem::path m_AssetRoot;
            std::filesystem::path m_CurrentDir;

            char  m_FilterBuffer[128] {0};
            float m_LeftPanelRatio {0.3f};
            bool  m_FocusToCurrent {false};
        };
    } // namespace editor
} // namespace vultra