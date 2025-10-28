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
            void selectPath(const std::filesystem::path& path);

        private:
            std::filesystem::path m_AssetRoot;
            std::filesystem::path m_CurrentDir;
            std::filesystem::path m_SelectedPath;

            char  m_FilterBuffer[128] {0};
            float m_LeftPanelRatio {0.3f};
            bool  m_FocusToCurrent {false};

            float m_IconSize {64.0f};
            float m_MinIconSize {24.0f};
            float m_MaxIconSize {128.0f};
            float m_ListThreshold {40.0f};
        };
    } // namespace editor
} // namespace vultra