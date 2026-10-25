#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vultra
{
    namespace
    {
        constexpr int kProjectVersion = 1;

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = path.generic_u8string();
            return {text.begin(), text.end()};
        }

        std::filesystem::path projectPath(std::string_view text)
        {
            const std::u8string utf8(text.begin(), text.end());
            const auto          input = std::filesystem::path(utf8);
            if (input.empty() || input.is_absolute() || input.has_root_name())
            {
                throw std::invalid_argument("Project path must be relative");
            }
            for (const auto& part : input)
            {
                if (part == "..")
                {
                    throw std::invalid_argument("Project path may not leave its directory");
                }
            }
            const auto path = input.lexically_normal();
            if (path == ".")
            {
                throw std::invalid_argument("Project path must name a file");
            }
            return path;
        }

        const char* languageName(ScriptLanguage language)
        {
            switch (language)
            {
                case ScriptLanguage::eNative:
                    return "native";
                case ScriptLanguage::eLua:
                    return "lua";
                case ScriptLanguage::eCSharp:
                    return "csharp";
            }
            throw std::invalid_argument("Unknown script language");
        }

        ScriptLanguage parseLanguage(std::string_view name)
        {
            if (name == "native")
            {
                return ScriptLanguage::eNative;
            }
            if (name == "lua")
            {
                return ScriptLanguage::eLua;
            }
            if (name == "csharp")
            {
                return ScriptLanguage::eCSharp;
            }
            throw std::invalid_argument("Unknown script language");
        }

        AssetId parseAssetId(const std::string& text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid asset ID: " + text);
            }
            return {*parsed};
        }
    } // namespace

    AssetId ProjectManifest::addAsset(const std::filesystem::path& path, std::optional<FbxImportOptions> fbx)
    {
        const auto normalized = projectPath(pathText(path));
        if (std::ranges::any_of(m_Assets,
                                [&](const AssetEntry& asset)
                                {
                                    return asset.path == normalized;
                                }))
        {
            throw std::invalid_argument("Duplicate project asset path: " + pathText(normalized));
        }
        AssetId id;
        do
        {
            id.value = StableId::generate();
        } while (std::ranges::any_of(m_Assets,
                                     [&](const AssetEntry& asset)
                                     {
                                         return asset.id == id;
                                     }));
        m_Assets.push_back({id, normalized, fbx});
        return id;
    }

    void ProjectManifest::renameAsset(AssetId id, const std::filesystem::path& path)
    {
        const auto normalized = projectPath(pathText(path));
        if (std::ranges::any_of(m_Assets,
                                [&](const AssetEntry& asset)
                                {
                                    return asset.path == normalized && asset.id != id;
                                }))
        {
            throw std::invalid_argument("Duplicate project asset path: " + pathText(normalized));
        }
        const auto entry = std::ranges::find_if(m_Assets,
                                                [&](const AssetEntry& candidate)
                                                {
                                                    return candidate.id == id;
                                                });
        if (entry == m_Assets.end())
        {
            throw std::invalid_argument("Unknown project asset ID: " + id.value.toString());
        }
        entry->path = normalized;
    }

    const AssetEntry& ProjectManifest::asset(AssetId id) const
    {
        const auto entry = std::ranges::find_if(m_Assets,
                                                [&](const AssetEntry& candidate)
                                                {
                                                    return candidate.id == id;
                                                });
        if (entry == m_Assets.end())
        {
            throw std::invalid_argument("Unknown project asset ID: " + id.value.toString());
        }
        return *entry;
    }

    const std::vector<AssetEntry>& ProjectManifest::assets() const
    {
        return m_Assets;
    }

    const ScriptModule& ProjectManifest::scriptModule(AssetId id) const
    {
        const auto found = std::ranges::find(scripts, id, &ScriptModule::asset);
        if (found == scripts.end())
        {
            throw std::invalid_argument("Asset is not a declared script module: " + id.value.toString());
        }
        return *found;
    }

    void ProjectManifest::validate() const
    {
        projectPath(pathText(mainScene));
        for (size_t i = 0; i < m_Assets.size(); ++i)
        {
            const auto& entry = m_Assets[i];
            if (!entry.id.value.valid())
            {
                throw std::invalid_argument("Project asset ID is empty");
            }
            projectPath(pathText(entry.path));
            auto extension = entry.path.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if (entry.fbx && ((extension != ".fbx") ||
                              (entry.fbx->materialConvention != FbxMaterialConvention::ePhong &&
                               entry.fbx->materialConvention != FbxMaterialConvention::eOrcaMetallicRoughness)))
            {
                throw std::invalid_argument("fbx_import requires an FBX asset and a supported material convention");
            }
            for (size_t j = 0; j < i; ++j)
            {
                if (m_Assets[j].id == entry.id || m_Assets[j].path == entry.path)
                {
                    throw std::invalid_argument("Duplicate project asset ID or path");
                }
            }
        }
        if (environment)
        {
            asset(*environment);
        }
        if (uiDocument)
        {
            asset(*uiDocument);
        }
        if (uiFont)
        {
            asset(*uiFont);
        }
        if (!nlohmann::json::parse(metadata).is_object())
        {
            throw std::invalid_argument("Project metadata must be a JSON object");
        }
        for (size_t index = 0; index < extensions.size(); ++index)
        {
            asset(extensions[index]);
            if (std::ranges::find(extensions.begin(), extensions.begin() + index, extensions[index]) !=
                extensions.begin() + index)
            {
                throw std::invalid_argument("Duplicate project extension asset");
            }
        }
        for (const auto& script : scripts)
        {
            asset(script.asset);
            languageName(script.language);
            if (std::ranges::count(scripts, script.asset, &ScriptModule::asset) != 1)
            {
                throw std::invalid_argument("Duplicate project script module asset");
            }
        }
    }

    void ProjectManifest::save(const std::filesystem::path& file) const
    {
        validate();
        nlohmann::json document = {{"format", "vultra.project"},
                                   {"version", kProjectVersion},
                                   {"main_scene", pathText(mainScene)},
                                   {"assets", nlohmann::json::array()},
                                   {"extensions", nlohmann::json::array()},
                                   {"scripts", nlohmann::json::array()}};
        if (environment)
        {
            document["environment"] = environment->value.toString();
        }
        if (uiDocument)
        {
            document["ui_document"] = uiDocument->value.toString();
        }
        if (uiFont)
        {
            document["ui_font"] = uiFont->value.toString();
        }
        for (const auto& asset : m_Assets)
        {
            nlohmann::json entry {{"id", asset.id.value.toString()}, {"path", pathText(asset.path)}};
            if (asset.fbx)
            {
                entry["fbx_import"] = {
                    {"materials", asset.fbx->materialConvention == FbxMaterialConvention::ePhong ? "phong" : "orca"},
                    {"normal_maps", asset.fbx->directXNormalMaps ? "directx" : "opengl"}};
            }
            document["assets"].push_back(std::move(entry));
        }
        for (const auto& extension : extensions)
        {
            document["extensions"].push_back(extension.value.toString());
        }
        for (const auto& script : scripts)
        {
            document["scripts"].push_back(
                {{"asset", script.asset.value.toString()}, {"language", languageName(script.language)}});
        }
        const auto settings = nlohmann::json::parse(metadata);
        if (!settings.empty())
        {
            document["metadata"] = settings;
        }
        const auto text = document.dump(2) + "\n";
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }

    ProjectManifest ProjectManifest::load(const std::filesystem::path& file, const AssetSource* source)
    {
        try
        {
            const auto  bytes    = readSourceFile(file, {}, source);
            const auto* begin    = reinterpret_cast<const char*>(bytes.data());
            const auto  document = nlohmann::json::parse(begin, begin + bytes.size());
            if (document.at("format") != "vultra.project" || document.at("version") != kProjectVersion)
            {
                throw std::invalid_argument("Unsupported project manifest format or version");
            }
            for (const auto& field : document.items())
            {
                const auto& name = field.key();
                if (name != "format" && name != "version" && name != "main_scene" && name != "assets" &&
                    name != "extensions" && name != "scripts" && name != "environment" && name != "ui_document" &&
                    name != "ui_font" && name != "metadata")
                {
                    throw std::invalid_argument("Unknown project manifest field: " + name);
                }
            }
            ProjectManifest project;
            project.mainScene = projectPath(document.at("main_scene").get<std::string>());
            if (document.contains("environment"))
            {
                project.environment = parseAssetId(document.at("environment").get<std::string>());
            }
            if (document.contains("ui_document"))
            {
                project.uiDocument = parseAssetId(document.at("ui_document").get<std::string>());
            }
            if (document.contains("ui_font"))
            {
                project.uiFont = parseAssetId(document.at("ui_font").get<std::string>());
            }
            for (const auto& entry : document.at("assets"))
            {
                project.m_Assets.push_back({parseAssetId(entry.at("id").get<std::string>()),
                                            projectPath(entry.at("path").get<std::string>())});
                if (entry.contains("fbx_import"))
                {
                    const auto& description = entry.at("fbx_import");
                    if (!description.is_object())
                    {
                        throw std::invalid_argument("fbx_import must be an object");
                    }
                    for (const auto& setting : description.items())
                    {
                        const auto& key = setting.key();
                        if (key != "materials" && key != "normal_maps")
                        {
                            throw std::invalid_argument("Unknown fbx_import setting: " + key);
                        }
                    }
                    const auto materials = description.value("materials", std::string("phong"));
                    const auto normals   = description.value("normal_maps", std::string("opengl"));
                    if ((materials != "phong" && materials != "orca") || (normals != "opengl" && normals != "directx"))
                    {
                        throw std::invalid_argument("Unknown fbx_import material or normal-map convention");
                    }
                    project.m_Assets.back().fbx =
                        FbxImportOptions {materials == "orca" ? FbxMaterialConvention::eOrcaMetallicRoughness :
                                                                FbxMaterialConvention::ePhong,
                                          normals == "directx"};
                }
            }
            for (const auto& extension : document.at("extensions"))
            {
                project.extensions.push_back(parseAssetId(extension.get<std::string>()));
            }
            for (const auto& entry : document.at("scripts"))
            {
                if (!entry.is_object() || entry.size() != 2)
                {
                    throw std::invalid_argument("Script modules require only asset and language fields");
                }
                project.scripts.push_back({parseAssetId(entry.at("asset").get<std::string>()),
                                           parseLanguage(entry.at("language").get<std::string>())});
            }
            project.metadata = document.value("metadata", nlohmann::json::object()).dump();
            project.validate();
            return project;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Read project manifest " + file.string() + ": " + error.what());
        }
    }

    std::filesystem::path ProjectManifest::materializeModule(const AssetSource&           source,
                                                             const std::filesystem::path& module) const
    {
        const auto result = source.materialize(module);
        const auto lua    = std::ranges::any_of(scripts,
                                                [&](const auto& script)
                                                {
                                                 return asset(script.asset).path == module &&
                                                        script.language == ScriptLanguage::eLua;
                                                });
        // The .NET host requires adjacent assemblies and its runtime/dependency descriptions.
        if (std::ranges::any_of(scripts,
                                [&](const auto& script)
                                {
                                    return asset(script.asset).path == module &&
                                           script.language == ScriptLanguage::eCSharp;
                                }))
        {
            const auto directory = module.parent_path();
            source.materialize(directory / "Vultra.Scripting.dll");
            source.materialize(directory / "Vultra.ManagedHost.dll");
            source.materialize(directory / "Vultra.ManagedHost.runtimeconfig.json");
            const std::array optional {directory / "Vultra.ManagedHost.deps.json",
                                       directory / (module.stem().string() + ".deps.json")};
            for (const auto& path : optional)
            {
                if (source.contains(path))
                {
                    source.materialize(path);
                }
            }
        }
        for (const auto& asset : assets())
        {
            auto extension = asset.path.extension().string();
            std::ranges::transform(extension,
                                   extension.begin(),
                                   [](unsigned char c)
                                   {
                                       return char(std::tolower(c));
                                   });
            if ((lua && (extension == ".lua" || extension == ".luac")) || extension == ".dll" || extension == ".so" ||
                extension == ".dylib" || asset.path.filename().string().contains(".so."))
            {
                source.materialize(asset.path);
            }
        }
        return result;
    }
} // namespace vultra
