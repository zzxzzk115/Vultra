#pragma once

#include "vultra_editor/ui/ui_window_manager.hpp"

#include <vultra/function/app/imgui_app.hpp>
#include <vultra_engine/project/project.hpp>

#include <argparse/argparse.hpp>

namespace vultra
{
    namespace editor
    {
        class EditorApp final : public ImGuiApp
        {
        public:
            explicit EditorApp(const std::span<char*>& args);
            ~EditorApp() override;

            void onPreUpdate(const fsec dt) override;
            void onUpdate(const fsec dt) override;
            void onPhysicsUpdate(const fsec dt) override;
            void onPostUpdate(const fsec dt) override;

            void onPreRender() override;
            void onRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec dt) override;
            void onPostRender() override;

            void onImGui() override;

        private:
            void drawMainMenuBar();

        private:
            UIWindowManager          m_UIWindowManager;
            argparse::ArgumentParser m_ArgParser {"Vultra Editor"};
            engine::Project          m_CurrentProject {};
        };
    } // namespace editor
} // namespace vultra