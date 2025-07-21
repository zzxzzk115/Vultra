#include <vultra/core/base/common_context.hpp>
#include <vultra/core/os/window.hpp>

using namespace vultra;

int main()
{
    auto window = os::Window::Builder {}.setTitle("Empty Vultra Window").setExtent({1024, 768}).build();

    // Event callback
    window.on<os::GeneralWindowEvent>([](const os::GeneralWindowEvent& event, os::Window& wd) {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            // Press ESC to close the window
            if (event.internalEvent.key.key == SDLK_ESCAPE)
            {
                wd.close();
            }
        }
    });

    VULTRA_CLIENT_WARN("Press ESC to close the window");

    while (!window.shouldClose())
    {
        window.pollEvents();
    }

    return 0;
}