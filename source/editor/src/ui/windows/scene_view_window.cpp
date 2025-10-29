#include "vultra_editor/ui/windows/scene_view_window.hpp"
#include "vultra_editor/selector.hpp"

#include <vultra/function/scenegraph/component_utils.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <IconsMaterialDesignIcons.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <imoguizmo/imoguizmo.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    namespace editor
    {
        SceneViewWindow::SceneViewWindow() : UIWindow("Scene View") {}

        SceneViewWindow::~SceneViewWindow() = default;

        void SceneViewWindow::onInit(rhi::RenderDevice& renderDevice)
        {
            UIWindow::onInit(renderDevice);

            // Default size
            m_SceneRenderTexture = rhi::Texture::Builder {}
                                       .setExtent({.width = 800, .height = 600})
                                       .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                       .setNumMipLevels(1)
                                       .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                                      rhi::ImageUsage::eTransferDst)
                                       .setupOptimalSampler(true)
                                       .build(renderDevice);
        }

        void SceneViewWindow::onImGui()
        {
            float displayScale = ImGui::GetStyle().FontScaleDpi;

            handleInput();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin(m_Name.c_str());
            ImGui::PopStyleVar();

            ImGui::BeginChild("ViewportToolbar", ImVec2(0, 30 * displayScale), false, ImGuiWindowFlags_NoScrollbar);
            drawToolbar();
            ImGui::EndChild();

            ImGui::BeginChild("ViewportMain", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
            m_IsWindowHovered = ImGui::IsWindowHovered();

            ImVec2 availSize = ImGui::GetContentRegionAvail();
            availSize.x      = std::max(availSize.x, 1.0f);
            availSize.y      = std::max(availSize.y, 1.0f);

            auto      viewportMinRegion = ImGui::GetWindowContentRegionMin();
            auto      viewportMaxRegion = ImGui::GetWindowContentRegionMax();
            auto      viewportOffset    = ImGui::GetWindowPos();
            glm::vec2 bounds0 = {viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y};
            glm::vec2 bounds1 = {viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y};

            if (m_SceneTexture)
            {
                // Resize render texture if needed
                if (static_cast<uint32_t>(availSize.x) != m_SceneRenderTexture.getExtent().width ||
                    static_cast<uint32_t>(availSize.y) != m_SceneRenderTexture.getExtent().height)
                {
                    recreateRenderTexture(static_cast<uint32_t>(availSize.x), static_cast<uint32_t>(availSize.y));
                }
            }
            else
            {
                recreateRenderTexture(static_cast<uint32_t>(availSize.x), static_cast<uint32_t>(availSize.y));
            }
            ImGui::Image(m_SceneTexture, availSize);

            // TODO: Scene View Selection as well
            auto lastSelectedEntityUUID = Selector::getLastSelection(SelectionCategory::eEntity);
            if (!lastSelectedEntityUUID.isNil())
            {
                m_SelectedEntity = m_LogicScene->getEntityWithCoreUUID(lastSelectedEntityUUID);
            }
            else
            {
                m_SelectedEntity = Entity {};
            }

            // TODO: Editor only camera
            auto camera           = m_LogicScene->getMainCamera();
            auto cameraComponent  = camera.getComponent<CameraComponent>();
            auto cameraTransform  = camera.getComponent<TransformComponent>();
            auto cameraView       = getCameraViewMatrix(cameraTransform);
            auto cameraProjection = getCameraProjectionMatrix(cameraComponent, false);

            // Gizmos
            if (m_SelectedEntity && m_GuizmoOperation != -1)
            {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();

                ImGuizmo::SetRect(bounds0.x, bounds0.y, bounds1.x - bounds0.x, bounds1.y - bounds0.y);

                // Selected entity transform
                auto& transformComponent = m_SelectedEntity.getComponent<TransformComponent>();
                auto  transform          = transformComponent.getTransform();

                // Snapping
                bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);

                float snapValue = 0.5f; // Snap to 0.5m for translation/scale

                // Snap to 45 degrees for rotation
                if (m_GuizmoOperation == ImGuizmo::OPERATION::ROTATE)
                {
                    snapValue = 45.0f;
                }

                float snapValues[3] = {snapValue, snapValue, snapValue};

                ImGuizmo::Manipulate(glm::value_ptr(cameraView),
                                     glm::value_ptr(cameraProjection),
                                     static_cast<ImGuizmo::OPERATION>(m_GuizmoOperation),
                                     m_GuizmoMode,
                                     glm::value_ptr(transform),
                                     nullptr,
                                     snap ? snapValues : nullptr);

                if (ImGuizmo::IsUsing())
                {
                    glm::vec3 rotation;
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform),
                                                          glm::value_ptr(transformComponent.position),
                                                          glm::value_ptr(rotation),
                                                          glm::value_ptr(transformComponent.scale));
                    transformComponent.setRotationEuler(rotation);
                }
            }

            ImGui::EndChild();

            // Begin overlay for imoguizmo
            float gizmoSize = 120.0f * displayScale;

            ImGui::SetNextWindowSize({gizmoSize, gizmoSize});
            ImGui::SetNextWindowPos({bounds0.x, bounds0.y});
            ImGui::BeginChild("ImoGuizmoOverlay",
                              ImVec2(gizmoSize, gizmoSize),
                              0,
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus);

            // Configure imoguizmo
            ImOGuizmo::config.axisLengthScale = 0.12f * std::sqrt(displayScale);

            // specify position and size of gizmo
            ImOGuizmo::SetRect(bounds0.x, bounds0.y, gizmoSize);

            // Begin imoguizmo frame, will call ImGui::Begin and ImGui::End internally to create a no decoration window
            // ImOGuizmo::BeginFrame();

            // set distance to pivot (-> activates interaction)
            // distance to pivot is actually the distance from camera to the world space origin (0,0,0)
            float pivotDistance = glm::length(cameraTransform.position);
            if (ImOGuizmo::DrawGizmo(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection), pivotDistance))
            {
                // decompose matrix and apply to camera transform
                glm::mat4 cameraWorld = glm::inverse(cameraView);

                glm::vec3 scale;
                glm::quat rotation;
                glm::vec3 translation;
                glm::vec3 skew {0, 0, 0};
                glm::vec4 perspective {0, 0, 0, 1};
                glm::decompose(cameraWorld, scale, rotation, translation, skew, perspective);

                camera.getComponent<TransformComponent>().position = translation;
                camera.getComponent<TransformComponent>().setRotation(rotation);
                camera.getComponent<TransformComponent>().scale = scale;
            }

            ImGui::EndChild();

            ImGui::End();
        }

        void SceneViewWindow::onRender(UIWindowRenderContext& ctx)
        {
            ctx.renderer->render(ctx.cb, &m_SceneRenderTexture, ctx.dt);
        }

        uint32_t SceneViewWindow::getViewportWidth() const { return m_SceneRenderTexture.getExtent().width; }

        uint32_t SceneViewWindow::getViewportHeight() const { return m_SceneRenderTexture.getExtent().height; }

        void SceneViewWindow::recreateRenderTexture(uint32_t width, uint32_t height)
        {
            m_SceneRenderTexture = rhi::Texture::Builder {}
                                       .setExtent({.width = width, .height = height})
                                       .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                       .setNumMipLevels(1)
                                       .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                                      rhi::ImageUsage::eTransferDst)
                                       .setupOptimalSampler(true)
                                       .build(*m_RenderDevice);

            if (m_SceneTexture)
            {
                imgui::removeTexture(*m_RenderDevice, m_SceneTexture);
            }
            m_SceneTexture = imgui::addTexture(m_SceneRenderTexture);
        }

        void SceneViewWindow::handleInput()
        {
            // Workaround for https://github.com/CedricGuillemet/ImGuizmo/issues/310
#if defined(NDEBUG)
            static bool isFirstTime = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_IsWindowHovered &&
                (!ImGuizmo::IsOver() || isFirstTime))
            {
                isFirstTime = false;
#else
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_IsWindowHovered && !ImGuizmo::IsOver())
            {
#endif
                if (m_SelectedEntity)
                {
                    // Single selection for now
                    Selector::unselectAll(SelectionCategory::eEntity);
                    Selector::select(SelectionCategory::eEntity, m_SelectedEntity.getCoreUUID());
                }
                else
                {
                    // https://github.com/CedricGuillemet/ImGuizmo/issues/133#issuecomment-708083755
                    auto selectedEntityUUID = Selector::getLastSelection(SelectionCategory::eEntity);
                    if (!selectedEntityUUID.isNil() && m_GuizmoOperation != -1)
                    {
                        auto entityCache = m_LogicScene->getEntityWithCoreUUID(selectedEntityUUID);
                        if (entityCache)
                        {
                            // TODO: Editor only camera
                            auto camera           = m_LogicScene->getMainCamera();
                            auto cameraComponent  = camera.getComponent<CameraComponent>();
                            auto cameraTransform  = camera.getComponent<TransformComponent>();
                            auto cameraView       = getCameraViewMatrix(cameraTransform);
                            auto cameraProjection = getCameraProjectionMatrix(cameraComponent, false);

                            glm::mat4 transform = entityCache.getComponent<TransformComponent>().getTransform();
                            transform           = glm::translate(transform, glm::vec3(0.0f, -10000.0f, 0.0f));
                            ImGuizmo::Manipulate(glm::value_ptr(cameraView),
                                                 glm::value_ptr(cameraProjection),
                                                 static_cast<ImGuizmo::OPERATION>(m_GuizmoOperation),
                                                 m_GuizmoMode,
                                                 glm::value_ptr(transform));
                        }
                    }

                    Selector::unselectAll(SelectionCategory::eEntity);
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Q) && m_IsWindowHovered && !ImGuizmo::IsUsing())
            {
                m_GuizmoOperation = -1;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_W) && m_IsWindowHovered && !ImGuizmo::IsUsing())
            {
                m_GuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_E) && m_IsWindowHovered && !ImGuizmo::IsUsing())
            {
                m_GuizmoOperation = ImGuizmo::OPERATION::ROTATE;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_R) && m_IsWindowHovered && !ImGuizmo::IsUsing())
            {
                m_GuizmoOperation = ImGuizmo::OPERATION::SCALE;
            }
        }

        void SceneViewWindow::drawToolbar()
        {
            float windowHeight = ImGui::GetWindowHeight();

            float centerHeight = windowHeight * 0.5f;
            float offsetY      = (windowHeight - centerHeight) * 0.5f;
            ImGui::SetCursorPos(ImVec2(0, offsetY));

            ImGui::Indent();
            ImGui::Text("");
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

            const ImVec4 selectedColor = ImVec4(0.18f, 0.46f, 0.98f, 1.0f);

            bool selected = false;
            {
                selected = m_GuizmoOperation == -1;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, selectedColor);
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_HAND_BACK_RIGHT))
                    m_GuizmoOperation = -1;
                if (selected)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Grab the screen to move the viewport. (Q)");
                }
            }
            ImGui::SameLine();

            {
                selected = m_GuizmoOperation == ImGuizmo::OPERATION::TRANSLATE;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, selectedColor);
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_ARROW_ALL))
                    m_GuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
                if (selected)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Translate the selected object. (W)");
                }
            }
            ImGui::SameLine();

            {
                selected = m_GuizmoOperation == ImGuizmo::OPERATION::ROTATE;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, selectedColor);
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_ROTATE_3D_VARIANT))
                    m_GuizmoOperation = ImGuizmo::OPERATION::ROTATE;
                if (selected)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Rotate the selected object. (E)");
                }
            }
            ImGui::SameLine();

            {
                selected = m_GuizmoOperation == ImGuizmo::OPERATION::SCALE;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, selectedColor);
                ImGui::SameLine();
                if (ImGui::Button(ICON_MDI_ARROW_EXPAND_ALL))
                    m_GuizmoOperation = ImGuizmo::OPERATION::SCALE;
                if (selected)
                    ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Scale the selected object. (R)");
                }
            }
            ImGui::SameLine();

            ImGui::PopStyleColor();
        }
    } // namespace editor
} // namespace vultra