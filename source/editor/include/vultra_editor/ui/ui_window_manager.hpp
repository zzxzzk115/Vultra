#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <memory>
#include <vector>

namespace vultra
{
    namespace editor
    {
        class UIWindowManager
        {
        public:
            template<typename T, typename... Args>
            T* registerWindow(Args&&... args)
            {
                auto win = std::make_unique<T>(std::forward<Args>(args)...);
                T*   ptr = win.get();
                m_Windows.push_back(std::move(win));
                return ptr;
            }

            void onInit()
            {
                for (auto& w : m_Windows)
                    w->onInit();
            }

            void onDestroy()
            {
                for (auto& w : m_Windows)
                    w->onDestroy();
            }

            void onPreUpdate()
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onPreUpdate();
                }
            }

            void onUpdate(const fsec dt)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onUpdate(dt);
                }
            }

            void onPhysicsUpdate(const fsec dt)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onPhysicsUpdate(dt);
                }
            }

            void onPostUpdate(const fsec dt)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onPostUpdate(dt);
                }
            }

            void onPreRender(UIWindowRenderContext& ctx)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onPreRender(ctx);
                }
            }

            void onRender(UIWindowRenderContext& ctx)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onRender(ctx);
                }
            }

            void onPostRender(UIWindowRenderContext& ctx)
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onPostRender(ctx);
                }
            }

            void onImGui()
            {
                for (auto& w : m_Windows)
                {
                    if (w->m_IsOpen)
                        w->onImGui();
                }
            }

            UIWindow* find(const std::string& name)
            {
                for (auto& w : m_Windows)
                    if (w->m_Name == name)
                        return w.get();
                return nullptr;
            }

            [[nodiscard]] const std::vector<std::unique_ptr<UIWindow>>& getWindows() const { return m_Windows; }

        private:
            std::vector<std::unique_ptr<UIWindow>> m_Windows;
        };
    } // namespace editor
} // namespace vultra