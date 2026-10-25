#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scene/scene_types.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void requireMoveRejected(SceneTree& tree)
    {
        try
        {
            SceneTree moved(std::move(tree));
        }
        catch (const std::logic_error&)
        {
            return;
        }
        throw std::runtime_error("Moving a running world was accepted");
    }

    void behaviorProperties()
    {
        SceneTree                     world(std::make_unique<Node>("Properties"));
        NodeBehavior                  previous;
        NodeBehavior                  replacement;
        std::array<PropertySchema, 1> schema;
        schema[0].name                  = "gain";
        schema[0].jsonPath              = "/gain";
        schema[0].kind                  = PropertyKind::eFloat;
        schema[0].defaultValue          = 1.0f;
        float                     value = 2;
        const NodePropertyBinding binding {schema,
                                           &value,
                                           [](void* state, std::string_view) -> PropertyValue
                                           {
                                               return *static_cast<float*>(state);
                                           },
                                           [](void* state, std::string_view, const PropertyValue& next)
                                           {
                                               *static_cast<float*>(state) = std::get<float>(next);
                                           }};
        world.attachBehavior(world.root(), previous, &binding);
        require(world.readProperty(world.root().id(), "gain") == PropertyValue(2.0f) &&
                    world.propertySchema(world.root().id(), "gain").defaultValue == PropertyValue(1.0f),
                "Behavior fields lost their storage or declaration defaults");
        world.writeProperty(world.root().id(), "gain", 3.0f);
        require(value == 3, "Behavior field wrote a shadow copy");
        float next                = 4;
        auto  nextBinding         = binding;
        nextBinding.state         = &next;
        auto collision            = schema;
        collision[0].name         = "name";
        collision[0].jsonPath     = "/name";
        auto invalidBinding       = nextBinding;
        invalidBinding.properties = collision;
        bool rejected             = false;
        try
        {
            world.replaceBehavior(world.root(), previous, replacement, &invalidBinding);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && world.readProperty(world.root().id(), "gain") == PropertyValue(3.0f),
                "A colliding field replaced the previous behavior");
        auto&        sibling = world.addChild(world.root(), std::make_unique<Node>("Sibling"));
        NodeBehavior siblingPrevious;
        NodeBehavior siblingReplacement;
        world.attachBehavior(sibling, siblingPrevious, &binding);
        const std::array invalidGroup {
            SceneTree::BehaviorBinding {world.root(), &previous, replacement, &nextBinding},
            SceneTree::BehaviorBinding {sibling, &siblingPrevious, siblingReplacement, &invalidBinding}};
        rejected = false;
        try
        {
            world.bindBehaviors(invalidGroup);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && world.registry().get<SceneProcess>(world.root().entity()).behavior == &previous &&
                    world.readProperty(world.root().id(), "gain") == PropertyValue(3.0f) &&
                    world.registry().get<SceneProcess>(sibling.entity()).behavior == &siblingPrevious,
                "An invalid second binding partially published a behavior group");
        const std::array duplicateGroup {
            SceneTree::BehaviorBinding {world.root(), &previous, replacement, &nextBinding},
            SceneTree::BehaviorBinding {world.root(), &previous, siblingReplacement, &nextBinding}};
        rejected = false;
        try
        {
            world.bindBehaviors(duplicateGroup);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && world.registry().get<SceneProcess>(world.root().entity()).behavior == &previous,
                "A duplicate target partially published a behavior group");
        world.detachBehavior(sibling, siblingPrevious);
        world.replaceBehavior(world.root(), previous, replacement, &nextBinding);
        world.detachBehavior(world.root(), previous);
        require(world.readProperty(world.root().id(), "gain") == PropertyValue(4.0f),
                "Retiring the old behavior removed replacement fields");
        world.detachBehavior(world.root(), replacement);
        require(world.propertyList(world.root().id()).size() == world.objectType(world.root().id()).properties.size(),
                "Detached fields remain visible to property discovery");
    }

    void behaviorReferences()
    {
        SceneTree             world(std::make_unique<Node3D>("References"));
        auto&                 target  = world.addChild(world.root(), std::make_unique<Node3D>("Target"));
        auto&                 foreign = world.addChild(world.root(), std::make_unique<Node3D>("Other module"));
        NodeBehavior          ownerBehavior;
        NodeBehavior          targetBehavior;
        NodeBehavior          foreignBehavior;
        const PropertyClassId required {"project/ships", "Beacon"};
        const std::array      targetClasses {PropertyClassId {"project/ships", "SpecialBeacon"}, required};
        const std::array      foreignClasses {PropertyClassId {"project/other", "Beacon"}};
        PropertySchema        schema;
        schema.name                     = "partner";
        schema.jsonPath                 = "/partner";
        schema.kind                     = PropertyKind::eReference;
        schema.referenceType            = "Node3D";
        schema.referenceCategory        = "node";
        schema.referenceClass           = required;
        schema.defaultValue             = PropertyReference {};
        PropertyValue             value = PropertyReference {target.idInScene().value};
        const NodePropertyBinding owner {{&schema, 1},
                                         &value,
                                         [](void* state, std::string_view)
                                         {
                                             return *static_cast<PropertyValue*>(state);
                                         },
                                         [](void* state, std::string_view, const PropertyValue& next)
                                         {
                                             *static_cast<PropertyValue*>(state) = next;
                                         }};
        auto                      targetBinding = owner;
        targetBinding.properties                = {};
        targetBinding.classes                   = targetClasses;
        auto foreignBinding                     = targetBinding;
        foreignBinding.classes                  = foreignClasses;
        const std::array bindings {SceneTree::BehaviorBinding {world.root(), nullptr, ownerBehavior, &owner},
                                   SceneTree::BehaviorBinding {target, nullptr, targetBehavior, &targetBinding},
                                   SceneTree::BehaviorBinding {foreign, nullptr, foreignBehavior, &foreignBinding}};
        world.validatePropertyValue(schema, value, nullptr, bindings);
        require(world.behaviorClasses(target.id()).empty(), "Prospective validation published an attachment");
        world.bindBehaviors(bindings);
        require(world.behaviorClasses(target.id()).size() == 2, "Behavior ancestry was not published");
        world.writeProperty(world.root().id(), "partner", value);
        bool rejected = false;
        try
        {
            world.writeProperty(world.root().id(), "partner", PropertyReference {foreign.idInScene().value});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && value == PropertyValue(PropertyReference {target.idInScene().value}),
                "A same-named class from another module passed a typed reference write");
        world.detachBehavior(target, targetBehavior);
        rejected = false;
        try
        {
            world.validatePropertyValue(schema, value);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "A retired behavior still satisfied its class reference contract");
        world.detachBehavior(world.root(), ownerBehavior);
        world.detachBehavior(foreign, foreignBehavior);
    }

    void componentTransforms()
    {
        auto root    = std::make_unique<Node3D>("root");
        auto initial = glm::mat4(1);
        initial[3].x = 3;
        root->setLocalTransform(initial);
        SceneTree tree(std::move(root));
        auto&     spatial     = tree.addChild(tree.root(), std::make_unique<Node3D>("spatial"));
        auto&     child       = tree.addChild(spatial, std::make_unique<Node3D>("child"));
        auto&     plain       = tree.addChild(tree.root(), std::make_unique<Node>("plain"));
        auto&     independent = tree.addChild(plain, std::make_unique<Node3D>("independent"));
        SceneTree other(std::make_unique<Node3D>("other"));
        require(tree.entity(other.root().id()) == entt::null && !tree.find(other.root().id()),
                "Foreign world identity resolved into a colliding entity slot");
        require(tree.registry().get<SceneNode>(spatial.entity()).id == spatial.id() &&
                    &tree.registry().get<SceneTransform>(spatial.entity()).local == &spatial.localTransform(),
                "Node and ECS query do not share the authoritative transform");
        require(static_cast<const Node3D&>(tree.root()).localTransform() == initial,
                "Attachment did not transfer the authored transform into component storage");

        std::vector<SceneTransformUpdate> updates;
        const auto                        view = tree.registry().view<const SceneNode, const SceneTransform>();
        for (const auto& [entity, node, transform] : view.each())
        {
            auto local = transform.local;
            local[3].x += 2;
            updates.push_back({node.id, local});
        }
        require(updates.size() == 4, "Spatial component query included a non-spatial node or missed a node");
        const auto revision = tree.changes().transforms;
        tree.setLocalTransforms(updates);
        require(tree.changes().transforms == revision + 1 && spatial.localTransform()[3].x == 2 &&
                    child.globalTransform()[3].x == 9 && independent.globalTransform()[3].x == 2,
                "Batch edits did not reach Node hierarchy transforms or change notifications");
        tree.setLocalTransforms(updates);
        require(tree.changes().transforms == revision + 1, "Unchanged batch invalidated render consumers");

        auto reject = [&](const SceneTransformUpdate& invalid)
        {
            const auto beforeTransform = spatial.localTransform();
            const auto beforeRevision  = tree.changes().transforms;
            auto       edited          = spatial.localTransform();
            edited[3].x                = 99;
            const SceneTransformUpdate batch[] {{spatial.id(), edited}, invalid};
            bool                       failed = false;
            try
            {
                tree.setLocalTransforms(batch);
            }
            catch (const std::invalid_argument&)
            {
                failed = true;
            }
            require(failed && spatial.localTransform() == beforeTransform &&
                        tree.changes().transforms == beforeRevision,
                    "Rejected transform batch partially changed world state");
        };
        reject({other.root().id(), glm::mat4(1)});
        reject({plain.id(), glm::mat4(1)});
        auto invalid  = glm::mat4(1);
        invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
        reject({spatial.id(), invalid});
        invalid       = glm::mat4(1);
        invalid[0][3] = 1;
        reject({spatial.id(), invalid});

        const auto retiredEntity = spatial.entity();
        const auto spatialId     = spatial.id();
        const auto childId       = child.id();
        auto       detached      = tree.remove(spatial);
        require(!tree.registry().valid(retiredEntity) && !tree.find(spatialId) && !tree.find(childId) &&
                    detached->entity() == entt::null && child.entity() == entt::null &&
                    static_cast<Node3D&>(*detached).localTransform()[3].x == 2,
                "Detachment left live entities or lost owned transform state");
        auto& attached = static_cast<Node3D&>(other.addChild(other.root(), std::move(detached)));
        require(attached.localTransform()[3].x == 2 && other.find(childId) == &child && !tree.find(childId),
                "Subtree transfer did not isolate world identity and transforms");
        reject({spatialId, glm::mat4(1)});
        const auto attachedEntity = attached.entity();
        SceneTree  moved(std::move(other));
        require(attached.tree() == &moved && attached.entity() == attachedEntity &&
                    moved.find(spatialId) == &attached && attached.localTransform()[3].x == 2,
                "Stopped-world move invalidated component storage or facade identity");
        auto        restored     = SceneTree::parse(moved.serialize());
        const auto* restoredNode = dynamic_cast<const Node3D*>(restored.find(attached.idInScene()));
        require(restoredNode && restoredNode->localTransform() == attached.localTransform() &&
                    restoredNode->id() != attached.id(),
                "Component transforms did not preserve stable scene data with fresh runtime identities");
    }

    class Probe final : public Node
    {
    public:
        Probe(std::string name, std::vector<std::string>& events) :
            Node(std::move(name)),
            m_Events(events)
        {
        }

        void onEnterTree() override
        {
            record("enter");
        }

        void onReady() override
        {
            record("ready");
        }

        void onPhysicsProcess(double delta) override
        {
            require(delta == tree()->fixedDelta(), "Fixed update received a variable delta");
            record("fixed");
        }

        void onProcess(double delta) override
        {
            lastDelta = delta;
            record("process");
            if (action)
            {
                action();
            }
        }

        void onExitTree() override
        {
            require(isInsideTree() && tree()->find(id()) == this, "Exit callback lost its live node identity");
            record("exit");
        }

        std::function<void()> action;
        double                lastDelta = 0;

    private:
        void record(const char* event)
        {
            m_Events.push_back(name() + ":" + event);
        }

        std::vector<std::string>& m_Events;
    };

    void lifecycleAndClock()
    {
        std::vector<std::string> events;
        SceneTree                tree(std::make_unique<Probe>("root", events));
        auto&                    a = tree.addChild(tree.root(), std::make_unique<Probe>("a", events));
        auto&                    b = tree.addChild(a, std::make_unique<Probe>("b", events));
        auto&                    c = tree.addChild(tree.root(), std::make_unique<Probe>("c", events));
        require(!tree.root().isInsideTree(), "Authored scene executed while being constructed");
        tree.start();
        require(events == std::vector<std::string> {"root:enter",
                                                    "a:enter",
                                                    "b:enter",
                                                    "c:enter",
                                                    "b:ready",
                                                    "a:ready",
                                                    "c:ready",
                                                    "root:ready"},
                "Tree lifecycle order differs from parent-first enter/children-first ready");
        events.clear();
        c.setProcessPriority(-1);
        tree.advance(tree.fixedDelta() * 2.5);
        require(tree.physicsFrame() == 2 && events.size() == 12 && events[0] == "c:fixed" && events[8] == "c:process",
                "Fixed accumulation or stable priority ordering failed");
        require(c.lastDelta == tree.fixedDelta() * 2.5, "Variable process delta was changed");
        events.clear();
        tree.setPaused(true);
        a.setProcessMode(ProcessMode::eWhenPaused);
        c.setProcessMode(ProcessMode::eAlways);
        tree.advance(0);
        require(events == std::vector<std::string> {"c:process", "a:process", "b:process"},
                "Pause modes did not inherit from the nearest explicit ancestor");
        events.clear();
        tree.stepPaused();
        require(tree.paused() && tree.physicsFrame() == 3 &&
                    events == std::vector<std::string> {"c:fixed", "root:fixed"},
                "Paused stepping changed pause state or ran paused-only nodes");
        tree.setPaused(false);
        a.setProcessMode(ProcessMode::eInherit);
        tree.setTimeScale(2);
        const auto frame = tree.physicsFrame();
        tree.advance(0.025);
        require(tree.physicsFrame() == frame + 3 && c.lastDelta == 0.05, "Scaled fixed/variable clock diverged");
        events.clear();
        tree.stop();
        SceneTree offline(std::make_unique<Node>("offline"));
        offline.advance(1);
        require(offline.physicsFrame() == 60, "One second of offline execution dropped a fixed tick");
        requireMoveRejected(offline);
        require(offline.running(), "Rejected move invalidated the running world's execution scope");
        offline.stop();
        NodeBehavior behavior;
        offline.attachBehavior(offline.root(), behavior);
        bool moveRejected = false;
        try
        {
            offline = SceneTree(std::make_unique<Node>("replacement"));
        }
        catch (const std::logic_error&)
        {
            moveRejected = true;
        }
        require(moveRejected && offline.root().name() == "offline", "Replacing a borrowed world retired host state");
        offline.detachBehavior(offline.root(), behavior);
        SceneTree moved(std::move(offline));
        require(moved.root().tree() == &moved, "Stopped world move left node tree pointers behind");
        require(events == std::vector<std::string> {"c:exit", "b:exit", "a:exit", "root:exit"},
                "Exit did not visit children before parents");
        events.clear();
        tree.start();
        require(events == std::vector<std::string> {"root:enter", "a:enter", "b:enter", "c:enter"},
                "Re-entry reran ready without a new node");
        tree.stop();
    }

    class Rig final : public Node3D
    {
    public:
        using Node3D::Node3D;

        std::string_view typeName() const override
        {
            return "Test.Rig";
        }

        CameraSettings settings;
    };

    void typeConstruction()
    {
        SceneTypeCatalog     types;
        const ObjectTypeInfo rigType {"Test.Rig", {}, &types.objects().type("Node3D"), "node"};
        const SceneNodeType  record {rigType,
                                     [](std::string name, NodeId id, std::string_view state) -> std::unique_ptr<Node>
                                     {
                                        auto rig = std::make_unique<Rig>(std::move(name), id);
                                        deserializeProperties(cameraSettingsType(), state, &rig->settings);
                                        return rig;
                                     },
                                     [](const Node& node)
                                     {
                                        return serializeProperties(cameraSettingsType(),
                                                                   &dynamic_cast<const Rig&>(node).settings);
                                     }};
        types.add(record);
        require(types.objects().type("Test.Rig").isA("Node") && rigType.isA("Node3D") && !rigType.isA("Camera3D"),
                "Explicit type ancestry does not match native node inheritance");
        SceneTree authored(std::make_unique<Node>("Root"));
        auto&     rig = authored.addChild(authored.root(), std::make_unique<Rig>("Rig"));
        rig.setOwner(&authored.root());
        rig.settings.verticalFov = 0.7f;
        rig.setProcessMode(ProcessMode::eAlways);
        rig.setProcessPriority(-5);
        const auto text     = authored.serialize(types);
        auto       reopened = SceneTree::parse(text, types);
        auto&      restored = dynamic_cast<Rig&>(*reopened.find(rig.idInScene()));
        require(restored.id() != rig.id() && restored.settings == rig.settings &&
                    restored.owner() == &reopened.root() && restored.processMode() == ProcessMode::eAlways &&
                    restored.processPriority() == -5 && reopened.serialize(types) == text,
                "Project node factory did not preserve typed state, owner, process settings or persistent identity");
        require(reopened.serialize() == text, "Loaded world lost its explicitly installed project type catalog");
        const auto expectFailure = [](const auto& operation)
        {
            bool rejected = false;
            try
            {
                operation();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, "Invalid type/scene input was accepted");
        };
        expectFailure(
            [&]
            {
                types.add(record);
            });
        expectFailure(
            [&]
            {
                SceneTree::parse(text);
            });
        expectFailure(
            [&]
            {
                types.objects().type("Unknown");
            });
        auto document                    = nlohmann::json::parse(text);
        document["root"]["process_mode"] = 0.5;
        expectFailure(
            [&]
            {
                SceneTree::parse(document.dump(), types);
            });
        document["root"]["process_mode"]     = 0;
        document["root"]["process_priority"] = uint64_t(UINT32_MAX);
        expectFailure(
            [&]
            {
                SceneTree::parse(document.dump(), types);
            });
        const ObjectTypeInfo missingBase {"Missing", {}, nullptr, "node"};
        const ObjectTypeInfo invalidType {"Invalid", {}, &missingBase, "node"};
        const SceneNodeType  invalidRecord {invalidType, record.create, record.serialize};
        expectFailure(
            [&]
            {
                types.add(invalidRecord);
            });
        const SceneNodeType wrongFactory {rigType,
                                          [](std::string name, NodeId id, std::string_view)
                                          {
                                              return std::unique_ptr<Node>(std::make_unique<Node>(std::move(name), id));
                                          },
                                          record.serialize};
        SceneTypeCatalog    other;
        other.add(wrongFactory);
        expectFailure(
            [&]
            {
                SceneTree::parse(text, other);
            });
        types.remove(record);
        expectFailure(
            [&]
            {
                SceneTree::parse(text, types);
            });
    }

    void deferredAndFailure()
    {
        std::vector<std::string> events;
        SceneTree                tree(std::make_unique<Probe>("root", events));
        auto&                    killer   = tree.addChild(tree.root(), std::make_unique<Probe>("killer", events));
        auto&                    target   = tree.addChild(tree.root(), std::make_unique<Probe>("target", events));
        auto&                    child    = tree.addChild(target, std::make_unique<Probe>("child", events));
        const auto               targetId = target.id();
        const auto               childId  = child.id();
        killer.action                     = [&]
        {
            target.queueFree();
            target.queueFree();
            require(target.isQueuedForDeletion() && child.isQueuedForDeletion(), "Deletion did not mark descendants");
            tree.deferAddChild(killer.id(), std::make_unique<Probe>("new", events));
            bool rejected = false;
            try
            {
                tree.remove(target);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            require(rejected, "Immediate ownership mutation was allowed during process dispatch");
        };
        tree.start();
        events.clear();
        tree.advance(0);
        require(!tree.find(targetId) && !tree.find(childId) && killer.children().size() == 1,
                "Deferred changes did not retire IDs or attach unique ownership");
        require(events == std::vector<std::string> {"root:process",
                                                    "killer:process",
                                                    "new:enter",
                                                    "new:ready",
                                                    "child:exit",
                                                    "target:exit"},
                "Queued deletion called a later process callback or exited in the wrong order");
        killer.action = [&]
        {
            tree.stop();
        };
        events.clear();
        tree.advance(0);
        require(!tree.running() && events.back() == "root:exit", "Callback stop did not retire at a safe boundary");
        killer.action = []
        {
            throw std::runtime_error("intentional callback failure");
        };
        bool failed = false;
        try
        {
            tree.advance(0);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        require(failed && !tree.running() && !killer.isInsideTree(), "Callback failure kept a partial world running");
        killer.action = {};
        tree.advance(0);
        require(tree.running(), "Explicit restart after callback failure was not possible");
        tree.stop();
    }
} // namespace

int main()
{
    lifecycleAndClock();
    deferredAndFailure();
    typeConstruction();
    componentTransforms();
    behaviorProperties();
    behaviorReferences();
}
