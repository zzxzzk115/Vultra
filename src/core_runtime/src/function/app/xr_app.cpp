#include "vultra/function/app/xr_app.hpp"
#include "vultra/function/service/services.hpp"

namespace vultra
{
    XRApp::XRApp(std::span<char*> args, const AppConfig& appConfig, const XRConfig& xrConfig) :
        ImGuiApp(args, appConfig, xrConfig.imguiConfig, xrConfig.clearColor), m_Headset(*m_RenderDevice)
    {}

    void XRApp::run()
    {
        FPSMonitor fpsMonitor {m_Window};

        const fsec targetFrameTime {1.0 / 60.0f};
        fsec       deltaTime {targetFrameTime};
        fsec       accumulator {0};

        uint64_t frameCounter = 0;

        // https://github.com/janhsimon/openxr-vulkan-example/blob/main/src/Main.cpp

        while (!m_Window.shouldClose() && !m_Headset.isExitRequested())
        {
            using clock           = std::chrono::high_resolution_clock;
            const auto beginTicks = clock::now();

            {
                ZoneScopedN("[App] PreUpdate");
                onPreUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PollEvents");
                m_Window.pollEvents();
            }
            if (!m_IsRunning)
                break;

            uint32_t                                  swapchainImageIndex;
            const openxr::XRHeadset::BeginFrameResult frameResult = m_Headset.beginFrame(swapchainImageIndex);
            if (frameResult == openxr::XRHeadset::BeginFrameResult::eError)
            {
                break;
            }
            else if (frameResult == openxr::XRHeadset::BeginFrameResult::eSkipAll)
            {
                continue;
            }

            {
                ZoneScopedN("[App] Update");
                onUpdate(deltaTime);
            }
            {
                ZoneScopedN("[App] PhysicsUpdate");
                accumulator += (deltaTime < targetFrameTime ? deltaTime : targetFrameTime);
                while (accumulator >= targetFrameTime)
                {
                    onPhysicsUpdate(targetFrameTime);
                    accumulator -= targetFrameTime;
                }
            }
            {
                ZoneScopedN("[App] PostUpdate");
                onPostUpdate(deltaTime);
            }

            // Renderdoc Capture Begin
            if (m_WantCaptureFrame)
            {
                if (!m_RenderDocAPI->isTargetControlConnected())
                {
                    m_RenderDocAPI->launchReplayUI();
                }

                m_RenderDocAPI->startFrameCapture();
                m_RenderDocAPI->setCaptureTitle(fmt::format("Vultra Frame#{}", frameCounter));
            }

            // Begin frame
            auto& cb = m_FrameController.beginFrame();

            // XR Rendering
            {
                auto& xrRenderTargetView = m_Headset.getSwapchainStereoRenderTargetView(swapchainImageIndex);
                {
                    ZoneScopedN("XRApp::onXrRender");
                    onXrRender(cb, xrRenderTargetView, deltaTime);
                }
            }

            bool acquiredNextFrame = false;
            if (m_Swapchain)
            {
                {
                    ZoneScopedN("[App] PreRender");
                    onPreRender();
                }

                // Normal Rendering (ImGui, Mirror View, etc.)
                {
                    ZoneScopedN("[App] Render");

                    acquiredNextFrame = m_FrameController.acquireNextFrame();

                    // Only render if we successfully acquired the next frame
                    if (acquiredNextFrame)
                    {
                        onRender(cb, m_FrameController.getCurrentTarget(), deltaTime);
                    }
                }
            }

            m_FrameController.endFrame();

            // Renderdoc Capture End
            if (m_WantCaptureFrame)
            {
                m_RenderDocAPI->endFrameCapture();
                m_RenderDocAPI->showReplayUI();
                m_WantCaptureFrame = false;
            }

            {
                ZoneScopedN("[App] PostRender");
                onPostRender();
            }

            // Only present if we successfully acquired the next frame
            if (acquiredNextFrame)
            {
                ZoneScopedN("[App] Present");
                m_FrameController.present();
                frameCounter++;
            }

            FrameMark;

            deltaTime = clock::now() - beginTicks;
            if (deltaTime > 1s)
                deltaTime = targetFrameTime;

            fpsMonitor.update(deltaTime);

            m_Headset.endFrame();
        }

        m_RenderDevice->waitIdle();
        service::Services::reset();
    }

    bool XRApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt)
    {
        {
            ZoneScopedN("XRApp::onNormalRender");
            onNormalRender(cb, rtv, dt);
        }

        return ImGuiApp::onRender(cb, rtv, dt);
    }
} // namespace vultra