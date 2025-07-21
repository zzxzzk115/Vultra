#pragma once

#include "vultra/core/rhi/vk/vulkan_include.hpp"

#include <string_view>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap34.html#VkFormat
        enum class PixelFormat
        {
            eUndefined = 0,

            //
            // 8 bits per component:
            //

            // -- Normalized float:

            eR8_UNorm    = VK_FORMAT_R8_UNORM,
            eRG8_UNorm   = VK_FORMAT_R8G8_UNORM,
            eRGBA8_UNorm = VK_FORMAT_R8G8B8A8_UNORM,
            eRGBA8_sRGB  = VK_FORMAT_R8G8B8A8_SRGB,

            eR8_SNorm    = VK_FORMAT_R8_SNORM,
            eRG8_SNorm   = VK_FORMAT_R8G8_SNORM,
            eRGBA8_SNorm = VK_FORMAT_R8G8B8A8_SNORM,

            eBGRA8_UNorm = VK_FORMAT_B8G8R8A8_UNORM,
            eBGRA8_sRGB  = VK_FORMAT_B8G8R8A8_SRGB,

            // -- Integer:

            eR8UI    = VK_FORMAT_R8_UINT,
            eRG8UI   = VK_FORMAT_R8G8_UINT,
            eRGBA8UI = VK_FORMAT_R8G8B8A8_UINT,

            eR8I    = VK_FORMAT_R8_SINT,
            eRG8I   = VK_FORMAT_R8G8_SINT,
            eRGBA8I = VK_FORMAT_R8G8B8A8_SINT,

            //
            // 16 bits per component:
            //

            // -- Normalized float:

            eR16_UNorm    = VK_FORMAT_R16_UNORM,
            eRG16_UNorm   = VK_FORMAT_R16G16_UNORM,
            eRGBA16_UNorm = VK_FORMAT_R16G16B16A16_UNORM,

            eR16_SNorm    = VK_FORMAT_R16_SNORM,
            eRG16_SNorm   = VK_FORMAT_R16G16_SNORM,
            eRGBA16_SNorm = VK_FORMAT_R16G16B16A16_SNORM,

            // -- Float:

            eR16F    = VK_FORMAT_R16_SFLOAT,
            eRG16F   = VK_FORMAT_R16G16_SFLOAT,
            eRGBA16F = VK_FORMAT_R16G16B16A16_SFLOAT,

            // -- Integer:

            eR16UI    = VK_FORMAT_R16_UINT,
            eRG16UI   = VK_FORMAT_R16G16_UINT,
            eRGBA16UI = VK_FORMAT_R16G16B16A16_UINT,

            eR16I    = VK_FORMAT_R16_SINT,
            eRG16I   = VK_FORMAT_R16G16_SINT,
            eRGBA16I = VK_FORMAT_R16G16B16A16_SINT,

            //
            // 32 bits per component:
            //

            // -- Float:

            eR32F    = VK_FORMAT_R32_SFLOAT,
            eRG32F   = VK_FORMAT_R32G32_SFLOAT,
            eRGBA32F = VK_FORMAT_R32G32B32A32_SFLOAT,

            // -- Integer:

            eR32UI    = VK_FORMAT_R32_UINT,
            eRG32UI   = VK_FORMAT_R32G32_UINT,
            eRGBA32UI = VK_FORMAT_R32G32B32A32_UINT,

            eR32I    = VK_FORMAT_R32_SINT,
            eRG32I   = VK_FORMAT_R32G32_SINT,
            eRGBA32I = VK_FORMAT_R32G32B32A32_SINT,

            //
            // ETC:
            //

            eETC2_RGB8_UNorm   = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
            eETC2_RGBA8_UNorm  = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
            eETC2_RGB8A1_UNorm = VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,

            eETC2_RGB8_sRGB   = VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
            eETC2_RGBA8_sRGB  = VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
            eETC2_RGB8A1_sRGB = VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,

            eBC2_UNorm = VK_FORMAT_BC2_UNORM_BLOCK,
            eBC2_sRGB  = VK_FORMAT_BC2_SRGB_BLOCK,

            //
            // BC3:
            //

            eBC3_UNorm = VK_FORMAT_BC3_UNORM_BLOCK,
            eBC3_sRGB  = VK_FORMAT_BC3_SRGB_BLOCK,

            //
            // Depth/Stencil:
            //

            eDepth16 = VK_FORMAT_D16_UNORM,
            eDepth32F = VK_FORMAT_D32_SFLOAT,

            eStencil8 = VK_FORMAT_S8_UINT,

            eDepth16_Stencil8 = VK_FORMAT_D16_UNORM_S8_UINT,
            eDepth24_Stencil8 = VK_FORMAT_D24_UNORM_S8_UINT,
            eDepth32F_Stencil8 = VK_FORMAT_D32_SFLOAT_S8_UINT
        };

        [[nodiscard]] uint8_t getBytesPerPixel(PixelFormat);

        [[nodiscard]] vk::ImageAspectFlags getAspectMask(PixelFormat);
        [[nodiscard]] std::string_view     toString(PixelFormat);
    } // namespace rhi
} // namespace vultra