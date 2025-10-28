#include "vultra_editor/selector.hpp"

namespace vultra
{
    namespace editor
    {
        std::unordered_map<SelectionCategory, std::vector<CoreUUID>> Selector::s_SelectionMap;
        SelectionCategory Selector::s_LastSelectionCategory = SelectionCategory::eNone;
        CoreUUID          Selector::s_LastSelectionUUID;

        void Selector::select(SelectionCategory category, CoreUUID selectionId)
        {
            auto& selections = s_SelectionMap[category];
            if (std::find(selections.begin(), selections.end(), selectionId) != selections.end())
            {
                return;
            }

            selections.emplace_back(selectionId);

            s_LastSelectionCategory = category;
            s_LastSelectionUUID     = selectionId;
        }

        void Selector::unselect(SelectionCategory category, CoreUUID selectionId)
        {
            auto& selections = s_SelectionMap[category];
            auto  it         = std::find(selections.begin(), selections.end(), selectionId);
            if (it == selections.end())
            {
                return;
            }

            selections.erase(it);

            // Reset last selection if it matches the unselected one
            if (s_LastSelectionCategory == category && s_LastSelectionUUID == selectionId)
            {
                s_LastSelectionCategory = SelectionCategory::eNone;
                s_LastSelectionUUID     = CoreUUID {};
            }
        }

        void Selector::unselect(CoreUUID selectionId)
        {
            for (auto& [category, selections] : s_SelectionMap)
            {
                auto it = std::find(selections.begin(), selections.end(), selectionId);
                if (it == selections.end())
                {
                    continue;
                }

                selections.erase(it);
                break;
            }

            // Reset last selection if it matches the unselected one
            if (s_LastSelectionUUID == selectionId)
            {
                s_LastSelectionCategory = SelectionCategory::eNone;
                s_LastSelectionUUID     = CoreUUID {};
            }
        }

        void Selector::unselectAll()
        {
            for (auto& [category, selections] : s_SelectionMap)
            {
                selections.clear();
            }

            s_LastSelectionCategory = SelectionCategory::eNone;
            s_LastSelectionUUID     = CoreUUID {};
        }

        void Selector::unselectAll(SelectionCategory category)
        {
            s_SelectionMap[category].clear();

            if (s_LastSelectionCategory == category)
            {
                s_LastSelectionCategory = SelectionCategory::eNone;
                s_LastSelectionUUID     = CoreUUID {};
            }
        }

        CoreUUID Selector::getSelection(SelectionCategory category, size_t index)
        {
            return s_SelectionMap[category][index];
        }

        size_t Selector::getSelectionCount(SelectionCategory category) { return s_SelectionMap[category].size(); }

        CoreUUID Selector::getLastSelection(SelectionCategory category)
        {
            if (s_LastSelectionCategory == category)
            {
                return s_LastSelectionUUID;
            }

            CoreUUID result {};

            auto selections = getSelections(category);
            if (!selections.empty())
            {
                result = selections[selections.size() - 1];
            }

            return result;
        }

        bool Selector::isSelected(SelectionCategory category, CoreUUID selectionId)
        {
            const auto& selections = s_SelectionMap[category];
            return std::find(selections.begin(), selections.end(), selectionId) != selections.end();
        }
    } // namespace editor
} // namespace vultra