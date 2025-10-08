#pragma once

#include <vultra/core/base/base.hpp>

#include <string>

namespace vultra
{
    namespace editor
    {
        struct UIWindowRenderContext
        {};

        class UIWindow
        {
        public:
            friend class UIWindowManager;

            UIWindow(const std::string& name) : m_Name(name) {}
            virtual ~UIWindow() = default;

            virtual void onInit() {}
            virtual void onDestroy() {}

            virtual void onPreUpdate() {}
            virtual void onUpdate(const fsec) {}
            virtual void onPhysicsUpdate(const fsec) {}
            virtual void onPostUpdate(const fsec) {}

            virtual void onPreRender(UIWindowRenderContext&) {}
            virtual void onRender(UIWindowRenderContext&) {};
            virtual void onPostRender(UIWindowRenderContext&) {}

            virtual void onImGui() {}

            [[nodiscard]] const std::string& getName() const { return m_Name; }
            [[nodiscard]] bool&              isOpen() { return m_IsOpen; }

        protected:
            std::string m_Name;
            bool        m_IsOpen {true};
        };
    } // namespace editor
} // namespace vultra