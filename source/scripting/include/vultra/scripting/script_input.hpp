#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/core/input/input_codes.hpp>

#include <string>

namespace vultra::scripting
{
    class NativeScript;

    // Owned snapshot. Actions/consumption go through the original script callback, never a retained host pointer.
    class InputEvent
    {
    public:
        InputEventKind kind() const
        {
            return InputEventKind(m_Data.kind);
        }

        uint32_t code() const
        {
            return m_Data.code;
        }

        InputAction action() const
        {
            return InputAction(m_Data.action);
        }

        uint32_t modifiers() const
        {
            return m_Data.modifiers;
        }

        float x() const
        {
            return m_Data.x;
        }

        float y() const
        {
            return m_Data.y;
        }

        float deltaX() const
        {
            return m_Data.deltaX;
        }

        float deltaY() const
        {
            return m_Data.deltaY;
        }

        const std::string& text() const
        {
            return m_Text;
        }

    private:
        friend class NativeScript;
        VultraSceneInputData m_Data {};
        VultraSceneFrame     m_Frame {};
        uint64_t             m_NodeId = 0;
        std::string          m_Text;
    };
} // namespace vultra::scripting
