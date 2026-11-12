#pragma once

#include <vultra/app/camera/orbit_camera.hpp>
#include <vultra/app/imgui_app.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/host/runtime.hpp>
#include <vultra/platform/os/file_watcher.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vultra::editor
{
    struct Options
    {
        std::filesystem::path project;
        uint64_t              frames = 0;
        std::filesystem::path capture;
        uint32_t              viewports = 1; // Viewports open at the start.
    };

    // One view of the project: a camera of its own and the image that camera rendered. A viewport is a
    // dockable window; the project is recorded once per open viewport, each recording a view of the frame.
    struct Viewport
    {
        uint32_t               number = 1;
        std::string            title;
        bool                   open = true;
        OrbitCamera            camera;
        std::optional<Texture> image;      // Display image of the session's size; created before first use.
        Extent                 content {}; // Size of the window's content area in the last GUI frame.
        bool                   hovered = false;
    };

    // The editor: a dock space with viewports, pass parameters, resources and a console around one project.
    // It drives the same host runtime as the command line, so a project loads, reloads and fails the same way.
    class EditorApp final : public ImGuiApp
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<EditorApp>, Error> create(Services               services,
                                                                                     const host::Registry&  registry,
                                                                                     const host::Workspace& workspace,
                                                                                     const Options&         options,
                                                                                     const AssetSource&     shaders);
        ~EditorApp() override;

    private:
        EditorApp(Services                       services,
                  const host::Registry&          registry,
                  const host::Workspace&         workspace,
                  const Options&                 options,
                  std::unique_ptr<host::Runtime> runtime,
                  std::unique_ptr<Frame>         frame);

        std::expected<void, Error> onUpdate(float deltaSeconds) override;
        std::expected<void, Error> onImGui() override;
        std::expected<void, Error> onRender(VriCommandBuffer* cmd, Texture& target) override;
        std::expected<void, Error> onPostRender(Texture& target) override;

        void                       log(const std::string& text);
        void                       describe(const host::Workspace& workspace);
        std::expected<void, Error> reload();

        // Viewports. Their images are created, resized and destroyed between completed frames.
        void                       addViewport();
        std::expected<void, Error> releaseImage(Viewport& viewport);
        std::expected<void, Error> syncViewports();
        std::expected<void, Error> renderViewports();

        // The GUI of one frame.
        void                       menuBar();
        void                       defaultLayout();
        std::expected<void, Error> viewportWindow(Viewport& viewport);
        std::expected<void, Error> parameterWindow();
        void                       resourceWindow();
        void                       consoleWindow();

        struct ResourceRow
        {
            std::string name;
            std::string format;
            std::string semantic;
            std::string provider;
        };

        std::filesystem::path m_Location;
        std::filesystem::path m_Base;
        host::Mode            m_Mode;
        Options               m_Options;
        const host::Registry& m_Registry;
        // Destroyed in reverse order, before the window, device and GUI of the base class: the viewports'
        // images, then the frame that rendered them, then the runtime.
        std::unique_ptr<host::Runtime> m_Runtime;
        std::unique_ptr<Frame>         m_Frame;
        std::vector<Viewport>          m_Viewports;
        uint32_t                       m_NextViewport = 1;
        // The viewport whose window size the project is rendered at: the one the mouse was over last.
        uint32_t m_SizingViewport = 1;
        // Where a new viewport is docked: beside the first one.
        ImGuiID m_ViewportDock = 0;

        FileWatcher              m_Watcher;
        bool                     m_ResetLayout    = false;
        bool                     m_ShowParameters = true;
        bool                     m_ShowResources  = true;
        bool                     m_ShowConsole    = true;
        std::string              m_RenderPath;
        std::vector<ResourceRow> m_Resources;
        std::vector<std::string> m_Console;
        size_t                   m_ShownLines = 0;
    };
} // namespace vultra::editor
