#include "vultra/function/resource/raw_resource_loader.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <magic_enum/magic_enum.hpp>

namespace vultra
{
    namespace resource
    {
        std::expected<rhi::Texture, std::string> loadTextureSTB(const std::filesystem::path& p, rhi::RenderDevice& rd)
        {
            stbi_set_flip_vertically_on_load(false);

            auto file = stbi__fopen(p.string().c_str(), "rb");
            if (!file)
            {
                return std::unexpected {"Could not open the file."};
            }

            const auto hdr = stbi_is_hdr_from_file(file);

            int32_t width;
            int32_t height;

            struct Deleter
            {
                void operator()(void* pixels) const { stbi_image_free(pixels); }
            };
            std::unique_ptr<void, Deleter> pixels;
            {
                auto ptr =
                    hdr ? static_cast<void*>(stbi_loadf_from_file(file, &width, &height, nullptr, STBI_rgb_alpha)) :
                          static_cast<void*>(stbi_load_from_file(file, &width, &height, nullptr, STBI_rgb_alpha));
                pixels.reset(ptr);
            }
            fclose(file);

            if (!pixels)
            {
                return std::unexpected {stbi_failure_reason()};
            }

            const auto extent       = rhi::Extent2D {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            const auto numMipLevels = rhi::calcMipLevels(extent);

            const auto generateMipmaps = numMipLevels > 1;

            auto usageFlags = rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled;
            if (generateMipmaps)
                usageFlags |= rhi::ImageUsage::eTransferSrc;

            const auto pixelFormat = hdr ? rhi::PixelFormat::eRGBA32F : rhi::PixelFormat::eRGBA8_UNorm;
            auto       texture     = rhi::Texture::Builder {}
                               .setExtent(extent)
                               .setPixelFormat(pixelFormat)
                               .setNumMipLevels(numMipLevels)
                               .setNumLayers(std::nullopt)
                               .setUsageFlags(usageFlags)
                               .setupOptimalSampler(true)
                               .build(rd);
            if (!texture)
            {
                return std::unexpected {
                    std::format("Unsupported pixel format: {}.", magic_enum::enum_name(pixelFormat))};
            }

            const auto pixelSize        = static_cast<int32_t>(hdr ? sizeof(float) : sizeof(uint8_t));
            const auto uploadSize       = width * height * STBI_rgb_alpha * pixelSize;
            const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, pixels.get());
            rhi::upload(rd, srcStagingBuffer, {}, texture, generateMipmaps);

            return texture;
        }
    } // namespace resource
} // namespace vultra