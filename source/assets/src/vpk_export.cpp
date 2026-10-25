#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/scene_data.hpp>
#include <vultra/assets/shader_asset.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/assets/vpk_export.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::filesystem::path sourceFile(const std::filesystem::path& root, std::string_view path)
        {
            const auto relative = VpkArchive::canonicalPath(path);
            const auto resolved = std::filesystem::canonical(root / relative);
            const auto within   = resolved.lexically_relative(root);
            if (within.empty() || within.is_absolute())
            {
                throw std::invalid_argument("Project asset leaves its root: " + relative);
            }
            for (const auto& part : within)
            {
                if (part == "..")
                {
                    throw std::invalid_argument("Project asset leaves its root: " + relative);
                }
            }
            if (!std::filesystem::is_regular_file(resolved))
            {
                throw std::invalid_argument("Project asset is not a file: " + relative);
            }
            return resolved;
        }

    } // namespace

    void packProject(const std::filesystem::path& projectFile,
                     const std::filesystem::path& output,
                     const ShaderCompileOptions&  shaders,
                     const SceneTypeCatalog&      types)
    {
        auto       project = ProjectManifest::load(projectFile);
        const auto root    = std::filesystem::canonical(projectFile.parent_path().empty() ? std::filesystem::path(".") :
                                                                                            projectFile.parent_path());
        auto       scene   = SceneTree::load(sourceFile(root, pathText(project.mainScene)), types);
        scene.validateAssets(project);
        std::vector<VpkFile> files;

        struct CookDirectory
        {
            std::filesystem::path path;

            ~CookDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cooked {std::filesystem::temp_directory_path() /
                  ("vultra-project-shaders-" + StableId::generate().toString())};

        std::filesystem::create_directory(cooked.path);
        // Cook only explicit shader assets. Native include libraries are not project material entrypoints.
        const auto assets = project.assets();
        for (const auto& asset : assets)
        {
            if (asset.path.extension() == ".vshader" || asset.path.extension() == ".slang")
            {
                auto relative = asset.path;
                relative.replace_extension(".vshaderc");
                auto options = shaders;
                options.includeDirectories.insert(options.includeDirectories.begin(), root);
                const auto cached = root / ".vultra/shaders" / (asset.id.value.toString() + ".vshaderc");
                if (asset.path.extension() == ".vshader")
                {
                    ShaderAsset::cook(sourceFile(root, pathText(asset.path)), cached, options);
                }
                else
                {
                    ShaderProgram::cook(sourceFile(root, pathText(asset.path)), cached, options);
                }
                std::filesystem::create_directories((cooked.path / relative).parent_path());
                std::filesystem::copy_file(cached, cooked.path / relative);
                project.renameAsset(asset.id, relative);
            }
        }
        const auto manifest = cooked.path / "project.vproject";
        project.save(manifest);
        files.emplace_back("project.vproject", manifest);
        files.emplace_back(VpkArchive::canonicalPath(pathText(project.mainScene)),
                           sourceFile(root, pathText(project.mainScene)));
        for (const auto& asset : project.assets())
        {
            const auto artifact = cooked.path / asset.path;
            files.emplace_back(VpkArchive::canonicalPath(pathText(asset.path)),
                               std::filesystem::is_regular_file(artifact) ? artifact :
                                                                            sourceFile(root, pathText(asset.path)));
            auto extension = asset.path.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if (extension == ".fbx")
            {
                Logger::core().info("Collecting FBX package dependencies: {}", pathText(asset.path));
                const SourceObserver dependency = [&](const auto& file, std::span<const std::byte>)
                {
                    const auto relative = file.lexically_relative(root);
                    const auto name     = VpkArchive::canonicalPath(pathText(relative));
                    files.emplace_back(name, sourceFile(root, name));
                };
                loadFbx(sourceFile(root, pathText(asset.path)),
                        dependency,
                        0,
                        nullptr,
                        asset.fbx.value_or(FbxImportOptions {}));
            }
        }
        for (const auto& script : project.scripts)
        {
            const auto& module = project.asset(script.asset).path;
            if (script.language == ScriptLanguage::eCSharp)
            {
                const auto bridge = module.parent_path() / "Vultra.ManagedHost.dll";
                const auto api    = module.parent_path() / "Vultra.Scripting.dll";
                const auto config = std::filesystem::path(bridge).replace_extension(".runtimeconfig.json");
                files.emplace_back(VpkArchive::canonicalPath(pathText(bridge)), sourceFile(root, pathText(bridge)));
                files.emplace_back(VpkArchive::canonicalPath(pathText(api)), sourceFile(root, pathText(api)));
                files.emplace_back(VpkArchive::canonicalPath(pathText(config)), sourceFile(root, pathText(config)));
                for (const auto& assembly : {module, bridge})
                {
                    const auto dependencies = std::filesystem::path(assembly).replace_extension(".deps.json");
                    if (std::filesystem::exists(root / dependencies))
                    {
                        files.emplace_back(VpkArchive::canonicalPath(pathText(dependencies)),
                                           sourceFile(root, pathText(dependencies)));
                    }
                }
            }
        }
        VpkArchive::pack(files, output);
    }

    void packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output)
    {
        const auto engine = std::filesystem::canonical(engineRoot);
        const auto passes = engine / "builtin/shaders/passes";
        if (!std::filesystem::is_directory(passes))
        {
            throw std::invalid_argument("Engine shader directory is missing: " + passes.string());
        }

        // The cook directory belongs to this invocation and is removed after the archive is finalized.
        struct CookDirectory
        {
            std::filesystem::path path;

            ~CookDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cooked {std::filesystem::temp_directory_path() / ("vultra-shaders-" + StableId::generate().toString())};

        std::filesystem::create_directory(cooked.path);
        const std::array                   includes {engine / "builtin/shaders", engine / "external"};
        std::vector<std::filesystem::path> sources;
        for (const auto& item : std::filesystem::recursive_directory_iterator(passes))
        {
            if (item.is_regular_file() && item.path().extension() == ".slang")
            {
                sources.push_back(item.path());
            }
        }
        std::ranges::sort(sources);
        if (sources.empty())
        {
            throw std::invalid_argument("No built-in shader passes to cook");
        }
        std::vector<VpkFile> files;
        for (const auto& source : sources)
        {
            Logger::core().info("Cooking built-in shader {}", source.filename().string());
            const auto program = ShaderProgram::compile(source, {}, includes, source.filename() == "path_trace.slang");
            if (!program.diagnostics.empty())
            {
                Logger::core().warn("{}", program.diagnostics);
            }
            auto relative = source.lexically_relative(engine);
            relative.replace_extension(".vshaderc");
            const auto outputFile = cooked.path / relative;
            program.save(outputFile);
            files.emplace_back(VpkArchive::canonicalPath(pathText(relative)), outputFile);
        }
        // Ship upstream attribution with the compiled implementation, without compiler inputs.
        files.emplace_back("external/vrf_fbx_license.txt", sourceFile(engine, "external/vrf_fbx_license.txt"));
        files.emplace_back("external/flip/LICENSE", sourceFile(engine, "external/flip/LICENSE"));
        files.emplace_back("external/flip/README.vultra.md", sourceFile(engine, "external/flip/README.vultra.md"));
        for (const auto* name : {"LICENSE.txt", "README.md"})
        {
            const auto relative = std::string("external/pcg_basic/") + name;
            files.emplace_back(relative, sourceFile(engine, relative));
        }
        for (const auto* name : {"LICENSE", "README.vultra.md"})
        {
            const auto relative = std::string("external/openpbr/") + name;
            files.emplace_back(relative, sourceFile(engine, relative));
        }
        const auto sdk = engine / "build/sdk";
        if (std::filesystem::is_directory(sdk))
        {
            for (const auto& item : std::filesystem::recursive_directory_iterator(sdk))
            {
                if (item.is_regular_file())
                {
                    files.emplace_back("sdk/" + pathText(item.path().lexically_relative(sdk)), item.path());
                }
            }
        }
        VpkArchive::pack(files, output);
    }
} // namespace vultra
