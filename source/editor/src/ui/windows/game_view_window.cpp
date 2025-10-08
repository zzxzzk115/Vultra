#include "vultra_editor/ui/windows/game_view_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        GameViewWindow::GameViewWindow() : UIWindow("Game View") {}

        GameViewWindow::~GameViewWindow() = default;

        void GameViewWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the game view window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra