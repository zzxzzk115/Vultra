#pragma once

#include <vultra/app/camera/orbit_camera.hpp>
#include <vultra/app/imgui_app.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/host/runtime.hpp>
#include <vultra/platform/os/file_watcher.hpp>

#include <spdlog/sinks/sink.h>

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vultra::editor
{
    struct Options
    {
        std::filesystem::path   project; // Empty starts without a project.
        host::Mode              mode   = host::Mode::eDebug;
        uint64_t                frames = 0;
        std::optional<uint32_t> viewports; // Overrides the viewports the project had open.
        std::filesystem::path   layout;    // The file the docking layout is kept in.
        std::filesystem::path   capture;
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
        glm::vec2              pointer {0}; // The mouse in pixels of the image, while hovered.
    };

    // The editor: a dock space with viewports, pass parameters, resources and a console around one project.
    // It drives the same host runtime as the command line, so a project loads, reloads and fails the same way.
    class EditorApp final : public ImGuiApp
    {
    public:
        // The project named by the options is opened when there is one; a project that cannot be opened is
        // reported in the console of an empty editor.
        [[nodiscard]] static std::expected<std::unique_ptr<EditorApp>, Error>
        create(Services services, const host::Registry& registry, const Options& options, const AssetSource& shaders);
        ~EditorApp() override;

        // After run(): saves the project's editor state and gives the process exit code.
        [[nodiscard]] std::expected<int, Error> finish();

    private:
        EditorApp(Services               services,
                  const host::Registry&  registry,
                  const Options&         options,
                  const AssetSource&     shaders,
                  std::unique_ptr<Frame> frame);

        std::expected<void, Error> onUpdate(float deltaSeconds) override;
        std::expected<void, Error> onImGui() override;
        std::expected<void, Error> onRender(VriCommandBuffer* cmd, Texture& target) override;
        std::expected<void, Error> onPostRender(Texture& target) override;

        void log(const std::string& text, bool problem = false);
        void describe(const host::Workspace& workspace);

        // Projects. All of these run between completed frames.
        std::expected<void, Error> openProject(const std::filesystem::path& location);
        std::expected<void, Error> reload();
        std::expected<void, Error> followChanges();
        bool                       nativeSource(const std::filesystem::path& file) const;
        std::filesystem::path      stateFile() const;
        std::expected<void, Error> saveState(bool restart) const;
        void                       restoreState();

        // Viewports. Their images are created, resized and destroyed between completed frames.
        Viewport&                  addViewport();
        std::expected<void, Error> releaseImage(Viewport& viewport);
        std::expected<void, Error> syncViewports();
        std::expected<void, Error> renderViewports();
        void                       forwardInput();

        // The GUI of one frame.
        void                       menuBar();
        void                       openProjectPopup();
        void                       defaultLayout();
        std::expected<void, Error> viewportWindow(Viewport& viewport);
        std::expected<void, Error> parameterWindow();
        void                       sceneWindow();
        void                       sceneNode(entt::entity entity);
        void                       inspectorWindow();
        void                       saveScene();
        void                       resourceWindow();
        void                       consoleWindow();

        struct ResourceRow
        {
            std::string name;
            std::string format;
            std::string semantic;
            std::string provider;
        };

        struct ConsoleLine
        {
            std::string text;
            bool        problem = false;
        };

        Options               m_Options;
        const host::Registry& m_Registry;
        const AssetSource&    m_Shaders;
        // The open project; empty and null without one.
        std::filesystem::path              m_Location;
        std::filesystem::path              m_Base;
        std::vector<std::filesystem::path> m_NativeDirectories;
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
        // What the project sees as its input: the window's keys and buttons with the mouse of the hovered
        // viewport, or nothing while no viewport is hovered or the camera is being moved.
        Input m_ProjectInput;

        FileWatcher m_Watcher;
        bool        m_Restart        = false;
        bool        m_ResetLayout    = false;
        bool        m_ShowParameters = true;
        bool        m_ShowResources  = true;
        bool        m_ShowConsole    = true;
        bool        m_ShowScene      = true;
        bool        m_ShowInspector  = true;
        // The entity the inspector shows, by its path: a reload keeps the selection.
        std::string m_Selected;
        // "Open project": the popup's text and the project to open before the next frame.
        bool                                 m_AskProject = false;
        std::array<char, 1024>               m_ProjectText {};
        std::optional<std::filesystem::path> m_PendingProject;

        std::string              m_RenderPath;
        std::vector<ResourceRow> m_Resources;
        std::vector<ConsoleLine> m_Console;
        std::string              m_LastProblem;
        size_t                   m_ShownLines = 0;
        // Lines the kernel logged, possibly on a worker thread, until the console takes them.
        std::shared_ptr<spdlog::sinks::sink> m_LogSink;
        std::mutex                           m_LogMutex;
        std::vector<ConsoleLine>             m_Logged;
    };
} // namespace vultra::editor
