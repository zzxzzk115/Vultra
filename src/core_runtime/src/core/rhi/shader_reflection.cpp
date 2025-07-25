#include "vultra/core/rhi/shader_reflection.hpp"

#include <spirv_cross/spirv_glsl.hpp>

// https://github.com/KhronosGroup/SPIRV-Cross/wiki/Reflection-API-user-guide

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] vk::ShaderStageFlags toVk(const spv::ExecutionModel executionModel)
            {
                switch (executionModel)
                {
                    case spv::ExecutionModelVertex:
                        return vk::ShaderStageFlagBits::eVertex;
                    case spv::ExecutionModelGeometry:
                        return vk::ShaderStageFlagBits::eGeometry;
                    case spv::ExecutionModelFragment:
                        return vk::ShaderStageFlagBits::eFragment;
                    case spv::ExecutionModelGLCompute:
                        return vk::ShaderStageFlagBits::eCompute;

                    default:
                        assert(false);
                        return vk::ShaderStageFlagBits::eVertex;
                }
            }

            [[nodiscard]] auto getLocalSize(const spirv_cross::CompilerGLSL& compiler)
            {
                glm::uvec3 localSize {};
                for (auto i = 0; i < decltype(localSize)::length(); ++i)
                {
                    localSize[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
                }
                return localSize;
            }

        } // namespace

        void ShaderReflection::accumulate(SPIRV&& spv)
        {
            spirv_cross::CompilerGLSL compiler {std::move(spv)};

            const auto stageFlag = toVk(compiler.get_execution_model());
            if (stageFlag & vk::ShaderStageFlagBits::eCompute)
            {
                localSize = getLocalSize(compiler);
            }
            const auto shaderResources = compiler.get_shader_resources();

            const auto addResource = [&](const spirv_cross::Resource& r, vk::DescriptorType descriptorType) {
                const auto& type    = compiler.get_type(r.type_id);
                const auto  set     = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                const auto  binding = compiler.get_decoration(r.id, spv::DecorationBinding);

                auto [it, emplaced] = descriptorSets[set].try_emplace(binding, descriptorType);
                auto& resource      = it->second;
                if (emplaced)
                    resource.count = type.array.empty() ? 1 : type.array[0];
                resource.stageFlags |= stageFlag;
            };

#define ADD_RESOURCES(Member, EnumValue) \
    for (const auto& r : shaderResources.Member) \
    { \
        addResource(r, vk::DescriptorType::e##EnumValue); \
    }

            ADD_RESOURCES(separate_samplers, Sampler)
            ADD_RESOURCES(sampled_images, CombinedImageSampler)
            ADD_RESOURCES(separate_images, SampledImage)
            ADD_RESOURCES(storage_images, StorageImage)
            ADD_RESOURCES(uniform_buffers, UniformBuffer)
            ADD_RESOURCES(storage_buffers, StorageBuffer)

#undef ADD_RESOURCES

            if (!shaderResources.push_constant_buffers.empty())
            {
                const auto& pc   = shaderResources.push_constant_buffers.front();
                const auto& type = compiler.get_type(pc.base_type_id);

                vk::PushConstantRange range {};
                range.offset     = 0;
                range.size       = 0;
                range.stageFlags = stageFlag;

                for (auto i = 0u; i < type.member_types.size(); ++i)
                {
                    if (i == 0)
                        range.offset = compiler.type_struct_member_offset(type, i);
                    range.size += static_cast<uint32_t>(compiler.get_declared_struct_member_size(type, i));
                }
                pushConstantRanges.emplace_back(range);
            }
        }
    } // namespace rhi
} // namespace vultra