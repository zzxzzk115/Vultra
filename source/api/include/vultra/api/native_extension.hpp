#pragma once

#include <vultra/api/native_plugin.h>

#include <filesystem>
#include <memory>

namespace vultra
{
    class SceneTypeCatalog;

    // Project-owned code and type records. The catalog must outlive this owner; worlds/instances retire first.
    class NativeExtension
    {
    public:
        NativeExtension(const std::filesystem::path& path, SceneTypeCatalog& types);
        NativeExtension(VultraPluginInit initialize, SceneTypeCatalog& types);
        ~NativeExtension();
        NativeExtension(const NativeExtension&)            = delete;
        NativeExtension& operator=(const NativeExtension&) = delete;

        // Fails without unloading while another catalog, a derived type or an instance still borrows the module.
        void unload();

    private:
        void initialize(const std::filesystem::path& path, VultraPluginInit entry);
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
