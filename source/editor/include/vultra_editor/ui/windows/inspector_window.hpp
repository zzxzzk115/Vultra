#pragma once

#include "vultra_editor/selector.hpp"
#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/scenegraph/components.hpp>
#include <vultra/function/scenegraph/entity.hpp>

namespace vultra
{
    namespace editor
    {
        class InspectorWindow final : public UIWindow
        {
        public:
            InspectorWindow();
            ~InspectorWindow() override;

            void onImGui() override;

        private:
            static void drawEntityProperties(Entity& entity);

            static void drawComponentName(NameComponent& comp);
            static void drawComponentFlags(EntityFlagsComponent& comp);
            static void drawComponentTransform(TransformComponent& comp);

            static void drawAssetProperties(const CoreUUID& assetUUID);
        };
    } // namespace editor
} // namespace vultra