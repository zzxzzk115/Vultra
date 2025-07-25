#pragma once

#include <SDL3/SDL_events.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace os
    {
        class Window : public entt::emitter<Window>
        {
        public:
            Window()              = delete;
            Window(const Window&) = delete;
            Window(Window&&) noexcept;
            ~Window() override;

            Window& operator=(const Window&) = delete;
            Window& operator=(Window&&) noexcept;

            using Extent   = glm::ivec2;
            using Position = glm::ivec2;

            Window& setTitle(std::string_view title);
            Window& setExtent(Extent extent);
            Window& setPosition(Position position);
            Window& setCursorVisibility(bool cursorVisibility);
            Window& setResizable(bool resizable);
            Window& setFullscreen(bool fullscreen);

            [[nodiscard]] std::string_view getTitle() const;
            [[nodiscard]] Extent           getExtent() const;
            [[nodiscard]] Extent           getFrameBufferExtent() const;
            [[nodiscard]] Position         getPosition() const;
            [[nodiscard]] bool             getCursorVisibility() const;
            [[nodiscard]] bool             isResizable() const;
            [[nodiscard]] bool             isFullscreen() const;

            [[nodiscard]] bool shouldClose() const;
            [[nodiscard]] bool isMinimized() const;

            [[nodiscard]] SDL_Window* getSDL3WindowHandle() const;
            [[nodiscard]] void*       getOSWindowHandle() const;

            [[nodiscard]] vk::SurfaceKHR createVulkanSurface(vk::Instance instance) const;

            void pollEvents();
            void close();

            class Builder
            {
            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder operator=(const Builder&)     = delete;
                Builder operator=(Builder&&) noexcept = delete;

                Builder& setTitle(std::string_view title);
                Builder& setExtent(Extent extent);
                Builder& setPosition(Position position);
                Builder& setCursorVisibility(bool cursorVisibility);
                Builder& setResizable(bool resizable);
                Builder& setFullscreen(bool fullscreen);

                [[nodiscard]] Window build() const;

            private:
                std::string m_Title;
                Position    m_Position {};
                Extent      m_Extent {};
                bool        m_CursorVisibility {true};
                bool        m_Resizable {true};
                bool        m_Fullscreen {false};
            };

            using WindowEventType = uint32_t;
            using WindowEvent     = SDL_Event;

            struct GeneralWindowEvent
            {
                WindowEventType type;
                WindowEvent     internalEvent;
            };

        private:
            Window(std::string_view, Extent, Position, bool, bool, bool);

        private:
            std::string m_Title;
            Extent      m_Extent {}, m_FrameBufferExtent {};
            Position    m_Position {};
            bool        m_CursorVisibility {true};
            bool        m_Resizable {true};
            bool        m_Fullscreen {false};

            bool m_ShouldClose {false};
            bool m_IsMinimized {false};

            SDL_Window* m_SDL3WindowHandle {nullptr};
        };

        using GeneralWindowEvent = Window::GeneralWindowEvent;
    } // namespace os
} // namespace vultra