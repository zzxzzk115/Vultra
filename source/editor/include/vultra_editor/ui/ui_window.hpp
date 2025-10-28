#pragma once

#include <vultra/core/base/base.hpp>
#include <vultra/core/rhi/rendertarget_view.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>

#include <string>

namespace vultra
{
    class LogicScene;

    namespace editor
    {
        struct UIWindowRenderContext
        {
            rhi::CommandBuffer&          cb;
            gfx::BuiltinRenderer*        renderer {nullptr};
            const rhi::RenderTargetView& rtv;
            fsec                         dt {};
        };

        class UIWindow
        {
        public:
            friend class UIWindowManager;

            UIWindow(const std::string& name) : m_Name(name) {}
            virtual ~UIWindow() = default;

            virtual void onInit(rhi::RenderDevice& renderDevice) { m_RenderDevice = &renderDevice; }
            virtual void onDestroy() {}

            virtual void onPreUpdate() {}
            virtual void onUpdate(const fsec, LogicScene* logicScene) { m_LogicScene = logicScene; }
            virtual void onPhysicsUpdate(const fsec) {}
            virtual void onPostUpdate(const fsec) {}

            virtual void onPreRender() {}
            virtual void onRender(UIWindowRenderContext&) {};
            virtual void onPostRender() {}

            virtual void onImGui() {}

            [[nodiscard]] const std::string& getName() const { return m_Name; }
            [[nodiscard]] bool&              isOpen() { return m_IsOpen; }

        protected:
            std::string m_Name;
            bool        m_IsOpen {true};

            rhi::RenderDevice* m_RenderDevice {nullptr};

            LogicScene* m_LogicScene {nullptr};
        };
    } // namespace editor
} // namespace vultra