#include "vultra_editor/ui/windows/asset_browser_window.hpp"
#include "vultra_editor/asset/asset_database.hpp"
#include "vultra_editor/selector.hpp"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

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

            // Clear selection when switching directory
            if (!m_SelectedPath.empty())
            {
                if (m_SelectedPath.parent_path() != m_CurrentDir)
                    m_SelectedPath.clear();
            }

            // Filter input
            ImGui::TextUnformatted("Filter:");
            ImGui::SameLine();
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##Filter", m_FilterBuffer, sizeof(m_FilterBuffer));
            ImGui::PopItemWidth();

            ImGui::Separator();
            ImGui::TextUnformatted("Icon Size:");
            ImGui::SameLine();
            ImGui::PushItemWidth(-1);
            ImGui::SliderFloat("##IconSize", &m_IconSize, m_MinIconSize, m_MaxIconSize, "%.0f px"); // Icon scaling
            ImGui::PopItemWidth();

            bool listMode = (m_IconSize < m_ListThreshold); // Switch to list mode when below threshold

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
            const float cellPadding = 8.0f;
            float       iconSize    = m_IconSize;
            float       panelWidth  = ImGui::GetContentRegionAvail().x;

            int columns = 1;
            if (!listMode)
            {
                columns = static_cast<int>(panelWidth / (iconSize + cellPadding));
                if (columns < 1)
                    columns = 1;
            }
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
                std::string name  = path.filename().stem().string();
                bool        isDir = std::filesystem::is_directory(path);

                ImGui::PushID(name.c_str());

                // Thumbnail / Icon
                if (isDir)
                {
                    ImGui::Button(ICON_MDI_FOLDER, ImVec2(iconSize, iconSize));
                }
                else
                {
                    // Only read meta files (only imported assets have meta files)
                    if (path.extension() != ".vmeta")
                    {
                        ImGui::PopID();
                        continue;
                    }

                    auto uuid       = AssetDatabase::get()->getMetaUUID(path);
                    auto assetEntry = AssetDatabase::get()->getRegistry().lookup(uuid);

                    if (assetEntry.type == vasset::VAssetType::eTexture)
                    {
                        auto* texId      = AssetDatabase::get()->getImGuiTextureByUUID(uuid);
                        auto  imguiTexId = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texId));

                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

                        if (imguiTexId)
                            ImGui::Image(imguiTexId, ImVec2(iconSize, iconSize));
                        else
                            ImGui::Button(ICON_MDI_FILE_IMAGE, ImVec2(iconSize, iconSize));

                        ImGui::PopStyleVar(2);
                    }
                    else if (assetEntry.type == vasset::VAssetType::eMesh)
                    {
                        ImGui::Button(ICON_MDI_CUBE, ImVec2(iconSize, iconSize));
                    }
                    else if (assetEntry.type == vasset::VAssetType::eMaterial)
                    {
                        ImGui::Button(ICON_MDI_FORMAT_PAINT, ImVec2(iconSize, iconSize));
                    }
                    else
                    {
                        ImGui::Button(ICON_MDI_FILE, ImVec2(iconSize, iconSize));
                    }
                }

                ImVec2 iconMin = ImGui::GetItemRectMin();
                ImVec2 iconMax = ImGui::GetItemRectMax();

                if (listMode)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(name.c_str());
                }
                else
                {
                    ImGui::TextWrapped("%s", name.c_str());
                }

                ImVec2 textMin = ImGui::GetItemRectMin();
                ImVec2 textMax = ImGui::GetItemRectMax();

                ImVec2 cellMin(ImMin(iconMin.x, textMin.x), ImMin(iconMin.y, textMin.y));
                ImVec2 cellMax(ImMax(iconMax.x, textMax.x), ImMax(iconMax.y, textMax.y));

                ImGui::SetItemAllowOverlap();

                // Hover / Click Check
                bool hovered = ImGui::IsMouseHoveringRect(cellMin, cellMax);
                bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered;

                // Select highlight
                bool isSelected = (m_SelectedPath == path);
                if (hovered || isSelected)
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    ImU32       color    = isSelected ?
                                               ImGui::GetColorU32(ImVec4(0.2f, 0.4f, 0.7f, 0.4f)) :
                                               ImGui::GetColorU32(ImVec4(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered).x,
                                                                ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered).y,
                                                                ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered).z,
                                                                0.2f));
                    drawList->AddRectFilled(cellMin, cellMax, color, 4.0f);
                }

                if (clicked)
                {
                    // Select file
                    selectPath(path);
                }

                // Double-click to open directory
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && isDir)
                {
                    m_CurrentDir     = path;
                    m_FocusToCurrent = true;
                    m_SelectedPath.clear();
                }

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    selectPath(path);
                    ImGui::OpenPopup("asset_browser_context");
                }

                if (ImGui::BeginPopup("asset_browser_context"))
                {
                    if (ImGui::MenuItem("Reimport"))
                    {
                        // TODO: Reimport logic
                    }
                    if (ImGui::MenuItem("Create"))
                    {
                        // TODO: Create logic
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        // TODO: Delete logic
                    }
                    if (ImGui::MenuItem("Rename"))
                    {
                        // TODO: Rename logic
                    }
                    ImGui::EndPopup();
                }

                ImGui::NextColumn();
                ImGui::PopID();
            }

            ImGui::Columns(1);
        }

        void AssetBrowserWindow::selectPath(const std::filesystem::path& path)
        {
            if (exists(path))
            {
                m_SelectedPath = path;

                if (!std::filesystem::is_directory(path))
                {
                    // Single selection for now
                    Selector::unselectAll(SelectionCategory::eAsset);
                    Selector::select(SelectionCategory::eAsset, AssetDatabase::get()->getMetaUUID(path));
                }

                if (path.parent_path() != m_CurrentDir)
                {
                    m_CurrentDir     = path.parent_path();
                    m_FocusToCurrent = true;
                }
            }
        }
    } // namespace editor
} // namespace vultra