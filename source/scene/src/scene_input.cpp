#include <vultra/scene/scene_tree.hpp>

#include <array>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace vultra
{
    uint64_t SceneTree::inputEventSerial() const
    {
        if (!m_InputEvent)
        {
            throw std::invalid_argument("No active scene input event");
        }
        return m_InputSerial;
    }

    const InputEvent& SceneTree::inputEvent(uint64_t serial) const
    {
        if (!m_InputEvent || serial != m_InputSerial)
        {
            throw std::invalid_argument("Scene input event has expired");
        }
        return *m_InputEvent;
    }

    void SceneTree::setInputHandled(uint64_t serial)
    {
        (void)inputEvent(serial);
        m_InputEvent->consumed = true;
    }

    void SceneTree::routeInput(std::span<InputEvent> events, InputCapture capture)
    {
        requireIdle();
        for (auto& event : events)
        {
            if (!m_Running)
            {
                break;
            }
            if (inputCaptured(event, capture))
            {
                continue;
            }
            m_Dispatching = true;
            try
            {
                // Lua integers are signed; reserve zero and never wrap an event borrow into a later event.
                if (m_InputSerial == uint64_t(std::numeric_limits<int64_t>::max()))
                {
                    throw std::overflow_error("Scene input serial exhausted");
                }
                ++m_InputSerial;
                m_InputEvent = &event;
                for (const bool unhandled : std::array {false, true})
                {
                    // Processing priority does not reorder input. Structural edits are deferred until both phases end.
                    for (const auto handle : std::views::reverse(orderedEntities()))
                    {
                        if (event.consumed || m_StopRequested)
                        {
                            break;
                        }
                        auto* node = m_Registry->get<SceneNode>(handle).node;
                        if (!node || !node->canProcess())
                        {
                            continue;
                        }
                        if (unhandled ? !node->unhandledInputEnabled() : !node->inputEnabled())
                        {
                            continue;
                        }
                        if (unhandled)
                        {
                            node->onUnhandledInput(event);
                        }
                        else
                        {
                            node->onInput(event);
                        }
                        if (event.consumed || m_StopRequested || !node->canProcess())
                        {
                            continue;
                        }
                        auto* behavior = node->processState().behavior;
                        if (!behavior)
                        {
                            continue;
                        }
                        if (unhandled && node->unhandledInputEnabled())
                        {
                            behavior->unhandledInput(event);
                        }
                        else if (!unhandled && node->inputEnabled())
                        {
                            behavior->input(event);
                        }
                    }
                }
                m_InputEvent  = nullptr;
                m_Dispatching = false;
                if (m_StopRequested)
                {
                    stop();
                }
                else
                {
                    flushDeferred();
                }
            }
            catch (...)
            {
                m_InputEvent  = nullptr;
                m_Dispatching = false;
                stop();
                throw;
            }
        }
    }
} // namespace vultra
