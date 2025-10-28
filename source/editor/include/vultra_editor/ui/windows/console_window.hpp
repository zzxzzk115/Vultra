#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <imgui.h>

using LogLevel = vultra::Logger::Level;

namespace vultra
{
    namespace editor
    {
        class ConsoleWindow final : public UIWindow
        {
        public:
            ConsoleWindow();
            ~ConsoleWindow() override;

            void onImGui() override;

        private:
            struct LogEntry
            {
                LogLevel    level;
                std::string message;
            };

            std::vector<LogEntry> m_LogMessages;
            uint16_t              m_LogMessagesEnd;
            uint32_t              m_LogMessagesFilter;
            ImGuiTextFilter       m_Filter;
            bool                  m_AllowToBottom;
            bool                  m_RequestToBottom;
        };
    } // namespace editor
} // namespace vultra