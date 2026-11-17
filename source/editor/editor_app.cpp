#include "editor.hpp"
#include "editor_app.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/host/shader_pass.hpp>
#include <vultra/platform/os/file.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/callback_sink.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace vultra::editor
{
    namespace
    {
        // Draws the control a pass parameter asks for and returns true with the new value when it was changed.
        bool parameterControl(const std::string& label, const host::SessionParameter& parameter, double& value)
        {
            if (parameter.control == PassControl::eCheckbox)
            {
                bool on = value != 0;
                if (!ImGui::Checkbox(label.c_str(), &on))
                {
                    return false;
                }
                value = on ? 1 : 0;
                return true;
            }
            if (parameter.control == PassControl::eChoice)
            {
                const auto current = std::ranges::find(parameter.choices, value, &PassChoice::value);
                bool       changed = false;
                if (ImGui::BeginCombo(label.c_str(), current == parameter.choices.end() ? "" : current->label.c_str()))
                {
                    for (const auto& choice : parameter.choices)
                    {
                        if (ImGui::Selectable(choice.label.c_str(), choice.value == value))
                        {
                            value   = choice.value;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }
            if (parameter.integer)
            {
                int whole = int(value);
                if (!ImGui::SliderInt(label.c_str(), &whole, int(parameter.minimum), int(parameter.maximum)))
                {
                    return false;
                }
                value = whole;
                return true;
            }
            float real = float(value);
            if (!ImGui::SliderFloat(label.c_str(), &real, float(parameter.minimum), float(parameter.maximum)))
            {
                return false;
            }
            value = real;
            return true;
        }

        // Controls for the fields present in `value`, labelled with their names; true when one changed. A field
        // without a default that the value does not have, such as the camera's `fov` when it has `vertical_fov`,
        // has no control. Files are shown relative to `base` and are not edited here.
        bool
        fieldControls(std::span<const host::FieldDesc> fields, nlohmann::json& value, const std::filesystem::path& base)
        {
            bool changed = false;
            for (const auto& field : fields)
            {
                const auto found = value.find(field.name);
                if (found == value.end())
                {
                    continue;
                }
                auto&       member  = *found;
                const auto* label   = field.name.c_str();
                const float minimum = field.minimum ? float(*field.minimum) : -FLT_MAX;
                const float maximum = field.maximum ? float(*field.maximum) : FLT_MAX;
                // A drag of the whole range takes about a thousand pixels; unbounded fields move by hundredths.
                const float speed = field.minimum && field.maximum ?
                                        std::max(float(*field.maximum - *field.minimum) / 1000, 1e-3f) :
                                        0.01f;
                switch (field.type)
                {
                    case host::FieldType::eFloat: {
                        float number = member.get<float>();
                        if (ImGui::DragFloat(label, &number, speed, minimum, maximum))
                        {
                            member  = number;
                            changed = true;
                        }
                        break;
                    }
                    case host::FieldType::eInt: {
                        int number = member.get<int>();
                        if (ImGui::DragInt(label,
                                           &number,
                                           std::max(speed, 0.1f),
                                           field.minimum ? int(*field.minimum) : INT_MIN,
                                           field.maximum ? int(*field.maximum) : INT_MAX))
                        {
                            member  = number;
                            changed = true;
                        }
                        break;
                    }
                    case host::FieldType::eBool: {
                        bool flag = member.get<bool>();
                        if (ImGui::Checkbox(label, &flag))
                        {
                            member  = flag;
                            changed = true;
                        }
                        break;
                    }
                    case host::FieldType::eVec2:
                    case host::FieldType::eVec3:
                    case host::FieldType::eVec4:
                    case host::FieldType::eColor: {
                        std::array<float, 4> parts {};
                        for (size_t index = 0; index < member.size(); ++index)
                        {
                            parts[index] = member[index].get<float>();
                        }
                        bool edited = false;
                        if (field.type == host::FieldType::eColor)
                        {
                            edited = ImGui::ColorEdit3(label, parts.data(), ImGuiColorEditFlags_Float);
                        }
                        else
                        {
                            edited = ImGui::DragScalarN(label,
                                                        ImGuiDataType_Float,
                                                        parts.data(),
                                                        int(member.size()),
                                                        speed,
                                                        &minimum,
                                                        &maximum);
                        }
                        if (edited)
                        {
                            for (size_t index = 0; index < member.size(); ++index)
                            {
                                member[index] = parts[index];
                            }
                            changed = true;
                        }
                        break;
                    }
                    case host::FieldType::eEnum: {
                        const auto current = member.get<std::string>();
                        if (ImGui::BeginCombo(label, current.c_str()))
                        {
                            for (const auto& option : field.options)
                            {
                                if (ImGui::Selectable(option.c_str(), option == current) && option != current)
                                {
                                    member  = option;
                                    changed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        break;
                    }
                    case host::FieldType::eString:
                    case host::FieldType::eEntity: {
                        std::array<char, 256> text {};
                        const auto            current = member.get<std::string>();
                        std::copy_n(current.begin(), std::min(current.size(), text.size() - 1), text.begin());
                        if (ImGui::InputText(label, text.data(), text.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            member  = std::string(text.data());
                            changed = true;
                        }
                        break;
                    }
                    case host::FieldType::eFile: {
                        const auto relative = std::filesystem::path(member.get<std::string>()).lexically_relative(base);
                        ImGui::LabelText(label, "%s", relative.generic_string().c_str());
                        break;
                    }
                    case host::FieldType::eGroup:
                        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            changed = fieldControls(field.fields, member, base) || changed;
                            ImGui::TreePop();
                        }
                        break;
                }
            }
            return changed;
        }

        constexpr const char* kParameterWindow = "Pass parameters";
        constexpr const char* kResourceWindow  = "Resources";
        constexpr const char* kConsoleWindow   = "Console";
        constexpr const char* kSceneWindow     = "Scene";
        constexpr const char* kInspectorWindow = "Inspector";
        constexpr const char* kProjectPopup    = "Open project";
        constexpr size_t      kConsoleLines    = 2000;

        std::string viewportTitle(uint32_t number)
        {
            return "Viewport " + std::to_string(number);
        }

        bool sameSize(Extent a, Extent b)
        {
            return a.width == b.width && a.height == b.height;
        }

        // The directory problems of a project that could not be read are reported relative to.
        std::filesystem::path baseOf(const std::filesystem::path& location)
        {
            std::error_code ignored;
            return std::filesystem::is_directory(location, ignored) ? location : location.parent_path();
        }

        nlohmann::json cameraState(const OrbitCamera& camera)
        {
            return {{"center", {camera.center.x, camera.center.y, camera.center.z}},
                    {"distance", camera.distance},
                    {"yaw", camera.yaw},
                    {"pitch", camera.pitch}};
        }

        // False, leaving the camera alone, when the state does not hold a camera.
        bool restoreCamera(const nlohmann::json& state, OrbitCamera& camera)
        {
            if (!state.is_object() || !state.contains("center") || !state.at("center").is_array() ||
                state.at("center").size() != 3 ||
                !std::ranges::all_of(state.at("center"), &nlohmann::json::is_number) ||
                !state.value("distance", nlohmann::json()).is_number() ||
                !state.value("yaw", nlohmann::json()).is_number() ||
                !state.value("pitch", nlohmann::json()).is_number())
            {
                return false;
            }
            const auto& center = state.at("center");
            camera.center      = {center[0].get<float>(), center[1].get<float>(), center[2].get<float>()};
            camera.distance    = state.at("distance").get<float>();
            camera.yaw         = state.at("yaw").get<float>();
            camera.pitch       = state.at("pitch").get<float>();
            return true;
        }
    } // namespace

    std::expected<std::unique_ptr<EditorApp>, Error> EditorApp::create(Services              services,
                                                                       const host::Registry& registry,
                                                                       const Options&        options,
                                                                       const AssetSource&    shaders)
    {
        auto frame = Frame::create(services.device());
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        std::unique_ptr<EditorApp> app(
            new EditorApp(std::move(services), registry, options, shaders, std::move(*frame)));
        if (!options.project.empty())
        {
            if (auto opened = app->openProject(options.project); !opened)
            {
                return std::unexpected(opened.error());
            }
            // A project named on the command line that cannot be opened ends the command.
            if (!app->m_Runtime)
            {
                return makeError("{}", app->m_LastProblem);
            }
        }
        return app;
    }

    EditorApp::EditorApp(Services               services,
                         const host::Registry&  registry,
                         const Options&         options,
                         const AssetSource&     shaders,
                         std::unique_ptr<Frame> frame) :
        ImGuiApp(std::move(services), {.title = "Vultra Editor", .size = {1600, 900}}),
        m_Options(options),
        m_Registry(registry),
        m_Shaders(shaders),
        m_Frame(std::move(frame))
    {
        // What the kernel logs while it compiles shaders or imports assets also belongs in the console.
        m_LogSink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& message)
            {
                if (message.level < spdlog::level::info)
                {
                    return;
                }
                const std::scoped_lock lock(m_LogMutex);
                m_Logged.push_back({std::string(message.payload.data(), message.payload.size()),
                                    message.level >= spdlog::level::warn});
            });
        Logger::core().sinks().push_back(m_LogSink);
        Logger::app().sinks().push_back(m_LogSink);
    }

    EditorApp::~EditorApp()
    {
        std::erase(Logger::core().sinks(), m_LogSink);
        std::erase(Logger::app().sinks(), m_LogSink);
        // The GUI may still hold the viewports' images of the last frame.
        getDevice().waitIdleForDestruction();
        for (auto& viewport : m_Viewports)
        {
            if (auto released = releaseImage(viewport); !released)
            {
                std::cerr << "vultra-editor: release " << viewport.title << ": " << released.error().message << '\n';
            }
        }
    }

    std::expected<int, Error> EditorApp::finish()
    {
        if (m_Runtime)
        {
            if (auto saved = saveState(m_Restart); !saved)
            {
                return std::unexpected(saved.error());
            }
        }
        if (m_Restart)
        {
            return kRestart;
        }
        return m_Runtime ? m_Runtime->quitRequested().value_or(0) : 0;
    }

    void EditorApp::log(const std::string& text, bool problem)
    {
        std::cout << "[vultra-editor] " << text << std::endl;
        if (problem)
        {
            m_LastProblem = text;
        }
        // Through the queue the kernel's lines take, so the console keeps the order things happened in.
        const std::scoped_lock lock(m_LogMutex);
        m_Logged.push_back({text, problem});
    }

    // What the resource window lists, kept as text so it does not refer to a workspace that was replaced.
    void EditorApp::describe(const host::Workspace& workspace)
    {
        m_RenderPath = workspace.renderPath.label;
        m_Resources.clear();
        for (const auto& resource : workspace.graph.resources)
        {
            m_Resources.push_back(
                {resource.name, host::passFormatName(resource.format), resource.semantic, resource.provider});
        }
        m_NativeDirectories.clear();
        for (const auto& package : workspace.packages)
        {
            if (package.native)
            {
                m_NativeDirectories.push_back(package.directory);
            }
        }
        // The editor has the packages it was built with; a project's script fails where it uses another.
        for (const auto* package : host::missingNativePackages(workspace, m_Registry))
        {
            log("package " + package->id + " is not part of this editor; `vultra edit` builds one with it", true);
        }
    }

    // Replaces the open project. A project that cannot be opened is reported in the console and the open one
    // stays; an error is a failure of the device.
    std::expected<void, Error> EditorApp::openProject(const std::filesystem::path& location)
    {
        host::Diagnostics diagnostics;
        const auto        workspace = host::loadWorkspace(location, m_Registry, diagnostics, m_Options.mode);
        if (!workspace)
        {
            log("could not open " + location.generic_string() + "\n" + diagnostics.text(baseOf(location)), true);
            return {};
        }
        // The sizing viewport decides the size once the first GUI frame has laid it out.
        auto runtime = host::Runtime::create(getDevice(),
                                             m_Registry,
                                             {{960, 540}, VriFormat_RGBA8_UNORM, &m_Shaders},
                                             host::Display::eWindow);
        if (!runtime)
        {
            return std::unexpected(runtime.error());
        }
        if (!(*runtime)->load(*workspace, diagnostics))
        {
            log("could not open " + workspace->project.name + "\n" + diagnostics.text(workspace->project.directory),
                true);
            return {};
        }
        if (m_Runtime)
        {
            if (auto saved = saveState(false); !saved)
            {
                log("could not save the editor state of the previous project: " + saved.error().message, true);
            }
        }
        // The previous project's viewports and runtime go; their last frame has completed.
        if (auto idle = getDevice().waitIdle(); !idle)
        {
            return idle;
        }
        for (auto& viewport : m_Viewports)
        {
            if (auto released = releaseImage(viewport); !released)
            {
                return released;
            }
        }
        m_Viewports.clear();
        m_NextViewport = 1;
        m_Runtime      = std::move(*runtime);
        m_Location     = workspace->project.file;
        m_Base         = workspace->project.directory;
        if (auto watched = m_Watcher.watch(workspace->watchDirectories); !watched)
        {
            return watched;
        }
        getWindow().setTitle(("Vultra Editor | " + workspace->project.name).c_str());
        describe(*workspace);
        restoreState();
        // The inspector starts on the scene's first entity.
        const auto entities = m_Runtime->session().world().entities();
        m_Selected          = entities.empty() ? std::string() :
                                                 m_Runtime->session().world().registry().get<host::Node>(entities.front()).path;
        // --viewports applies to the project of the command line only.
        m_Options.viewports.reset();
        log("loaded " + workspace->project.name + " (" + host::modeName(m_Options.mode) + ")");
        return {};
    }

    // A change that cannot be applied is a problem of the project, shown in the console; the viewports keep
    // their last image. Only a failure to keep watching ends the editor.
    std::expected<void, Error> EditorApp::reload()
    {
        host::Diagnostics diagnostics;
        const auto        workspace = host::loadWorkspace(m_Location, m_Registry, diagnostics, m_Options.mode);
        if (workspace && m_Runtime->load(*workspace, diagnostics))
        {
            if (auto watched = m_Watcher.watch(workspace->watchDirectories); !watched)
            {
                return watched;
            }
            describe(*workspace);
            log("reloaded " + workspace->project.name);
        }
        if (!diagnostics.ok())
        {
            log("reload failed, keeping the previous frame\n" + diagnostics.text(m_Base), true);
        }
        return {};
    }

    // A C++ file or the manifest of a native package of the project.
    bool EditorApp::nativeSource(const std::filesystem::path& file) const
    {
        const auto extension = file.extension();
        const bool source    = file.filename() == host::kPackageManifest ||
                            std::ranges::any_of(std::array {".cpp", ".cc", ".c", ".hpp", ".h", ".inl"},
                                                [&](const char* candidate)
                                                {
                                                    return extension == candidate;
                                                });
        return source && std::ranges::any_of(m_NativeDirectories,
                                             [&](const std::filesystem::path& directory)
                                             {
                                                 const auto relative = file.lexically_relative(directory);
                                                 return !relative.empty() && *relative.begin() != "..";
                                             });
    }

    std::expected<void, Error> EditorApp::followChanges()
    {
        const auto changes = m_Watcher.poll();
        if (changes.empty())
        {
            return {};
        }
        if (!std::ranges::any_of(changes,
                                 [this](const std::filesystem::path& file)
                                 {
                                     return nativeSource(file);
                                 }))
        {
            return reload();
        }
        // C++ code cannot be replaced in a running process. The command that started this editor builds it
        // again and starts it again, where the state saved by finish() is restored.
        if (const char* supervised = std::getenv(kSupervisedVariable); supervised && *supervised)
        {
            log("the source of a native package changed; rebuilding the editor");
            m_Restart = true;
            getWindow().requestClose();
        }
        else
        {
            log("the source of a native package changed; open the project with `vultra edit` to rebuild", true);
        }
        return {};
    }

    std::filesystem::path EditorApp::stateFile() const
    {
        return m_Base / ".vultra" / "editor.json";
    }

    // What the editor keeps for a project: its viewports with their cameras. Before a restart for a rebuild it
    // also keeps the values of the graph's parameters, which a normal start takes from the project.
    std::expected<void, Error> EditorApp::saveState(bool restart) const
    {
        auto viewports = nlohmann::json::array();
        for (const auto& viewport : m_Viewports)
        {
            if (viewport.open)
            {
                viewports.push_back({{"number", viewport.number}, {"camera", cameraState(viewport.camera)}});
            }
        }
        nlohmann::json document {{"format", "vultra.editor"}, {"version", 1}, {"viewports", std::move(viewports)}};
        if (restart)
        {
            auto parameters = nlohmann::json::array();
            for (const auto& parameter : m_Runtime->session().parameters())
            {
                parameters.push_back({{"pass", parameter.pass}, {"name", parameter.name}, {"value", parameter.value}});
            }
            document["restart"] = {{"parameters", std::move(parameters)}};
        }
        std::error_code failure;
        std::filesystem::create_directories(stateFile().parent_path(), failure);
        if (failure)
        {
            return makeError("Create {}: {}", stateFile().parent_path().generic_string(), failure.message());
        }
        const auto text = document.dump(2) + "\n";
        return writeFileAtomically(stateFile(), std::as_bytes(std::span(text.data(), text.size())));
    }

    // Opens the viewports the project had, or the number the command line asked for. A state that does not
    // fit is ignored: the project then starts with one viewport, as it would have without a state.
    void EditorApp::restoreState()
    {
        nlohmann::json document;
        if (std::ifstream input(stateFile(), std::ios::binary); input)
        {
            std::ostringstream text;
            text << input.rdbuf();
            document = nlohmann::json::parse(text.str(), nullptr, false);
        }
        const bool valid = document.is_object() && document.value("format", std::string()) == "vultra.editor" &&
                           document.value("version", 0) == 1;
        if (valid && !m_Options.viewports && document.contains("viewports") && document.at("viewports").is_array())
        {
            for (const auto& saved : document.at("viewports"))
            {
                if (!saved.is_object() || !saved.value("number", nlohmann::json()).is_number_unsigned() ||
                    !saved.contains("camera"))
                {
                    continue;
                }
                const auto number = saved.at("number").get<uint32_t>();
                if (number == 0 || std::ranges::find(m_Viewports, number, &Viewport::number) != m_Viewports.end())
                {
                    continue;
                }
                auto& viewport  = addViewport();
                viewport.number = number;
                viewport.title  = viewportTitle(number);
                restoreCamera(saved.at("camera"), viewport.camera);
                m_NextViewport = std::max(m_NextViewport, number + 1);
            }
        }
        while (m_Viewports.size() < std::max(m_Options.viewports.value_or(1), 1u))
        {
            addViewport();
        }
        if (!valid || !document.contains("restart"))
        {
            return;
        }
        const auto& restart = document.at("restart");
        if (restart.is_object() && restart.contains("parameters") && restart.at("parameters").is_array())
        {
            for (const auto& parameter : restart.at("parameters"))
            {
                if (parameter.is_object() && parameter.value("pass", nlohmann::json()).is_string() &&
                    parameter.value("name", nlohmann::json()).is_string() &&
                    parameter.value("value", nlohmann::json()).is_number())
                {
                    // A parameter the rebuilt project no longer has, or no longer accepts, keeps its default.
                    static_cast<void>(m_Runtime->session().setParameter(parameter.at("pass").get<std::string>(),
                                                                        parameter.at("name").get<std::string>(),
                                                                        parameter.at("value").get<double>()));
                }
            }
        }
    }

    Viewport& EditorApp::addViewport()
    {
        Viewport viewport;
        viewport.number             = m_NextViewport++;
        viewport.title              = viewportTitle(viewport.number);
        const auto& session         = m_Runtime->session();
        viewport.camera.center      = session.sceneCenter();
        viewport.camera.radius      = session.sceneRadius();
        viewport.camera.distance    = session.sceneRadius() * 2.4f;
        viewport.camera.verticalFov = glm::radians(45.0f);
        // Further viewports start from another side, so they do not repeat the first one.
        viewport.camera.yaw += 1.2f * float(viewport.number - 1);
        m_Viewports.push_back(std::move(viewport));
        return m_Viewports.back();
    }

    // The GUI lets go of an image before it is destroyed. The frames that showed it have completed.
    std::expected<void, Error> EditorApp::releaseImage(Viewport& viewport)
    {
        if (!viewport.image)
        {
            return {};
        }
        if (auto forgotten = getEditorGui().forgetTexture(*viewport.image); !forgotten)
        {
            return forgotten;
        }
        viewport.image.reset();
        return {};
    }

    // Between completed frames: drops closed viewports, follows the size of the sizing viewport's window and
    // gives every viewport an image of the session's size.
    std::expected<void, Error> EditorApp::syncViewports()
    {
        for (auto& viewport : m_Viewports)
        {
            if (!viewport.open)
            {
                if (auto released = releaseImage(viewport); !released)
                {
                    return released;
                }
            }
        }
        std::erase_if(m_Viewports,
                      [](const Viewport& viewport)
                      {
                          return !viewport.open;
                      });
        if (m_Viewports.empty())
        {
            return {};
        }
        auto sizing = std::ranges::find(m_Viewports, m_SizingViewport, &Viewport::number);
        if (sizing == m_Viewports.end())
        {
            sizing           = m_Viewports.begin();
            m_SizingViewport = sizing->number;
        }
        const Extent wanted = sizing->content;
        if (wanted.width > 0 && wanted.height > 0 && !sameSize(wanted, m_Runtime->session().size()))
        {
            host::Diagnostics diagnostics;
            if (!m_Runtime->resize(wanted, diagnostics))
            {
                log("resize failed\n" + diagnostics.text(m_Base), true);
            }
        }
        const Extent size = m_Runtime->session().size();
        for (auto& viewport : m_Viewports)
        {
            if (viewport.image && sameSize({viewport.image->desc.width, viewport.image->desc.height}, size))
            {
                continue;
            }
            if (auto released = releaseImage(viewport); !released)
            {
                return released;
            }
            auto image = Texture::create(getDevice(), colorTexture(size, VriFormat_RGBA8_UNORM));
            if (!image)
            {
                return std::unexpected(image.error());
            }
            viewport.image = std::move(*image);
        }
        return {};
    }

    // The project's input for this frame. Over a viewport it gets the window's keys and buttons with the
    // mouse in that viewport's image; elsewhere, and while Alt moves the camera, everything is released.
    void EditorApp::forwardInput()
    {
        const auto& window  = getWindow().input();
        const bool  camera  = window.isKeyHeld(KeyCode::eLAlt) || window.isKeyHeld(KeyCode::eRAlt);
        const auto  hovered = std::ranges::find(m_Viewports, true, &Viewport::hovered);
        const bool  forward = hovered != m_Viewports.end() && !camera;
        if (hovered != m_Viewports.end())
        {
            m_SizingViewport = hovered->number;
            if (camera)
            {
                // The mouse is over a GUI window, the viewport, so the GUI's own capture does not apply here.
                hovered->camera.update(window, hovered->content);
            }
        }
        m_ProjectInput.setMousePosition(forward ? hovered->pointer : glm::vec2(-1));
        for (uint32_t index = 0; index < uint32_t(MouseCode::eCount); ++index)
        {
            const auto button = MouseCode(index);
            const bool held   = forward && window.isMouseButtonHeld(button);
            if (held != m_ProjectInput.isMouseButtonHeld(button))
            {
                m_ProjectInput.setMouseButton(button, held);
            }
        }
        for (uint32_t index = 0; index < uint32_t(KeyCode::eCount); ++index)
        {
            const auto key  = KeyCode(index);
            const bool held = forward && key != KeyCode::eUnknown && window.isKeyHeld(key);
            if (key != KeyCode::eUnknown && held != m_ProjectInput.isKeyHeld(key))
            {
                m_ProjectInput.setKey(key, held ? InputAction::ePress : InputAction::eRelease);
            }
        }
        if (forward)
        {
            m_ProjectInput.addMouseScroll(window.mouseScrollDelta());
            m_ProjectInput.addText(window.textInput());
        }
        m_ProjectInput.advanceFrame();
    }

    // Records the project once per viewport with that viewport's camera and copies the display image into
    // the viewport's own. Each recording is a submission of its own: the frame graph keeps one set of
    // per-frame data. The hovered viewport is view 0, the view whose camera the project's mouse refers to.
    std::expected<void, Error> EditorApp::renderViewports()
    {
        std::vector<Viewport*> order;
        for (auto& viewport : m_Viewports)
        {
            order.push_back(&viewport);
        }
        std::ranges::stable_partition(order,
                                      [](const Viewport* viewport)
                                      {
                                          return viewport->hovered;
                                      });
        auto&    session = m_Runtime->session();
        uint32_t view    = 0;
        for (auto* viewport : order)
        {
            const auto cmd = m_Frame->begin();
            if (!cmd)
            {
                return std::unexpected(cmd.error());
            }
            if (auto recorded = m_Runtime->record(*cmd, viewport->camera.camera(session.size()), nullptr, view++);
                !recorded)
            {
                return recorded;
            }
            const auto output = session.output("final");
            if (!output)
            {
                return std::unexpected(output.error());
            }
            (*output)->transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            viewport->image->transition(
                *cmd,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriTextureCopyDesc copy {};
            copy.src.aspect   = VriImageAspect_Color;
            copy.dst.aspect   = VriImageAspect_Color;
            copy.src.layerNum = 1;
            copy.dst.layerNum = 1;
            getDevice().core.CmdCopyTexture(*cmd, viewport->image->handle, (*output)->handle, &copy);
            // The GUI samples it in the viewport's window.
            viewport->image->transition(
                *cmd,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            if (auto completed = m_Frame->submitAndWait(); !completed)
            {
                return completed;
            }
        }
        return {};
    }

    std::expected<void, Error> EditorApp::onUpdate(float deltaSeconds)
    {
        if (m_PendingProject)
        {
            const auto location = std::move(*m_PendingProject);
            m_PendingProject.reset();
            if (auto opened = openProject(location); !opened)
            {
                return opened;
            }
        }
        if (!m_Runtime)
        {
            return {};
        }
        if (auto followed = followChanges(); !followed)
        {
            return followed;
        }
        if (m_Restart)
        {
            return {};
        }
        // The windows were laid out in the previous GUI frame; that frame has completed.
        if (auto synced = syncViewports(); !synced)
        {
            return synced;
        }
        forwardInput();
        if (auto updated = m_Runtime->update(deltaSeconds, &m_ProjectInput, m_Runtime->session().size()); !updated)
        {
            return updated;
        }
        if (m_Runtime->quitRequested())
        {
            getWindow().requestClose();
        }
        return renderViewports();
    }

    void EditorApp::menuBar()
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open project..."))
            {
                m_AskProject = true;
            }
            const bool scene =
                m_Runtime && m_Runtime->session().workspace() && !m_Runtime->session().workspace()->scene.file.empty();
            if (ImGui::MenuItem("Save scene", nullptr, false, scene))
            {
                saveScene();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                getWindow().requestClose();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("New viewport", nullptr, false, m_Runtime != nullptr))
            {
                addViewport();
            }
            ImGui::Separator();
            for (auto& viewport : m_Viewports)
            {
                ImGui::MenuItem(viewport.title.c_str(), nullptr, &viewport.open);
            }
            ImGui::Separator();
            ImGui::MenuItem(kSceneWindow, nullptr, &m_ShowScene);
            ImGui::MenuItem(kInspectorWindow, nullptr, &m_ShowInspector);
            ImGui::MenuItem(kParameterWindow, nullptr, &m_ShowParameters);
            ImGui::MenuItem(kResourceWindow, nullptr, &m_ShowResources);
            ImGui::MenuItem(kConsoleWindow, nullptr, &m_ShowConsole);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout"))
            {
                m_ResetLayout = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Asks for the path of a project. The project is opened before the next frame, between completed frames.
    void EditorApp::openProjectPopup()
    {
        if (m_AskProject)
        {
            m_AskProject       = false;
            const auto current = m_Base.generic_string();
            std::fill(m_ProjectText.begin(), m_ProjectText.end(), '\0');
            std::copy_n(current.data(), std::min(current.size(), m_ProjectText.size() - 1), m_ProjectText.begin());
            ImGui::OpenPopup(kProjectPopup);
        }
        if (!ImGui::BeginPopupModal(kProjectPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextUnformatted("Project directory or .vproject file");
        ImGui::SetNextItemWidth(640);
        const bool entered = ImGui::InputText("##project",
                                              m_ProjectText.data(),
                                              m_ProjectText.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if ((entered || ImGui::Button("Open")) && m_ProjectText[0] != '\0')
        {
            m_PendingProject = std::filesystem::path(m_ProjectText.data());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Viewports in the centre; the scene, the inspector and the tabbed parameters and resources down the right;
    // the console along the bottom. Applied
    // when there is no saved layout and on "Reset layout"; afterwards the user's docking is kept.
    void EditorApp::defaultLayout()
    {
        const ImGuiID dockspace = getEditorGui().dockspaceId();
        const auto*   node      = ImGui::DockBuilderGetNode(dockspace);
        const bool    untouched = node && !node->IsSplitNode() && node->Windows.empty();
        if (!m_ResetLayout && !untouched)
        {
            return;
        }
        m_ResetLayout = false;
        ImGui::DockBuilderRemoveNodeChildNodes(dockspace);
        ImGuiID       centre = dockspace;
        const ImGuiID right  = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.26f, nullptr, &centre);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.26f, nullptr, &centre);
        // The right column from the top: the scene, the inspector, then parameters and resources as tabs.
        ImGuiID       scene     = right;
        ImGuiID       inspector = ImGui::DockBuilderSplitNode(scene, ImGuiDir_Down, 0.7f, nullptr, &scene);
        const ImGuiID lower     = ImGui::DockBuilderSplitNode(inspector, ImGuiDir_Down, 0.4f, nullptr, &inspector);
        // Two viewports stand side by side; further ones are tabs of the first. Without a project the first
        // viewport's window is still shown, and says so.
        ImGuiID second = 0;
        if (m_Viewports.size() > 1)
        {
            second = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.5f, nullptr, &centre);
        }
        ImGui::DockBuilderDockWindow(viewportTitle(1).c_str(), centre);
        for (size_t index = 0; index < m_Viewports.size(); ++index)
        {
            ImGui::DockBuilderDockWindow(m_Viewports[index].title.c_str(), index == 1 ? second : centre);
        }
        ImGui::DockBuilderDockWindow(kSceneWindow, scene);
        ImGui::DockBuilderDockWindow(kInspectorWindow, inspector);
        ImGui::DockBuilderDockWindow(kParameterWindow, lower);
        ImGui::DockBuilderDockWindow(kResourceWindow, lower);
        ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
        ImGui::DockBuilderFinish(dockspace);
        m_ViewportDock = centre;
    }

    std::expected<void, Error> EditorApp::viewportWindow(Viewport& viewport)
    {
        // A viewport opened later joins the others as a tab; the user docks it where it is wanted.
        if (m_ViewportDock != 0)
        {
            ImGui::SetNextWindowDockID(m_ViewportDock, ImGuiCond_FirstUseEver);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        const bool shown = ImGui::Begin(viewport.title.c_str(), &viewport.open);
        ImGui::PopStyleVar();
        viewport.hovered = false;
        if (shown)
        {
            if (viewport.number == m_Viewports.front().number && ImGui::IsWindowDocked())
            {
                m_ViewportDock = ImGui::GetWindowDockID();
            }
            const auto available = ImGui::GetContentRegionAvail();
            viewport.content     = {uint32_t(std::clamp(available.x, 16.0f, 8192.0f)),
                                    uint32_t(std::clamp(available.y, 16.0f, 8192.0f))};
            if (viewport.image)
            {
                const auto image = getEditorGui().textureId(*viewport.image);
                if (!image)
                {
                    ImGui::End();
                    return std::unexpected(image.error());
                }
                // Every viewport shows an image of the session's size; one that is not the sizing viewport
                // fits it into its window without distortion.
                const float  width  = float(viewport.image->desc.width);
                const float  height = float(viewport.image->desc.height);
                const float  scale  = std::min(available.x / width, available.y / height);
                const ImVec2 size {width * scale, height * scale};
                const auto   origin = ImGui::GetCursorPos();
                ImGui::SetCursorPos({origin.x + (available.x - size.x) / 2, origin.y + (available.y - size.y) / 2});
                ImGui::Image(*image, size);
                viewport.hovered = ImGui::IsItemHovered();
                if (viewport.hovered)
                {
                    const auto corner = ImGui::GetItemRectMin();
                    const auto mouse  = ImGui::GetMousePos();
                    viewport.pointer  = {(mouse.x - corner.x) / scale, (mouse.y - corner.y) / scale};
                }
            }
        }
        ImGui::End();
        return {};
    }

    std::expected<void, Error> EditorApp::parameterWindow()
    {
        if (!m_ShowParameters)
        {
            return {};
        }
        if (ImGui::Begin(kParameterWindow, &m_ShowParameters) && m_Runtime)
        {
            const auto parameters = m_Runtime->session().parameters();
            if (parameters.empty())
            {
                ImGui::TextDisabled("The project's graph has no parameters.");
            }
            for (const auto& parameter : parameters)
            {
                const auto label = parameter.label + "##" + parameter.pass + "." + parameter.name;
                double     value = parameter.value;
                if (parameterControl(label, parameter, value))
                {
                    // Applied before the next frame is recorded; the previous frame has completed.
                    if (auto set = m_Runtime->session().setParameter(parameter.pass, parameter.name, value); !set)
                    {
                        ImGui::End();
                        return set;
                    }
                }
            }
        }
        ImGui::End();
        return {};
    }

    // The entities of the scene as a tree; a click selects one for the inspector.
    void EditorApp::sceneNode(entt::entity entity)
    {
        const auto& world = m_Runtime->session().world();
        const auto& node  = world.registry().get<host::Node>(entity);
        const auto  flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                           (node.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                           (node.path == m_Selected ? ImGuiTreeNodeFlags_Selected : 0);
        const bool open = ImGui::TreeNodeEx(node.path.c_str(), flags, "%s", node.name.c_str());
        if (ImGui::IsItemClicked())
        {
            m_Selected = node.path;
        }
        if (open)
        {
            for (const auto child : node.children)
            {
                sceneNode(child);
            }
            ImGui::TreePop();
        }
    }

    void EditorApp::sceneWindow()
    {
        if (!m_ShowScene)
        {
            return;
        }
        if (ImGui::Begin(kSceneWindow, &m_ShowScene) && m_Runtime)
        {
            const auto& world = m_Runtime->session().world();
            if (world.entities().empty())
            {
                ImGui::TextDisabled("The project names no scene, or the scene is empty.");
            }
            for (const auto entity : world.entities())
            {
                if (world.registry().get<host::Node>(entity).parent == entt::null)
                {
                    sceneNode(entity);
                }
            }
        }
        ImGui::End();
    }

    // The components of the selected entity. Changes go to the live scene at once, between completed frames,
    // and reach the file with "Save scene".
    void EditorApp::inspectorWindow()
    {
        if (!m_ShowInspector)
        {
            return;
        }
        if (ImGui::Begin(kInspectorWindow, &m_ShowInspector) && m_Runtime)
        {
            auto&      world  = m_Runtime->session().world();
            const auto entity = world.find(m_Selected);
            if (entity == entt::null)
            {
                ImGui::TextDisabled("Select an entity in the scene.");
                ImGui::End();
                return;
            }
            auto& registry = world.registry();
            ImGui::TextUnformatted(m_Selected.c_str());
            // Every described component the entity has, built-in ones first, with controls made from its fields.
            // An edit that breaks a rule of the component, such as a zero scale or a frustum that does not open, is
            // not applied.
            for (const auto list : {host::builtinComponents(), m_Registry.packageComponents()})
            {
                for (const auto& component : list)
                {
                    auto value = component.read(registry, entity);
                    if (value.is_null() ||
                        !ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        continue;
                    }
                    // A material is part of the GPU scene and is checked against its type, which the controls
                    // of its generic fields cannot do: it is shown, and changed in the scene file.
                    if (component.name == "material")
                    {
                        // Read without nlohmann's throwing accessors: find() and a dump that replaces invalid UTF-8.
                        const auto type       = value.find("type");
                        const auto parameters = value.find("parameters");
                        ImGui::TextUnformatted(type != value.end() && type->is_string() ?
                                                   type->get_ref<const std::string&>().c_str() :
                                                   "");
                        ImGui::TextWrapped(
                            "%s",
                            parameters == value.end() ?
                                "{}" :
                                parameters->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
                        ImGui::TextDisabled("Edit the material in the scene file.");
                        continue;
                    }
                    ImGui::PushID(component.name.c_str());
                    if (fieldControls(component.fields, value, m_Base) && (!component.check || !component.check(value)))
                    {
                        if (auto written = component.write(registry, entity, value); !written)
                        {
                            log(component.name + ": " + written.error().message, true);
                        }
                    }
                    if (component.name == "transform" && registry.all_of<host::AnimationComponent>(entity))
                    {
                        ImGui::TextDisabled("An animation moves this entity every frame.");
                    }
                    ImGui::PopID();
                }
            }
            if (const auto* animation = registry.try_get<host::AnimationComponent>(entity);
                animation && ImGui::CollapsingHeader("animation"))
            {
                ImGui::Text("%zu tracks%s", animation->tracks.size(), animation->loop ? ", looping" : "");
            }
        }
        ImGui::End();
    }

    // Writes the live scene over the project's scene file. The file watcher then loads the project again from
    // it, as after any other edit of the file.
    void EditorApp::saveScene()
    {
        const auto& file = m_Runtime->session().workspace()->scene.file;
        const auto  text = host::writeScene(m_Runtime->session().world().describe(file), m_Base, m_Registry);
        if (auto written = writeFileAtomically(file, std::as_bytes(std::span(text.data(), text.size()))); !written)
        {
            log("could not save the scene: " + written.error().message, true);
            return;
        }
        log("saved " + file.lexically_relative(m_Base).generic_string());
    }

    void EditorApp::resourceWindow()
    {
        if (!m_ShowResources)
        {
            return;
        }
        if (ImGui::Begin(kResourceWindow, &m_ShowResources) && m_Runtime)
        {
            ImGui::Text("Render path: %s", m_RenderPath.c_str());
            if (ImGui::BeginTable("resources", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Format");
                ImGui::TableSetupColumn("Semantic");
                ImGui::TableSetupColumn("Provided by");
                ImGui::TableHeadersRow();
                for (const auto& row : m_Resources)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.format.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.semantic.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.provider.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::SeparatorText("Passes of the frame");
            for (const auto& pass : m_Runtime->session().activePasses())
            {
                ImGui::BulletText("%s", pass.c_str());
            }
        }
        ImGui::End();
    }

    void EditorApp::consoleWindow()
    {
        {
            const std::scoped_lock lock(m_LogMutex);
            m_Console.insert(m_Console.end(), m_Logged.begin(), m_Logged.end());
            m_Logged.clear();
        }
        if (m_Console.size() > kConsoleLines)
        {
            m_Console.erase(m_Console.begin(), m_Console.end() - kConsoleLines);
            m_ShownLines = 0;
        }
        if (!m_ShowConsole)
        {
            return;
        }
        if (ImGui::Begin(kConsoleWindow, &m_ShowConsole))
        {
            // A script that failed keeps its last state; its message stays until the script is fixed.
            if (const auto problems = m_Runtime ? m_Runtime->problems() : std::string(); !problems.empty())
            {
                ImGui::TextColored({1, 0.45f, 0.4f, 1}, "%s", problems.c_str());
                ImGui::Separator();
            }
            for (const auto& line : m_Console)
            {
                if (line.problem)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.45f, 0.4f, 1});
                }
                ImGui::TextWrapped("%s", line.text.c_str());
                if (line.problem)
                {
                    ImGui::PopStyleColor();
                }
            }
            if (m_Console.size() != m_ShownLines)
            {
                ImGui::SetScrollHereY(1);
                m_ShownLines = m_Console.size();
            }
        }
        ImGui::End();
    }

    std::expected<void, Error> EditorApp::onImGui()
    {
        menuBar();
        openProjectPopup();
        defaultLayout();
        if (!m_Runtime)
        {
            if (ImGui::Begin(viewportTitle(1).c_str()))
            {
                ImGui::TextDisabled("No project is open. Use File > Open project...");
            }
            ImGui::End();
        }
        // Not a range-for: the menu may have added a viewport before this point.
        for (size_t index = 0; index < m_Viewports.size(); ++index)
        {
            if (!m_Viewports[index].open)
            {
                continue;
            }
            if (auto shown = viewportWindow(m_Viewports[index]); !shown)
            {
                return shown;
            }
        }
        if (auto shown = parameterWindow(); !shown)
        {
            return shown;
        }
        sceneWindow();
        inspectorWindow();
        resourceWindow();
        consoleWindow();
        return m_Runtime ? m_Runtime->gui() : std::expected<void, Error> {};
    }

    // The project was rendered into the viewports' images in onUpdate(); the window shows only the GUI.
    std::expected<void, Error> EditorApp::onRender(VriCommandBuffer* cmd, Texture& target)
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const float clear[4] {0.05f, 0.05f, 0.06f, 1};
        beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
        return drawGui(cmd, target);
    }

    // --capture saves the window of the last frame, or of the first one without --frames.
    std::expected<void, Error> EditorApp::onPostRender(Texture& target)
    {
        if (m_Options.capture.empty() || frameCount() + 1 != std::max<uint64_t>(m_Options.frames, 1))
        {
            return {};
        }
        const auto image = readback(getDevice(), target);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        return savePng(*image, m_Options.capture);
    }
} // namespace vultra::editor
