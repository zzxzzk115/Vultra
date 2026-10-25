#include <vultra/assets/material_properties.generated.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/render_properties.generated.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_types.hpp>
#include <vultra/servers/rendering/builtin/render_properties.generated.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<typename Callback>
    std::string requireFailure(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::exception& error)
        {
            return error.what();
        }
        throw std::runtime_error("Invalid property operation was accepted");
    }

    const nlohmann::json& describedProperty(const nlohmann::json& description, std::string_view name)
    {
        for (const auto& property : description.at("properties"))
        {
            if (property.at("name") == name)
            {
                return property;
            }
        }
        throw std::runtime_error("Type description omitted a property: " + std::string(name));
    }
} // namespace

int main()
try
{
    using namespace vultra;
    ObjectTypeCatalog    first;
    ObjectTypeCatalog    second;
    const ObjectTypeInfo unnamed {nullptr, {}};
    requireFailure(
        [&]
        {
            first.add(unnamed);
        });
    first.add(cameraSettingsType());
    second.add(cameraSettingsType());
    requireFailure(
        [&]
        {
            first.add(cameraSettingsType());
        });
    first.remove(cameraSettingsType());
    requireFailure(
        [&]
        {
            first.type("CameraSettings");
        });
    require(&second.type("CameraSettings") == &cameraSettingsType(), "Type catalogs share registration state");
    requireFailure(
        [&]
        {
            first.remove(cameraSettingsType());
        });

    for (const auto kind : {PropertyKind::eReference, PropertyKind::eArray})
    {
        auto reference              = nodeType().property("ownerId");
        reference.kind              = kind;
        reference.elementKind       = PropertyKind::eReference;
        reference.referenceCategory = "component";
        const ObjectTypeInfo invalidReference {"ComponentReference", std::span(&reference, 1), &nodeType(), "node"};
        SceneTypeCatalog     types;
        const auto&          base = types.nodeType("Node");
        const SceneNodeType  invalidNode {invalidReference, base.create, base.serialize};
        requireFailure(
            [&]
            {
                types.add(invalidNode);
            });
    }

    requireFailure(
        [&]
        {
            serializeProperties(cameraSettingsType(), nullptr);
        });
    CameraSettings camera;
    const auto&    near = cameraSettingsType().property("nearPlane");
    near.write(&camera, 0.1f);
    require(camera.nearPlane == 0.1f && std::get<float>(near.read(&camera)) == 0.1f,
            "Named property read/write did not reach the actual field");
    require(std::get<float>(near.defaultValue) == CameraSettings {}.nearPlane,
            "Generated defaults differ from C++ defaults");
    requireFailure(
        [&]
        {
            near.write(&camera, 0.2);
        });
    requireFailure(
        [&]
        {
            near.write(&camera, std::numeric_limits<float>::infinity());
        });
    requireFailure(
        [&]
        {
            near.read(nullptr);
        });
    requireFailure(
        [&]
        {
            cameraSettingsType().property("missing");
        });
    const auto accepted = camera;
    requireFailure(
        [&]
        {
            deserializeProperties(cameraSettingsType(), R"({"near":0.3,"far":true})", &camera);
        });
    require(camera == accepted, "Failed JSON decoding partially mutated the settings");
    requireFailure(
        [&]
        {
            deserializeProperties(cameraSettingsType(), R"({"neer":0.3})", &camera);
        });
    deserializeProperties(cameraSettingsType(), R"({"near":0.25})", &camera);
    require(camera.nearPlane == 0.25f && camera.farPlane == CameraSettings {}.farPlane,
            "Missing serialized fields did not use C++ defaults");
    Camera3D node("Camera");
    auto     invalid  = camera;
    invalid.nearPlane = invalid.farPlane;
    requireFailure(
        [&]
        {
            node.setSettings(invalid);
        });
    require(node.settings() == CameraSettings {}, "Property descriptors bypassed the scene validation boundary");

    MaterialParameters material;
    material.baseRed        = 0.4f;
    material.coatWeight     = 0.5f;
    const auto materialJson = serializeProperties(materialParametersType(), &material);
    const auto data         = nlohmann::json::parse(materialJson);
    require(data.at("base_color").size() == 4 && data.at("coat_weight") == 0.5f,
            "Generated JSON paths lost the scene material layout");
    MaterialParameters copy;
    deserializeProperties(materialParametersType(), materialJson, &copy);
    require(serializeProperties(materialParametersType(), &copy) == materialJson, "Material round trip changed values");
    LightSettings light;
    requireFailure(
        [&]
        {
            deserializeProperties(lightSettingsType(), R"({"color":[1,1]})", &light);
        });

    RenderSettings renderer;
    renderer.path             = RenderPath::eReferencePathTracing;
    renderer.lightColor       = {0.2f, 0.4f, 0.8f};
    renderer.ambientColor     = {0.15f, 0.1f, 0.05f};
    renderer.meshShading      = true;
    renderer.shadowFilter     = ShadowFilter::ePcss;
    renderer.shadowResolution = 2048;
    RenderSettings reopened;
    deserializeProperties(renderSettingsType(), serializeProperties(renderSettingsType(), &renderer), &reopened);
    require(reopened.path == renderer.path && reopened.lightColor == renderer.lightColor &&
                reopened.ambientColor == renderer.ambientColor && reopened.meshShading &&
                reopened.shadowFilter == renderer.shadowFilter &&
                reopened.shadowResolution == renderer.shadowResolution,
            "Renderer round trip lost uninspected or non-scalar fields");
    requireFailure(
        [&]
        {
            renderSettingsType().property("path").write(&renderer, PropertyEnum {90});
        });
    requireFailure(
        [&]
        {
            renderSettingsType().property("shadowResolution").write(&renderer, uint64_t {1} << 32);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"shadowResolution":-1})", &renderer);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"shadowResolution":1.5})", &renderer);
        });
    requireFailure(
        [&]
        {
            deserializeProperties(renderSettingsType(), R"({"lightColor":[1,2,3,4]})", &renderer);
        });
    SceneTree world(std::make_unique<Node>("Root"));
    auto&     child = world.addChild(world.root(), std::make_unique<Node3D>("Child"));
    require(node3DType().isA("Node") && node3DType().properties.size() > nodeType().properties.size(),
            "Generated object inheritance lost effective properties");
    const auto nodeDescription = nlohmann::json::parse(serializeTypeDescription(node3DType()));
    require(nodeDescription.at("version") == 1 && nodeDescription.at("base") == "Node" &&
                nodeDescription.at("category") == "node" &&
                describedProperty(nodeDescription, "name").at("kind") == "string" &&
                describedProperty(nodeDescription, "localTransform").at("default").size() == 16,
            "Type description lost inherited metadata or matrix defaults");
    const auto  modelDescription = nlohmann::json::parse(serializeTypeDescription(meshInstance3DType()));
    const auto& modelProperty    = describedProperty(modelDescription, "model");
    require(modelProperty.at("reference") ==
                    nlohmann::json {{"category", "asset"}, {"type", "ModelResource"}, {"nullable", false}} &&
                modelProperty.at("default").is_null(),
            "Required constructor references cannot be described without a live object");
    const auto  rendererDescription = nlohmann::json::parse(serializeTypeDescription(renderSettingsType()));
    const auto& pathDescription     = describedProperty(rendererDescription, "path");
    require(pathDescription.at("kind") == "enum" &&
                pathDescription.at("enum_values").size() == renderSettingsType().property("path").enumValues.size() &&
                pathDescription.at("choices").size() == renderSettingsType().property("path").choices.size(),
            "Type description omitted enum values or editor choices");
    const auto  cameraDescription = nlohmann::json::parse(serializeTypeDescription(cameraSettingsType()));
    const auto& nearDescription   = describedProperty(cameraDescription, "nearPlane");
    require(nearDescription.at("default") == CameraSettings {}.nearPlane &&
                nearDescription.at("range_hint").at("min") == near.min &&
                nearDescription.at("range_hint").at("max") == near.max &&
                nearDescription.at("flags") == nlohmann::json::array({"serialize", "inspect", "bind"}),
            "Type description drifted from declared defaults, ranges or usage");
    require(sceneDescribeType(world, "CameraSettings") == serializeTypeDescription(cameraSettingsType()) &&
                sceneDescribeType(world, "MaterialParameters") == serializeTypeDescription(materialParametersType()),
            "Scene API value types are missing from runtime schema discovery");
    world.writeProperty(child.id(), "name", std::string("Renamed"));
    world.writeProperty(child.id(), "processPriority", int64_t {25});
    auto transform  = glm::mat4 {1};
    transform[3][0] = 2;
    world.writeProperty(child.id(), "localTransform", transform);
    world.writeProperty(child.id(), "ownerId", PropertyReference {world.root().idInScene().value});
    require(child.name() == "Renamed" && child.processPriority() == 25 && child.localTransform() == transform &&
                child.owner() == &world.root(),
            "Inherited semantic properties did not edit the ECS store");
    const auto nodeState = nlohmann::json::parse(serializeProperties(node3DType(), &child));
    require(nodeState.at("name") == "Renamed" && nodeState.at("process_priority") == 25 &&
                nodeState.at("owner") == world.root().idInScene().value.toString() &&
                nodeState.at("transform").size() == 16,
            "Object serialization and semantic property access use different schemas");
    SceneTree foreign(std::make_unique<Node>("Foreign"));
    requireFailure(
        [&]
        {
            world.readProperty(foreign.root().id(), "name");
        });
    requireFailure(
        [&]
        {
            world.writeProperty(child.id(), "ownerId", PropertyReference {foreign.root().idInScene().value});
        });
    require(child.owner() == &world.root(), "Failed reference resolution changed ownership");
    requireFailure(
        [&]
        {
            world.writeProperty(child.id(), "localTransform", glm::mat4 {0});
        });
    require(child.localTransform() == transform, "Property reflection bypassed affine transform validation");
    auto reopenedWorld = SceneTree::parse(world.serialize());
    require(reopenedWorld.find(child.idInScene())->owner() == &reopenedWorld.root(),
            "Reflected common scene properties failed to round trip");
    require(sceneReadProperty(world, child.id(), "name") == R"("Renamed")", "Owned property JSON differs");
    sceneWriteProperty(world, nullptr, child.id(), "processPriority", "30");
    require(child.processPriority() == 30, "Generic property API failed to call its semantic setter");

    PropertyInfo links = nodeType().property("ownerId");
    links.name         = "links";
    links.jsonPath     = "/links";
    links.kind         = PropertyKind::eArray;
    links.elementKind  = PropertyKind::eReference;
    links.flags        = PropertyFlags::eSerialize;
    links.get          = [](const void* object) -> PropertyValue
    {
        return *static_cast<const PropertyArray*>(object);
    };
    links.set = [](void* object, const PropertyValue& value)
    {
        *static_cast<PropertyArray*>(object) = std::get<PropertyArray>(value);
    };
    links.defaultValue = PropertyArray {};
    const ObjectTypeInfo linksType {"Links", std::span(&links, 1)};
    PropertyArray        references {{PropertyReference {child.idInScene().value}, PropertyReference {}}};
    const auto           linksJson = serializeProperties(linksType, &references);
    PropertyArray        restoredLinks;
    deserializeProperties(linksType, linksJson, &restoredLinks);
    require(references == restoredLinks, "Variable-length reference collection failed to round trip");
    requireFailure(
        [&]
        {
            deserializeProperties(linksType, R"({"links":[null,"bad"]})", &restoredLinks);
        });
    require(restoredLinks == references, "Failed collection decoding partially mutated the object");
    requireFailure(
        [&]
        {
            deserializeProperty(links, R"([12])");
        });
    links.nullable = false;
    requireFailure(
        [&]
        {
            links.validate(PropertyArray {{PropertyReference {}}});
        });
    links.get = [](const void*) -> PropertyValue
    {
        throw std::runtime_error("Metadata inspection must not read live object state");
    };
    const auto linksDescription = nlohmann::json::parse(serializeTypeDescription(linksType));
    require(describedProperty(linksDescription, "links").at("element_kind") == "reference" &&
                describedProperty(linksDescription, "links").at("reference").at("nullable") == false &&
                describedProperty(linksDescription, "links").at("default") == nlohmann::json::array(),
            "Array metadata queried live state or lost its reference contract");
    require(serializeTypeDescription(linksType) == linksDescription.dump(), "Type descriptions are nondeterministic");
    const auto validDefault = links.defaultValue;
    links.defaultValue      = false;
    requireFailure(
        [&]
        {
            serializeTypeDescription(linksType);
        });
    links.defaultValue = validDefault;
    links.label        = "Linked nodes \xe2\x86\x92";
    links.flags        = PropertyFlags::eSerialize | PropertyFlags::eReload;
    const ObjectTypeInfo unicodeType {"Settings \xc3\xa9", std::span(&links, 1)};
    const auto           unicodeDescription = nlohmann::json::parse(serializeTypeDescription(unicodeType));
    require(unicodeDescription.at("name") == "Settings \xc3\xa9" &&
                describedProperty(unicodeDescription, "links").at("label") == "Linked nodes \xe2\x86\x92" &&
                describedProperty(unicodeDescription, "links").at("flags") ==
                    nlohmann::json::array({"serialize", "reload"}),
            "Type descriptions lost UTF-8 text or reload usage");
    std::cout
        << "Property tests passed: named access, catalogs, defaults, strict/atomic decoding and settings parity\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
