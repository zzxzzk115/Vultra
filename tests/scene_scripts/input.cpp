#include <vultra/scripting/native_script.hpp>

namespace
{
    using namespace vultra;
    using namespace vultra::scripting;

    class InputBase : public NativeScript
    {
    public:
        int64_t     inputs    = 0;
        int64_t     presses   = 0;
        int64_t     unhandled = 0;
        int64_t     expired   = 0;
        int64_t     updates   = 0;
        std::string typed;

        void onInput(const InputEvent& event) override
        {
            ++inputs;
            checkPrevious();
            m_Previous = event;
            if (event.kind() == InputEventKind::eKey && event.code() == uint32_t(KeyCode::eF2))
            {
                if (!actor().inputEnabled())
                {
                    throw std::runtime_error("Input was already disabled");
                }
                actor().setInputEnabled(false);
                return;
            }
            if (event.kind() == InputEventKind::eKey && event.code() == uint32_t(KeyCode::eF3))
            {
                if (!actor().unhandledInputEnabled())
                {
                    throw std::runtime_error("Unhandled input was already disabled");
                }
                actor().setUnhandledInputEnabled(false);
                return;
            }
            if (event.kind() == InputEventKind::eKey && event.code() == uint32_t(KeyCode::eDelete))
            {
                actor().queueFree();
                return;
            }
            if (inputAction(event, "jump").pressed)
            {
                ++presses;
                auto position = actor().position();
                position.x += 1;
                actor().setPosition(position);
                acceptInput(event);
            }
            if (event.kind() == InputEventKind::eText)
            {
                typed += event.text();
            }
        }

        void onProcess(float) override
        {
            ++updates;
            checkPrevious();
        }

    private:
        void checkPrevious()
        {
            if (m_Previous)
            {
                bool rejected = false;
                try
                {
                    acceptInput(*m_Previous);
                }
                catch (const std::logic_error&)
                {
                    rejected = true;
                }
                if (!rejected)
                {
                    throw std::runtime_error("Stored native input event remained active");
                }
                ++expired;
            }
        }

        std::optional<InputEvent> m_Previous;
    };

    class InputController final : public InputBase
    {
    public:
        void onUnhandledInput(const InputEvent& event) override
        {
            ++unhandled;
            auto position = actor().position();
            if (event.kind() == InputEventKind::eMouseButton && event.code() == uint32_t(MouseCode::eLeft) &&
                event.action() == InputAction::ePress)
            {
                position.y += 2;
            }
            if (event.kind() == InputEventKind::eMouseMotion)
            {
                position.z += event.deltaX() * 0.01f;
            }
            if (event.kind() == InputEventKind::eMouseScroll)
            {
                position.z -= event.deltaY() * 0.25f;
            }
            actor().setPosition(position);
        }
    };

    const auto kBase = scriptClass<InputBase>("InputBase",
                                              {scriptProperty("inputs", &InputBase::inputs),
                                               scriptProperty("presses", &InputBase::presses),
                                               scriptProperty("unhandled", &InputBase::unhandled),
                                               scriptProperty("expired", &InputBase::expired),
                                               scriptProperty("updates", &InputBase::updates),
                                               scriptProperty("typed", &InputBase::typed)},
                                              "Node3D");
} // namespace

VULTRA_NATIVE_MODULE(kBase, (scriptClass<InputController, InputBase>("InputController", kBase)))
