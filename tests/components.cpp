#include <vultra/api/scene_bridge.hpp>
#include <vultra/scene/component_properties.generated.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <string>

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

    template<class Callback>
    void reject(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        throw std::runtime_error("Invalid component operation was accepted");
    }

    struct Speed
    {
        float value = 1;
    };

    const ObjectTypeInfo& speedType()
    {
        static const auto properties = []
        {
            auto value     = cameraSettingsType().property("nearPlane");
            value.name     = "value";
            value.label    = "Speed";
            value.drawerId = "Speed.value";
            value.jsonPath = "/value";
            value.get      = [](const void* component) -> PropertyValue
            {
                return static_cast<const Speed*>(component)->value;
            };
            value.set = [](void* component, const PropertyValue& data)
            {
                static_cast<Speed*>(component)->value = std::get<float>(data);
            };
            value.defaultValue = 1.0f;
            return std::array {value};
        }();
        static const ObjectTypeInfo type {"Speed", properties, nullptr, "component"};
        return type;
    }

    const void* getSpeed(const entt::registry& registry, entt::entity entity)
    {
        return registry.try_get<Speed>(entity);
    }

    void writeSpeed(SceneTree& scene, entt::entity entity, const PropertyInfo& property, const PropertyValue& value)
    {
        auto candidate = scene.registry().get<Speed>(entity);
        property.write(&candidate, value);
        if (candidate.value < 0)
        {
            throw std::invalid_argument("Speed must be nonnegative");
        }
        scene.registry().patch<Speed>(entity,
                                      [&](Speed& speed)
                                      {
                                          speed = candidate;
                                      });
        scene.markChanged(SceneChange::eMetadata);
    }

    const SceneComponentType kSpeed {speedType(), &getSpeed, &writeSpeed};

    struct BorrowProbe
    {
        SceneTypeLifetime* lifetime;
        bool*              observed;

        ~BorrowProbe()
        {
            *observed = lifetime->catalogs == 2;
        }
    };

    void registrationLifetime()
    {
        SceneTypeLifetime        lifetime;
        bool                     observed = false;
        const ObjectTypeInfo     schema {"BorrowProbe", {}, nullptr, "component"};
        const SceneComponentType record {schema,
                                         [](const entt::registry& registry, entt::entity entity) -> const void*
                                         {
                                             return registry.try_get<BorrowProbe>(entity);
                                         },
                                         {},
                                         &lifetime};
        SceneTypeCatalog         types;
        types.add(record);
        {
            SceneTree  scene(std::make_unique<Node>("Borrow world"), types);
            const auto entity = scene.createEntity();
            scene.registry().emplace<BorrowProbe>(scene.entity(entity), &lifetime, &observed);
        }
        require(observed && lifetime.catalogs == 1, "Registration borrow retired before the EnTT pool destructor");
        observed = false;
        {
            SceneTree scene(std::make_unique<Node>("Pending world"), types);
            scene.deferCommand(
                [probe = std::make_unique<BorrowProbe>(&lifetime, &observed)](SceneTree&) mutable
                {
                    probe.reset();
                });
            scene = SceneTree(std::make_unique<Node>("Replacement"));
            require(observed, "World replacement retired registration borrows before pending callbacks");
        }
    }

    void customComponents()
    {
        SceneTypeCatalog types;
        types.add(kSpeed);
        reject(
            [&]
            {
                types.add(kSpeed);
            });
        reject(
            [&]
            {
                types.add(SceneComponentType {speedType(), &getSpeed, nullptr});
            });
        SceneTree  world(std::make_unique<Node3D>("World"), types);
        SceneTree  foreign(std::make_unique<Node>("Foreign"));
        const auto actor = world.createEntity("Runtime");
        world.registry().emplace<Speed>(world.entity(actor));
        require(!world.findObject(actor) && world.readComponentProperty(actor, "Speed", "value") == PropertyValue(1.0f),
                "Component inspection requires a Node facade or shadows native storage");
        const auto revision = world.changes().metadata;
        world.writeComponentProperty(actor, "Speed", "value", 3.0f);
        require(world.registry().get<Speed>(world.entity(actor)).value == 3 && world.changes().metadata == revision + 1,
                "Component edit did not publish the native component and its revision");
        reject(
            [&]
            {
                world.writeComponentProperty(actor, "Speed", "value", -1.0f);
            });
        reject(
            [&]
            {
                world.writeComponentProperty(actor, "Speed", "value", true);
            });
        reject(
            [&]
            {
                world.readComponentProperty(actor, "Speed", "missing");
            });
        reject(
            [&]
            {
                world.readComponentProperty(world.root().id(), "Speed", "value");
            });
        reject(
            [&]
            {
                world.readComponentProperty(foreign.root().id(), "Speed", "value");
            });
        require(world.registry().get<Speed>(world.entity(actor)).value == 3 && world.changes().metadata == revision + 1,
                "Failed component edits partially mutated storage or notified consumers");
        reject(
            [&]
            {
                foreign.types().componentType("Speed");
            });
        types.remove(kSpeed);
        require(world.readComponentProperty(actor, "Speed", "value") == PropertyValue(3.0f),
                "World lost its copied project component registration");
        world.destroyEntity(actor);
        reject(
            [&]
            {
                world.readComponentProperty(actor, "Speed", "value");
            });
    }

    void builtinComponents()
    {
        SceneTree   world(std::make_unique<Node3D>("World"));
        auto&       camera = world.addChild(world.root(), std::make_unique<Camera3D>("Camera"));
        auto&       light  = world.addChild(world.root(), std::make_unique<Light3D>("Light", RenderLightKind::ePoint));
        const auto& descriptor  = sceneCameraType().property("settings.nearPlane");
        const auto  description = nlohmann::json::parse(sceneDescribeType(world, "SceneCamera"));
        require(sceneCameraType().category == "component" &&
                    std::string_view(descriptor.jsonPath) == "/settings/near" &&
                    std::get<float>(descriptor.defaultValue) == CameraSettings {}.nearPlane &&
                    sceneHierarchyType().properties.empty() && description.at("version") == 1 &&
                    description.at("category") == "component" && description.at("properties").size() == 3,
                "Generated component metadata lost nested settings or exposed engine-owned hierarchy");
        const auto cameraRevision = world.changes().camera;
        world.writeComponentProperty(camera.id(), "SceneCamera", "settings.nearPlane", 0.25f);
        require(camera.settings().nearPlane == 0.25f && world.changes().camera == cameraRevision + 1,
                "Component camera edit did not update the Node view and renderer revision");
        reject(
            [&]
            {
                world.writeComponentProperty(camera.id(), "SceneCamera", "settings.farPlane", 0.1f);
            });
        require(camera.settings().nearPlane == 0.25f && camera.settings().farPlane == CameraSettings {}.farPlane &&
                    world.changes().camera == cameraRevision + 1,
                "Invalid camera component candidate changed active data");
        world.writeComponentProperty(light.id(), "SceneLight", "settings.intensity", 4.0f);
        world.writeComponentProperty(light.id(), "SceneLight", "kind", PropertyEnum {int64_t(RenderLightKind::eSpot)});
        require(light.settings().intensity == 4 && light.lightKind() == RenderLightKind::eSpot,
                "Component light settings/kind did not reach the native component");
        reject(
            [&]
            {
                world.writeComponentProperty(light.id(), "SceneLight", "kind", PropertyEnum {99});
            });
        const auto transformRevision = world.changes().transforms;
        auto       matrix            = glm::mat4 {1};
        matrix[3][0]                 = 3;
        world.writeComponentProperty(camera.id(), "SceneTransform", "local", matrix);
        require(camera.localTransform() == matrix && camera.globalTransform() == matrix &&
                    world.changes().transforms == transformRevision + 1,
                "Component transform did not invalidate the derived global matrix");
        reject(
            [&]
            {
                world.writeComponentProperty(camera.id(), "SceneTransform", "local", glm::mat4 {0});
            });
        reject(
            [&]
            {
                world.readComponentProperty(camera.id(), "SceneTransform", "global");
            });
        reject(
            [&]
            {
                world.writeComponentProperty(camera.id(), "SceneHierarchy", "parent", PropertyReference {});
            });
        reject(
            [&]
            {
                world.writeComponentProperty(camera.id(), "SceneNode", "name", std::string {});
            });
        world.writeComponentProperty(camera.id(), "SceneNode", "name", std::string("Renamed"));
        require(camera.name() == "Renamed", "Component identity metadata did not update the Node view");
        const auto  data     = world.serialize();
        auto        copy     = SceneTree::parse(data);
        const auto* restored = static_cast<Camera3D*>(copy.find(camera.idInScene()));
        require(restored && restored->settings() == camera.settings() && restored->localTransform() == matrix &&
                    copy.find(light.idInScene())->typeName() == "SpotLight3D",
                "Authored persistence lost component edits");
    }

    void referenceBoundaries()
    {
        ProjectManifest project;
        const auto      first  = project.addAsset("first.obj");
        const auto      second = project.addAsset("second.glb");
        const auto      image  = project.addAsset("image.png");
        SceneTree       world(std::make_unique<Node>("World"));
        const auto      actor    = world.createEntity("Mesh");
        auto&           material = world.addMaterial(std::make_unique<MaterialResource>("Paint"));
        auto&           mesh     = world.registry().emplace<SceneMesh>(world.entity(actor), first);
        mesh.materials.push_back({0, material.resourceId()});
        const auto* slots    = mesh.materials.data();
        const auto  revision = world.changes();
        reject(
            [&]
            {
                world.writeComponentProperty(actor, "SceneMesh", "model", PropertyReference {second.value});
            });
        reject(
            [&]
            {
                world.writeComponentProperty(actor, "SceneMesh", "model", PropertyReference {image.value}, &project);
            });
        reject(
            [&]
            {
                world.writeComponentProperty(actor,
                                             "SceneMesh",
                                             "model",
                                             PropertyReference {material.resourceId().value},
                                             &project);
            });
        require(mesh.model == first && mesh.materials.data() == slots && world.changes() == revision,
                "Rejected component references changed native storage or revisions");
        world.writeComponentProperty(actor, "SceneMesh", "model", PropertyReference {second.value}, &project);
        require(mesh.model == second && mesh.materials.data() == slots &&
                    mesh.materials.front().material == material.resourceId() &&
                    world.changes().structure == revision.structure + 1,
                "Component model edit replaced material slots or omitted topology invalidation");
        const auto unchanged = world.changes();
        world.writeComponentProperty(actor, "SceneMesh", "model", PropertyReference {second.value}, &project);
        require(world.changes() == unchanged, "An unchanged component edit notified consumers");
    }

    void componentAbi()
    {
        SceneTree  world(std::make_unique<Node>("World"));
        const auto actor = world.createEntity("Runtime camera");
        world.registry().emplace<SceneCamera>(world.entity(actor));
        SceneAccess            access {.scene = &world, .serial = 7, .active = true};
        const VultraSceneFrame frame {&access, 7};
        const auto&            api   = sceneApi();
        uint64_t               count = 0;
        require(api.component_count(frame, actor.value, &count) == VULTRA_STATUS_OK && count == 4,
                "Component ABI cannot enumerate a runtime-only entity");
        const char* name = nullptr;
        uint64_t    size = 0;
        require(api.component_name(frame, actor.value, count, &name, &size) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Component ABI accepted an out-of-range index");
        require(
            api.write_component_property(frame, actor.value, "SceneCamera", 11, "settings.nearPlane", 18, "0.5", 3) ==
                    VULTRA_STATUS_OK &&
                world.registry().get<SceneCamera>(world.entity(actor)).settings.nearPlane == 0.5f,
            "Generated component ABI did not update native ECS state");
        require(api.read_component_property(frame,
                                            actor.value,
                                            "SceneCamera",
                                            11,
                                            "settings.nearPlane",
                                            18,
                                            nullptr,
                                            0,
                                            &size) == VULTRA_STATUS_OK,
                "Generated component ABI cannot read its edit");
        std::string value(size, '\0');
        require(api.read_component_property(frame,
                                            actor.value,
                                            "SceneCamera",
                                            11,
                                            "settings.nearPlane",
                                            18,
                                            value.data(),
                                            value.size(),
                                            &size) == VULTRA_STATUS_OK &&
                    value == "0.5",
                "Component ABI read differs from C++");
        require(
            api.write_component_property(frame, actor.value, "SceneCamera", 11, "settings.farPlane", 17, "0.1", 3) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                api.read_component_property(frame,
                                            actor.value,
                                            "SceneLight",
                                            10,
                                            "settings.intensity",
                                            18,
                                            nullptr,
                                            0,
                                            &size) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Component ABI accepted an invalid candidate or missing component");
        access.active = false;
        require(api.component_count(frame, actor.value, &count) == VULTRA_STATUS_INVALID_FRAME,
                "Component ABI retained an expired borrow");
        access.active = true;
        ++access.serial;
        require(api.component_count(frame, actor.value, &count) == VULTRA_STATUS_INVALID_FRAME,
                "Component ABI accepted a stale frame serial");
        world.destroyEntity(actor);
        require(api.component_count({&access, access.serial}, actor.value, &count) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Component ABI accepted a retired entity identity");
    }
} // namespace

int main()
{
    registrationLifetime();
    customComponents();
    builtinComponents();
    referenceBoundaries();
    componentAbi();
}
