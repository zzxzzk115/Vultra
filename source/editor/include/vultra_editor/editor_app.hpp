#pragma once

#include <vultra/core/base/common_context.hpp>
#include <vultra/core/rhi/graphics_pipeline.hpp>
#include <vultra/core/rhi/vertex_buffer.hpp>
#include <vultra/function/app/imgui_app.hpp>

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        class EditorApp final : public ImGuiApp
        {
        public:
            explicit EditorApp(const std::span<char*>& args) :
                ImGuiApp(args, {.title = "Vultra Editor", .width = 1280, .height = 720})
            {
                VULTRA_CLIENT_TRACE("ArgCount: {}", args.size());
                for (uint32_t i = 0; i < args.size(); ++i)
                {
                    VULTRA_CLIENT_TRACE("Arg[{}]: {}", i, args[i]);
                }
            }

            void onImGui() override
            {
                ImGui::Begin("Example Window");
                ImGui::Text("Hello, world!");
#ifdef VULTRA_ENABLE_RENDERDOC
                ImGui::Button("Capture One Frame");
                if (ImGui::IsItemClicked())
                {
                    m_WantCaptureFrame = true;
                }
#endif
                ImGui::End();
            }

            void onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt) override
            {
                // const auto& [frameIndex, target] = rtv;
                ImGuiApp::onRender(cb, rtv, dt);
            }
        };
    } // namespace editor
} // namespace vultra