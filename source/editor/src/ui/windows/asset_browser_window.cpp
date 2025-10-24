#include "vultra_editor/ui/windows/asset_browser_window.hpp"
#include "vultra_editor/asset/asset_database.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        AssetBrowserWindow::AssetBrowserWindow() : UIWindow("Asset Browser")
        {
            m_AssetRoot  = AssetDatabase::get()->getAssetRootDir();
            m_CurrentDir = m_AssetRoot;
        }

        AssetBrowserWindow::~AssetBrowserWindow() = default;

        void AssetBrowserWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 region            = ImGui::GetContentRegionAvail();
            const float  splitterThickness = 4.0f;
            const float  minRatio          = 0.1f;

            // Clamp ratio
            m_LeftPanelRatio = std::clamp(m_LeftPanelRatio, minRatio, 1.0f - minRatio);

            float leftWidth  = region.x * m_LeftPanelRatio;
            float rightWidth = region.x - leftWidth - splitterThickness;

            // Auto-sync focus between right panel and left tree
            if (m_FocusToCurrent)
            {
                // We only use this as a flag; actual opening happens in drawDirectoryRecursive
                m_FocusToCurrent = false;
            }

            // Left: FileSystem Tree
            ImGui::BeginChild("##LeftPanel", ImVec2(leftWidth, 0), true);
            {
                if (exists(m_AssetRoot))
                {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    if (ImGui::TreeNodeEx(m_AssetRoot.filename().string().c_str(),
                                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
                    {
                        drawDirectoryRecursive(m_AssetRoot);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    ImGui::TextColored(
                        ImVec4(1, 0.3f, 0.3f, 1), "Asset root not found:\n%s", m_AssetRoot.string().c_str());
                }
            }
            ImGui::EndChild();

            // Middle: Splitter
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 0.9f));

            ImGui::Button("##Splitter", ImVec2(splitterThickness, -1));
            if (ImGui::IsItemActive())
            {
                m_LeftPanelRatio += ImGui::GetIO().MouseDelta.x / region.x;
                m_LeftPanelRatio = std::clamp(m_LeftPanelRatio, minRatio, 1.0f - minRatio);
            }
            ImGui::PopStyleColor(3);

            // Right: Details Panel
            ImGui::SameLine(0, 0);
            ImGui::BeginChild("##RightPanel", ImVec2(rightWidth, 0), true);
            drawRightPanel();
            ImGui::EndChild();

            ImGui::End();
        }

        void AssetBrowserWindow::drawDirectoryRecursive(const std::filesystem::path& dirPath)
        {
            using namespace std::filesystem;

            for (const auto& entry : directory_iterator(dirPath))
            {
                if (!entry.is_directory())
                    continue; // Skip files

                const auto&       path = entry.path();
                const std::string name = path.filename().string();

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

                if (path == m_CurrentDir)
                    flags |= ImGuiTreeNodeFlags_Selected;

                // Determine whether this folder is on the path to the current directory
                bool isParentOfCurrent = path.string().rfind(m_CurrentDir.string()) == 0;

                // If it is on the path, force open this node
                if (isParentOfCurrent)
                {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                }
                bool open = ImGui::TreeNodeEx(name.c_str(), flags);

                // Handle click (select only when not toggling)
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                {
                    m_CurrentDir     = path;
                    m_FocusToCurrent = false; // user clicked inside tree, no sync needed
                }

                // Recursively draw subfolders
                if (open)
                {
                    drawDirectoryRecursive(path);
                    ImGui::TreePop();
                }
            }
        }

        void AssetBrowserWindow::drawRightPanel()
        {
            if (!exists(m_CurrentDir))
            {
                ImGui::TextDisabled("No valid directory selected.");
                return;
            }

            // Filter input
            ImGui::TextUnformatted("Filter:");
            ImGui::SameLine();
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##Filter", m_FilterBuffer, sizeof(m_FilterBuffer));
            ImGui::PopItemWidth();

            // Breadcrumb navigation
            {
                // Relative path from asset root
                std::filesystem::path relativePath = std::filesystem::relative(m_CurrentDir, m_AssetRoot);

                // Root button
                if (ImGui::SmallButton(m_AssetRoot.filename().string().c_str()))
                {
                    m_CurrentDir = m_AssetRoot;
                }

                // Breadcrumb buttons
                std::filesystem::path accum = m_AssetRoot;
                for (const auto& part : relativePath)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(">");
                    ImGui::SameLine();

                    accum /= part;

                    if (ImGui::SmallButton(part.string().c_str()))
                    {
                        m_CurrentDir     = accum;
                        m_FocusToCurrent = true;
                    }
                }
            }

            ImGui::Separator();

            // Icon grid
            const float iconSize    = 64.0f;
            const float cellPadding = 8.0f;
            float       panelWidth  = ImGui::GetContentRegionAvail().x;
            int         columns     = static_cast<int>(panelWidth / (iconSize + cellPadding));
            if (columns < 1)
                columns = 1;
            ImGui::Columns(columns, nullptr, false);

            std::vector<std::filesystem::path> filesToShow;

            std::string filter = m_FilterBuffer;
            if (!filter.empty())
            {
                // --- Recursive search when filter is active ---
                std::function<void(const std::filesystem::path&)> searchRecursive =
                    [&](const std::filesystem::path& dir) {
                        for (const auto& entry : std::filesystem::directory_iterator(dir))
                        {
                            const auto&       path = entry.path();
                            const std::string name = path.filename().string();

                            if (entry.is_directory())
                            {
                                searchRecursive(path);
                            }
                            else
                            {
                                if (name.find(filter) != std::string::npos)
                                    filesToShow.push_back(path);
                            }
                        }
                    };

                searchRecursive(m_CurrentDir);
            }
            else
            {
                // --- Normal listing of current directory ---
                for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDir))
                    filesToShow.push_back(entry.path());
            }

            // Draw items
            for (const auto& path : filesToShow)
            {
                std::string name  = path.filename().string();
                bool        isDir = std::filesystem::is_directory(path);

                ImGui::PushID(name.c_str());

                // Folder / file icon
                if (isDir)
                    ImGui::Button(ICON_MDI_FOLDER, ImVec2(iconSize, iconSize));
                else
                    ImGui::Button(ICON_MDI_FILE, ImVec2(iconSize, iconSize));

                // Tooltip
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", name.c_str());

                // Double-click to open folder
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && isDir)
                {
                    m_CurrentDir     = path;
                    m_FocusToCurrent = true;
                }

                // Name under icon
                ImGui::TextWrapped("%s", name.c_str());
                ImGui::NextColumn();
                ImGui::PopID();
            }

            ImGui::Columns(1);
        }
    } // namespace editor
} // namespace vultra