#include "vultra_editor/scripts/editor_camera_script.hpp"
#include "vultra_editor/selector.hpp"

#include <vultra/core/input/input.hpp>
#include <vultra/core/os/window.hpp>
#include <vultra/function/scenegraph/component_utils.hpp>
#include <vultra/function/scenegraph/entity.hpp>

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        void EditorCameraScriptInstance::onImGui()
        {
            float deltaTime = ImGui::GetIO().DeltaTime;

            auto& camera     = m_OwnerEntity->getComponent<CameraComponent>();
            auto& transform  = m_OwnerEntity->getComponent<TransformComponent>();
            auto  viewMatrix = getCameraViewMatrix(transform);

            glm::vec2 mousePosition = Input::getMousePosition();

            // FreeMove
            if (!m_IsGrabMoveValid)
            {
                if (Input::getMouseButton(MouseCode::eRight) && m_IsWindowHovered)
                {
                    m_LastFrameMousePosition = mousePosition;
                    m_IsFreeMoveValid        = true;
                }

                // When pressing mouse button right:
                //  Allow camera rotate around and WASD to move the camera
                if (Input::getMouseButton(MouseCode::eRight) && m_IsFreeMoveValid)
                {
                    // Relative mouse mode
                    os::Window::getActiveWindow().setMouseRelativeMode(true);

                    // Block imgui mouse events
                    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;

                    auto offset = Input::getMousePositionDelta();
                    offset *= m_Sensitivity * 2.0f;

                    auto cameraRotationEuler = transform.getRotationEuler();

                    float yaw   = cameraRotationEuler.y;
                    float pitch = cameraRotationEuler.x;

                    yaw -= offset.x;
                    pitch -= offset.y;

                    if (pitch > 89.0f)
                    {
                        pitch = 89.0f;
                    }

                    if (pitch < -89.0f)
                    {
                        pitch = -89.0f;
                    }

                    cameraRotationEuler.y = yaw;
                    cameraRotationEuler.x = pitch;

                    transform.setRotationEuler(cameraRotationEuler);
                    m_LastFrameMousePosition = mousePosition;

                    glm::vec3 forward = getForward(cameraRotationEuler);
                    glm::vec3 up      = glm::vec3(0, 1, 0);

                    glm::vec3 right = glm::normalize(glm::cross(forward, up));

                    float speed = m_BaseSpeed * 100.0f;

                    // Speed Up
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
                    {
                        speed *= 2;
                    }

                    // Forward
                    if (ImGui::IsKeyDown(ImGuiKey_W))
                    {
                        transform.position += forward * speed * deltaTime;
                    }

                    // Backward
                    if (ImGui::IsKeyDown(ImGuiKey_S))
                    {
                        transform.position -= forward * speed * deltaTime;
                    }

                    // Left
                    if (ImGui::IsKeyDown(ImGuiKey_A))
                    {
                        transform.position -= right * speed * deltaTime;
                    }

                    // Right
                    if (ImGui::IsKeyDown(ImGuiKey_D))
                    {
                        transform.position += right * speed * deltaTime;
                    }

                    // Upward
                    if (ImGui::IsKeyDown(ImGuiKey_Q))
                    {
                        transform.position += up * speed * deltaTime;
                    }

                    // Downward
                    if (ImGui::IsKeyDown(ImGuiKey_E))
                    {
                        transform.position -= up * speed * deltaTime;
                    }
                }

                if (Input::getMouseButtonUp(MouseCode::eRight) && m_IsFreeMoveValid)
                {
                    // Re-enable ImGui mouse events
                    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

                    // Disable relative mouse mode
                    os::Window::getActiveWindow().setMouseRelativeMode(false);

                    m_IsFreeMoveValid = false;
                }
            }

            // GrabMove
            if (m_IsGrabMoveEnabled)
            {
                if (Input::getMouseButton(MouseCode::eLeft) && m_IsWindowHovered)
                {
                    m_LastFrameMousePosition = mousePosition;
                    m_IsGrabMoveValid        = true;
                }

                // When pressing mouse button left:
                // drag mouse to grab the view
                if (Input::getMouseButton(MouseCode::eLeft) && m_IsGrabMoveValid)
                {
                    // Show drag cursor
                    os::Window::getActiveWindow().setCursor(os::Window::CursorType::eGrab);

                    // Block imgui mouse events
                    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;

                    auto offset = mousePosition - m_LastFrameMousePosition;
                    offset *= m_Sensitivity;

                    // Assuming transform is the camera's transform
                    auto& transform = m_OwnerEntity->getComponent<TransformComponent>();

                    // Calculate the right and up vectors of the view matrix
                    glm::vec3 right = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
                    glm::vec3 up    = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

                    // Calculate the translation based on offset
                    glm::vec3 translation = -right * offset.x + up * offset.y;

                    // Apply translation to the camera position
                    transform.position += translation * 0.1f;

                    m_LastFrameMousePosition = mousePosition;
                }

                if (Input::getMouseButtonUp(MouseCode::eLeft) && m_IsGrabMoveValid)
                {
                    // Re-enable ImGui mouse events
                    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

                    // Restore cursor
                    os::Window::getActiveWindow().setCursor(os::Window::CursorType::eArrow);

                    m_IsGrabMoveValid = false;
                }
            }

            if (m_IsWindowHovered)
            {
                // Zoom in / out
                auto cameraRotationEuler = transform.getRotationEuler();

                glm::vec3 forward = getForward(cameraRotationEuler);

                float yOffset = ImGui::GetIO().MouseWheel * m_BaseSpeed * 5.0f;
                transform.position += yOffset * forward;

                // Focus
                // if (ImGui::IsKeyDown(ImGuiKey_F))
                // {
                //     auto selectedEntityUUID = Selector::getLastSelection(SelectionCategory::eEntity);
                //     if (!selectedEntityUUID.isNil())
                //     {
                //         auto selectedEntity =
                //         SceneManager::getActiveScene()->GetEntityWithCoreUUID(selectedEntityUUID); if
                //         (selectedEntity)
                //         {
                //             auto selectedEntityTransform = selectedEntity.GetComponent<TransformComponent>();
                //             transform.position           = selectedEntityTransform.position - 10.0f * forward;
                //         }
                //     }
                // }
            }
        }
    } // namespace editor
} // namespace vultra