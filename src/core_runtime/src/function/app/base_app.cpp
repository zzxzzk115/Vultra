#include "vultra/function/app/base_app.hpp"

#include "vultra/function/service/services.hpp"

namespace vultra
{
    namespace
    {
        class FPSMonitor final
        {
        public:
            explicit FPSMonitor(os::Window& window) : m_Target {window}, m_OriginalTile {window.getTitle()} {}

            void update(const fsec dt)
            {
                ++m_NumFrames;
                m_Time += dt;

                if (m_Time >= 1s)
                {
                    m_Target.setTitle(std::format("{} | FPS = {}", m_OriginalTile, m_NumFrames));

                    m_Time      = 0s;
                    m_NumFrames = 0;
                }
            }

        private:
            os::Window&       m_Target;
            const std::string m_OriginalTile;

            uint32_t m_NumFrames {0};
            fsec     m_Time {0s};
        };
    } // namespace

    BaseApp::BaseApp(std::span<char*>, const AppConfig& cfg) :
        m_RenderDocAPI(std::make_unique<RenderDocAPI>()),
        m_Window(os::Window::Builder {}.setExtent({cfg.width, cfg.height}).build()),
        m_RenderDevice(std::make_unique<rhi::RenderDevice>(cfg.renderDeviceFeatureFlag))
    {
        m_Window.setTitle(std::format("{} ({})", cfg.title, m_RenderDevice->getName()));

        m_Swapchain       = m_RenderDevice->createSwapchain(m_Window, cfg.swapchainFormat, cfg.vSyncConfig);
        m_FrameController = rhi::FrameController {*m_RenderDevice, m_Swapchain, cfg.numFramesInFlight};

        setupWindowCallbacks();

        service::Services::init(*m_RenderDevice);
    }

    os::Window& BaseApp::getWindow() { return m_Window; }

    rhi::RenderDevice& BaseApp::getRenderDevice() { return *m_RenderDevice; }

    rhi::Swapchain& BaseApp::getSwapchain() { return m_Swapchain; }

    void BaseApp::run()
    {
        FPSMonitor fpsMonitor {m_Window};

        const fsec targetFrameTime {1.0 / 60.0f};
        fsec       deltaTime {targetFrameTime};
        fsec       accumulator {0};

        uint64_t frameCounter = 0;

        // https://gafferongames.com/post/fix_your_timestep/
        // http://gameprogrammingpatterns.com/game-loop.html
        // https://dewitters.com/dewitters-gameloop/
        // http://higherorderfun.com/blog/2010/08/17/understanding-the-game-main-loop/

        while (!m_Window.shouldClose())
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

            if (m_Swapchain)
            {
                bool ok = true;
                {
                    ZoneScopedN("[App] PreRender");
                    onPreRender();
                }
                {
                    ZoneScopedN("[App] Render");

                    if (m_WantCaptureFrame)
                    {
                        if (!m_RenderDocAPI->isTargetControlConnected())
                        {
                            m_RenderDocAPI->launchReplayUI();
                        }

                        m_RenderDocAPI->startFrameCapture();
                        m_RenderDocAPI->setCaptureTitle(fmt::format("Vultra Frame#{}", frameCounter));
                    }

                    auto [cb, valid] = m_FrameController.beginFrame();
                    if (!valid)
                    {
                        // If the command buffer is not valid, skip this frame.
                        continue;
                    }

                    ok = onRender(cb, m_FrameController.getCurrentTarget(), deltaTime);

                    m_FrameController.endFrame();

                    if (m_WantCaptureFrame)
                    {
                        m_RenderDocAPI->endFrameCapture();
                        m_RenderDocAPI->showReplayUI();
                        m_WantCaptureFrame = false;
                    }
                }
                {
                    ZoneScopedN("[App] PostRender");
                    onPostRender();
                }
                {
                    ZoneScopedN("[App] Present");
                    m_FrameController.present();
                    frameCounter++;
                }

                if (!ok)
                {
                    continue;
                }
            }
            FrameMark;

            deltaTime = clock::now() - beginTicks;
            if (deltaTime > 1s)
                deltaTime = targetFrameTime;

            fpsMonitor.update(deltaTime);
        }

        m_RenderDevice->waitIdle();
        service::Services::reset();
    }

    void BaseApp::close() { m_IsRunning = false; }

    void BaseApp::setupWindowCallbacks()
    {
        m_Window.on<os::GeneralWindowEvent>(
            [this](const os::GeneralWindowEvent& event, os::Window&) { onGeneralWindowEvent(event); });
    }

    void BaseApp::onGeneralWindowEvent(const os::GeneralWindowEvent& event)
    {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            onKeyPress(event.internalEvent.key.key, event.internalEvent.key.scancode, event.internalEvent.key.mod);
        }

        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            onResize(event.internalEvent.window.data1, event.internalEvent.window.data2);
        }
    }

    void BaseApp::onResize(uint32_t /*width*/, uint32_t /*height*/)
    {
        // Only recreate the swapchain for Wayland, as X11 handles it automatically
        if (m_Window.getDriverType() == os::Window::DriverType::eWayland)
        {
            m_Swapchain.recreate();
        }
    }

    void BaseApp::onKeyPress(int key, int scancode, int mod)
    {
        // TODO: InputSystem
    }
} // namespace vultra