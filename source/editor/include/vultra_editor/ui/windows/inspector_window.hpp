#pragma once

#include "vultra_editor/event/select_event.hpp"
#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/scenegraph/components.hpp>
#include <vultra/function/scenegraph/entity.hpp>

namespace vultra
{
    namespace editor
    {
        class SceneGraphWindow;
        class AssetBrowserWindow;

        class InspectorWindow final : public UIWindow
        {
        public:
            InspectorWindow();
            ~InspectorWindow() override;

            void onImGui() override;

            void listen(SceneGraphWindow* sceneGraphWindow, AssetBrowserWindow* assetBrowserWindow);

        private:
            static void drawEntityProperties(Entity& entity);

            static void drawComponentName(NameComponent& comp);
            static void drawComponentFlags(EntityFlagsComponent& comp);
            static void drawComponentTransform(TransformComponent& comp);

            static void drawAssetProperties(const CoreUUID& assetUUID);

        private:
            CoreUUID   m_InspectUUID;
            SelectType m_InspectType;
        };
    } // namespace editor
} // namespace vultra