#include <vultra/scene/scene_script.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace vultra
{
    void SceneScript::validate() const
    {
        if (!module.value.valid() || className.empty() || className.contains('\0'))
        {
            throw std::invalid_argument("Scene script requires a module asset ID and class name");
        }
        const auto values = nlohmann::json::parse(exports);
        if (!values.is_object())
        {
            throw std::invalid_argument("Scene script exports must be a JSON object");
        }
        for (const auto& value : values.items())
        {
            if (value.key().empty() || value.key().contains('\0'))
            {
                throw std::invalid_argument("Scene script export requires a field name");
            }
        }
    }
} // namespace vultra
