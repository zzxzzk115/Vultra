#include "vultra/function/app/xr_app.hpp"

namespace vultra
{
    XRApp::XRApp(std::span<char*> args, const AppConfig& appConfig, const XRConfig& xrConfig) :
        ImGuiApp(args, appConfig, xrConfig.imguiConfig, xrConfig.clearColor), m_Headset(*m_RenderDevice)
    {}

    bool XRApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt)
    {
        uint32_t                                  swapchainImageIndex;
        const openxr::XRHeadset::BeginFrameResult frameResult = m_Headset.beginFrame(swapchainImageIndex);

        if (frameResult == openxr::XRHeadset::BeginFrameResult::eNormal)
        {
            auto& xrRenderTargetView = m_Headset.getSwapchainStereoRenderTargetView(swapchainImageIndex);

            {
                ZoneScopedN("XRApp::onXrRender");
                onXrRender(cb, xrRenderTargetView, dt);
            }
        }
        else if (frameResult == openxr::XRHeadset::BeginFrameResult::eError || m_Headset.isExitRequested())
        {
            m_IsRunning = false;
            return false;
        }
        else if (frameResult == openxr::XRHeadset::BeginFrameResult::eSkipAll)
        {
            return false;
        }
        m_Headset.endFrame();

        {
            ZoneScopedN("XRApp::onNormalRender");
            onNormalRender(cb, rtv, dt);
        }

        return ImGuiApp::onRender(cb, rtv, dt);
    }
} // namespace vultra