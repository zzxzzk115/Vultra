#include "vultra_editor/ui/windows/asset_browser_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        AssetBrowserWindow::AssetBrowserWindow() : UIWindow("Asset Browser") {}

        AssetBrowserWindow::~AssetBrowserWindow() = default;

        void AssetBrowserWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the asset browser window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra