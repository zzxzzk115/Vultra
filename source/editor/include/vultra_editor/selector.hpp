#pragma once

#include <vultra/core/base/uuid.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace vultra
{
    namespace editor
    {
        enum class SelectionCategory
        {
            eNone = 0,
            eEntity,
            eAsset
        };

        class Selector
        {
        public:
            static void                                select(SelectionCategory category, CoreUUID selectionId);
            static void                                unselect(SelectionCategory category, CoreUUID selectionId);
            static void                                unselect(CoreUUID selectionId);
            static void                                unselectAll();
            static void                                unselectAll(SelectionCategory category);
            static CoreUUID                            getSelection(SelectionCategory category, size_t index);
            static size_t                              getSelectionCount(SelectionCategory category);
            static CoreUUID                            getLastSelection(SelectionCategory category);
            inline static const std::vector<CoreUUID>& getSelections(SelectionCategory category)
            {
                return s_SelectionMap[category];
            }

            static bool isSelected(SelectionCategory category, CoreUUID selectionId);

            static SelectionCategory getLastSelectionCategory() { return s_LastSelectionCategory; }
            static CoreUUID          getLastSelectionUUID() { return s_LastSelectionUUID; }

        private:
            static std::unordered_map<SelectionCategory, std::vector<CoreUUID>> s_SelectionMap;
            static SelectionCategory                                            s_LastSelectionCategory;
            static CoreUUID                                                     s_LastSelectionUUID;
        };
    } // namespace editor
} // namespace vultra