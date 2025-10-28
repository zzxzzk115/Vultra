#include "vultra_editor/ui/windows/console_window.hpp"

#include <IconsMaterialDesignIcons.h>

namespace
{
    // Helper function to convert log level to ImGui color
    ImVec4 getLogLevelColor(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::eTrace:
                return {0.75f, 0.75f, 0.75f, 1.00f}; // Gray
            case LogLevel::eInfo:
                return {0.40f, 0.70f, 1.00f, 1.00f}; // Blue
            case LogLevel::eWarn:
                return {1.00f, 1.00f, 0.00f, 1.00f}; // Yellow
            case LogLevel::eError:
                return {1.00f, 0.25f, 0.25f, 1.00f}; // Red
            case LogLevel::eCritical:
                return {0.6f, 0.2f, 0.8f, 1.00f}; // Purple
            default:
                return {1.00f, 1.00f, 1.00f, 1.00f};
        }
    }

    // Helper function to get log level icon (material design)
    const char* getLogLevelIcon(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::eTrace:
                return ICON_MDI_MESSAGE_TEXT;
            case LogLevel::eInfo:
                return ICON_MDI_INFORMATION;
            case LogLevel::eWarn:
                return ICON_MDI_ALERT;
            case LogLevel::eError:
                return ICON_MDI_CLOSE_OCTAGON;
            case LogLevel::eCritical:
                return ICON_MDI_ALERT_OCTAGRAM;
            default:
                return ICON_MDI_ALERT_OCTAGRAM;
        }
    }

    uint32_t getLogLevelFlag(LogLevel level) { return 1 << static_cast<uint8_t>(level); }
} // namespace

namespace vultra
{
    namespace editor
    {
        enum MyItemColumnID
        {
            eMessage,
            eType
        };

        const uint32_t MAX_LOG_MESSAGES = 3500;

        ConsoleWindow::ConsoleWindow() : UIWindow("Console")
        {
            m_LogMessages.resize(MAX_LOG_MESSAGES);
            m_LogMessagesEnd    = 0;
            m_LogMessagesFilter = getLogLevelFlag(LogLevel::eMaxLevels) - 1;
            m_AllowToBottom     = true;
            m_RequestToBottom   = false;

            vultra::commonContext.logger.on<Logger::LogEvent>([this](const Logger::LogEvent& event, auto&) {
                // Only log client events
                if (event.region != Logger::Region::eClient)
                    return;
                if (m_LogMessagesEnd >= MAX_LOG_MESSAGES)
                {
                    m_LogMessagesEnd = 0;
                }
                m_LogMessages[m_LogMessagesEnd++] = {event.level, event.msg};
                if (m_AllowToBottom)
                    m_RequestToBottom = true;
            });
        }

        ConsoleWindow::~ConsoleWindow() = default;

        void ConsoleWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());

            ImGuiStyle& style = ImGui::GetStyle();

            ImGui::AlignTextToFramePadding();
            ImGui::SameLine();
            ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
            ImGui::SameLine();

            float spacing                   = ImGui::GetStyle().ItemSpacing.x;
            ImGui::GetStyle().ItemSpacing.x = 2;

            float levelButtonWidth = ImGui::CalcTextSize(getLogLevelIcon(static_cast<LogLevel>(1))).x +
                                     ImGui::GetStyle().FramePadding.x * 2.0f;
            float levelButtonWidths = (levelButtonWidth + ImGui::GetStyle().ItemSpacing.x) * 5;

            {
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
                m_Filter.Draw("###ConsoleFilter", ImGui::GetContentRegionAvail().x - (levelButtonWidths));
                auto*  drawList = ImGui::GetWindowDrawList();
                ImVec2 min      = ImGui::GetItemRectMin();
                ImVec2 max      = ImGui::GetItemRectMax();
                min.x -= 1.0f;
                min.y -= 1.0f;
                max.x += 1.0f;
                max.y += 1.0f;
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
                {
                    drawList->AddRect(min, max, ImColor(60, 60, 60), 2.0f, 0, 1.5f);
                }
                if (ImGui::IsItemActive())
                {
                    drawList->AddRect(min, max, ImColor(80, 80, 80), 2.0f, 0, 1.0f);
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                ImGui::PopFont();
            }

            // Log level buttons
            ImGui::SameLine();
            for (int i = 0; i < 5; i++)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::SameLine();
                auto level     = static_cast<LogLevel>(i);
                auto levelFlag = getLogLevelFlag(level);

                bool levelEnabled = m_LogMessagesFilter & levelFlag;
                if (levelEnabled)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, getLogLevelColor(level));
                }
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5, 0.5f, 0.5f));

                if (ImGui::Button(getLogLevelIcon(level)))
                {
                    m_LogMessagesFilter ^= levelFlag;
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", getLogLevelIcon(level));
                }

                ImGui::PopStyleColor(2);
            }

            ImGui::GetStyle().ItemSpacing.x = spacing;

            // Clear button
            if (!m_Filter.IsActive())
            {
                ImGui::SameLine();
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImGui::SetCursorPosX(ImGui::GetFontSize() * 4.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
                ImGui::TextUnformatted("Search...");
                ImGui::PopStyleVar();
                ImGui::PopFont();
            }

            ImGui::Separator();

            if (ImGui::BeginTable("Messages",
                                  2,
                                  ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Type",
                                        ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed,
                                        0.0f,
                                        MyItemColumnID::eType);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_NoSort, 0.0f, MyItemColumnID::eMessage);
                ImGui::TableSetupScrollFreeze(0, 1);

                ImGui::TableHeadersRow();
                // ImGuiUtilities::AlternatingRowsBackground();

                ImGui::TableNextRow();

                for (uint16_t i = 0; i < m_LogMessagesEnd; i++)
                {
                    const auto& level      = m_LogMessages[i].level;
                    const auto& messageStr = m_LogMessages[i].message;
                    const auto  levelFlag  = getLogLevelFlag(level);

                    if (m_Filter.IsActive())
                    {
                        if (m_Filter.PassFilter(messageStr.c_str()))
                        {
                            if (m_LogMessagesFilter & levelFlag)
                            {
                                ImGui::TableNextColumn();

                                ImGui::PushStyleColor(ImGuiCol_Text, getLogLevelColor(level));
                                const auto* levelIcon = getLogLevelIcon(level);
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() -
                                                     ImGui::CalcTextSize(levelIcon).x - ImGui::GetScrollX() -
                                                     2 * ImGui::GetStyle().ItemSpacing.x);
                                ImGui::TextUnformatted(levelIcon);

                                ImGui::PopStyleColor();

                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(messageStr.c_str());
                                ImGui::TableNextRow();
                            }
                        }
                    }
                    else
                    {
                        if (m_LogMessagesFilter & levelFlag)
                        {
                            ImGui::TableNextColumn();
                            ImGui::PushStyleColor(ImGuiCol_Text, getLogLevelColor(level));
                            const auto* levelIcon = getLogLevelIcon(level);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() -
                                                 ImGui::CalcTextSize(levelIcon).x - ImGui::GetScrollX() -
                                                 2 * ImGui::GetStyle().ItemSpacing.x);
                            ImGui::TextUnformatted(levelIcon);

                            ImGui::PopStyleColor();

                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(messageStr.c_str());
                            ImGui::TableNextRow();
                        }
                    }
                }
                if (m_RequestToBottom && ImGui::GetScrollMaxY() > 0)
                {
                    ImGui::SetScrollHereY(1.0f);
                    m_RequestToBottom = false;
                }
                ImGui::EndTable();
            }

            ImGui::End();
        }
    } // namespace editor
} // namespace vultra