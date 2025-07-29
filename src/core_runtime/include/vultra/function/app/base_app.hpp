#pragma once

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/logger.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/profiling/renderdoc_api.hpp"
#include "vultra/core/rhi/frame_controller.hpp"
#include "vultra/core/rhi/frame_index.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    struct AppConfig
    {
        std::string                      title {"Untitled Vultra Application"};
        uint32_t                         width {1024};
        uint32_t                         height {768};
        rhi::FrameIndex::ValueType       numFramesInFlight {2};
        rhi::RenderDeviceFeatureFlagBits renderDeviceFeatureFlag {rhi::RenderDeviceFeatureFlagBits::eNormal};
        Logger::Level                    logLevel {Logger::Level::eTrace};
        rhi::VerticalSync                vSyncConfig {rhi::VerticalSync::eAdaptive};
        rhi::Swapchain::Format           swapchainFormat {rhi::Swapchain::Format::eLinear};
    };

    class BaseApp
    {
    public:
        BaseApp(std::span<char*> args, const AppConfig& cfg);
        BaseApp(const BaseApp&)     = delete;
        BaseApp(BaseApp&&) noexcept = delete;
        virtual ~BaseApp()          = default;

        BaseApp& operator=(const BaseApp&)     = delete;
        BaseApp& operator=(BaseApp&&) noexcept = delete;

        [[nodiscard]] os::Window&        getWindow();
        [[nodiscard]] rhi::RenderDevice& getRenderDevice();
        [[nodiscard]] rhi::Swapchain&    getSwapchain();

        void run();
        void close();

    protected:
        void setupWindowCallbacks();

        virtual void onGeneralWindowEvent(const os::GeneralWindowEvent& event);
        virtual void onResize(uint32_t width, uint32_t height);
        virtual void onKeyPress(int key, int scancode, int mod);

        virtual void onPreUpdate(const fsec) {}
        virtual void onUpdate(const fsec) {}
        virtual void onPhysicsUpdate(const fsec) {}
        virtual void onPostUpdate(const fsec) {}

        virtual void onPreRender() {}
        virtual bool onRender(rhi::CommandBuffer&, const rhi::RenderTargetView, const fsec) { return true; }
        virtual void onPostRender() {}

    protected:
        bool m_IsRunning {true};
        bool m_WantCaptureFrame {false};

        std::unique_ptr<RenderDocAPI>      m_RenderDocAPI {nullptr};
        os::Window                         m_Window;
        std::unique_ptr<rhi::RenderDevice> m_RenderDevice {nullptr};
        rhi::Swapchain                     m_Swapchain;
        rhi::FrameController               m_FrameController;
    };
} // namespace vultra

#define CONFIG_MAIN(AppClass) \
    int main(int argc, char* argv[]) \
    { \
        try \
        { \
            AppClass app {std::span {argv, std::size_t(argc)}}; \
            app.run(); \
        } \
        catch (const std::exception& e) \
        { \
            VULTRA_CLIENT_CRITICAL(e.what()); \
            return -1; \
        } \
        return 0; \
    }
