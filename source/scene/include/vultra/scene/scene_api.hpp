#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/core/base/api_annotations.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace vultra
{
    struct VULTRA_BIND_POD SceneTranslation
    {
        float x;
        float y;
        float z;
    };

    struct VULTRA_BIND_POD SceneInputData
    {
        uint64_t serial;
        uint32_t kind;
        uint32_t code;
        uint32_t action;
        uint32_t modifiers;
        float    x;
        float    y;
        float    deltaX;
        float    deltaY;
    };

    // Snapshot data may be retained; operations/text borrows require the original callback and event serial.
    VULTRA_BIND_SCENE SceneInputData sceneInputEvent(const SceneTree& scene);
    VULTRA_BIND_SCENE std::string_view sceneInputText(const SceneTree& scene, uint64_t serial);
    VULTRA_BIND_SCENE uint64_t sceneInputActionState(const SceneTree& scene, uint64_t serial, std::string_view action);
    VULTRA_BIND_SCENE void     sceneSetInputHandled(SceneTree& scene, uint64_t serial);
    VULTRA_BIND_SCENE uint64_t sceneNodeInputEnabled(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void     sceneSetNodeInputEnabled(SceneTree& scene, ObjectId node, uint64_t enabled);
    VULTRA_BIND_SCENE uint64_t sceneNodeUnhandledInputEnabled(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void     sceneSetNodeUnhandledInputEnabled(SceneTree& scene, ObjectId node, uint64_t enabled);

    // Scene access is borrowed during lifecycle/process/input or explicit property callbacks, never by onGui.
    VULTRA_BIND_SCENE ObjectId sceneRootId(const SceneTree& scene);
    VULTRA_BIND_SCENE uint64_t sceneChildCount(const SceneTree& scene, ObjectId parent);
    VULTRA_BIND_SCENE ObjectId sceneChildId(const SceneTree& scene, ObjectId parent, uint64_t index);
    VULTRA_BIND_SCENE std::string_view sceneNodeName(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE std::string_view sceneNodeType(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE uint64_t         sceneTypeCount(const SceneTree& scene);
    VULTRA_BIND_SCENE std::string_view sceneTypeName(const SceneTree& scene, uint64_t index);
    // Metadata comes from this world's catalog, including project-registered types and inherited fields.
    VULTRA_BIND_SCENE std::string sceneDescribeType(const SceneTree& scene, std::string_view type);
    // Instance fields include native properties and the currently attached script's exports.
    VULTRA_BIND_SCENE std::string scenePropertyList(const SceneTree& scene, ObjectId object);
    VULTRA_BIND_SCENE uint64_t    sceneComponentCount(const SceneTree& scene, ObjectId entity);
    VULTRA_BIND_SCENE std::string_view sceneComponentName(const SceneTree& scene, ObjectId entity, uint64_t index);
    VULTRA_BIND_SCENE std::string sceneReadComponentProperty(const SceneTree& scene,
                                                             ObjectId         entity,
                                                             std::string_view component,
                                                             std::string_view property);
    VULTRA_BIND_SCENE void        sceneWriteComponentProperty(SceneTree&             scene,
                                                              const ProjectManifest* project,
                                                              ObjectId               entity,
                                                              std::string_view       component,
                                                              std::string_view       property,
                                                              std::string_view       json);
    // Owned UTF-8 JSON values; generated wrappers copy into caller-owned buffers.
    VULTRA_BIND_SCENE std::string sceneReadProperty(const SceneTree& scene, ObjectId node, std::string_view property);
    VULTRA_BIND_SCENE void        sceneWriteProperty(SceneTree&             scene,
                                                     const ProjectManifest* project,
                                                     ObjectId               node,
                                                     std::string_view       property,
                                                     std::string_view       json);
    VULTRA_BIND_SCENE ObjectId    sceneNodeParentId(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE ObjectId    sceneNodeOwnerId(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void        sceneSetNodeOwner(SceneTree& scene, ObjectId node, ObjectId owner);
    VULTRA_BIND_SCENE uint64_t    sceneNodeInsideTree(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE uint64_t    sceneNodeQueuedForDeletion(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE uint64_t    sceneNodeProcessMode(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void        sceneSetNodeProcessMode(SceneTree& scene, ObjectId node, uint64_t mode);
    VULTRA_BIND_SCENE int32_t     sceneNodeProcessPriority(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void        sceneSetNodeProcessPriority(SceneTree& scene, ObjectId node, int32_t priority);
    VULTRA_BIND_SCENE void        sceneQueueFree(SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE uint64_t    scenePaused(const SceneTree& scene);
    VULTRA_BIND_SCENE void        sceneSetPaused(SceneTree& scene, uint64_t paused);
    VULTRA_BIND_SCENE uint64_t    scenePhysicsFrame(const SceneTree& scene);
    // Bit 0: held, bit 1: pressed, bit 2: released. Uses the active frame/fixed callback phase.
    VULTRA_BIND_SCENE uint64_t sceneActionState(const SceneTree& scene, std::string_view action);
    VULTRA_BIND_SCENE double   sceneTimeScale(const SceneTree& scene);
    VULTRA_BIND_SCENE void     sceneSetTimeScale(SceneTree& scene, double scale);
    VULTRA_BIND_SCENE uint64_t sceneRandomSeed(const SceneTree& scene);
    VULTRA_BIND_SCENE uint64_t sceneRandomUInt(SceneTree& scene, std::string_view stream);
    VULTRA_BIND_SCENE uint64_t sceneRandomBoundedUInt(SceneTree& scene, std::string_view stream, uint64_t bound);
    VULTRA_BIND_SCENE double   sceneRandomUnit(SceneTree& scene, std::string_view stream);
    VULTRA_BIND_SCENE std::string sceneRandomState(const SceneTree& scene);
    // These operations require stopped, idle execution and validate before changing any stream.
    VULTRA_BIND_SCENE void             sceneSetRandomSeed(SceneTree& scene, uint64_t seed);
    VULTRA_BIND_SCENE void             sceneRestoreRandomState(SceneTree& scene, std::string_view json);
    VULTRA_BIND_SCENE SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void     sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation);
    VULTRA_BIND_SCENE ObjectId sceneCreateNode(SceneTree& scene, ObjectId parent, std::string_view name);
    VULTRA_BIND_SCENE ObjectId sceneCreateNode3D(SceneTree& scene, ObjectId parent, std::string_view name);
    VULTRA_BIND_SCENE ObjectId sceneCreateNodeType(SceneTree&             scene,
                                                   const ProjectManifest* project,
                                                   ObjectId               parent,
                                                   std::string_view       type,
                                                   std::string_view       name,
                                                   std::string_view       state);
    VULTRA_BIND_SCENE void
    sceneAddComponent(SceneTree& scene, ObjectId entity, std::string_view type, std::string_view state);
    VULTRA_BIND_SCENE void     sceneRemoveComponent(SceneTree& scene, ObjectId entity, std::string_view type);
    VULTRA_BIND_SCENE ObjectId sceneCreateMesh(SceneTree&             scene,
                                               const ProjectManifest& project,
                                               ObjectId               parent,
                                               std::string_view       name,
                                               std::string_view       assetId);
    VULTRA_BIND_SCENE void sceneReparentNode(SceneTree& scene, ObjectId node, ObjectId newParent, uint64_t keepGlobal);
    // Shares mesh/material assets and copies the local transform, without children or scripts.
    VULTRA_BIND_SCENE ObjectId sceneDuplicateMesh(SceneTree& scene, ObjectId source, ObjectId parent);
    VULTRA_BIND_SCENE void     sceneCopyMeshModel(SceneTree& scene, ObjectId target, ObjectId source);
    VULTRA_BIND_SCENE void
    sceneSetMeshModel(SceneTree& scene, const ProjectManifest& project, ObjectId target, std::string_view assetId);
    VULTRA_BIND_SCENE void           sceneRemoveNode(SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE ObjectId       sceneCreateCamera(SceneTree& scene, ObjectId parent, std::string_view name);
    VULTRA_BIND_SCENE CameraSettings sceneCameraSettings(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void           sceneSetCameraSettings(SceneTree& scene, ObjectId node, CameraSettings settings);
    VULTRA_BIND_SCENE ObjectId       sceneCurrentCamera(const SceneTree& scene);
    VULTRA_BIND_SCENE void           sceneSetCurrentCamera(SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE ObjectId       sceneCreateLight(SceneTree&       scene,
                                                      ObjectId         parent,
                                                      std::string_view name,
                                                      uint64_t         kind);
    VULTRA_BIND_SCENE uint64_t       sceneLightKind(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE LightSettings  sceneLightSettings(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void           sceneSetLightSettings(SceneTree& scene, ObjectId node, LightSettings settings);
    VULTRA_BIND_SCENE ObjectId       sceneCreateEnvironment(SceneTree& scene, ObjectId parent, std::string_view name);
    VULTRA_BIND_SCENE EnvironmentSettings sceneEnvironmentSettings(const SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE void sceneSetEnvironmentSettings(SceneTree& scene, ObjectId node, EnvironmentSettings settings);
    // Empty asset text selects the procedural environment.
    VULTRA_BIND_SCENE void
    sceneSetEnvironmentAsset(SceneTree& scene, const ProjectManifest& project, ObjectId node, std::string_view assetId);
    VULTRA_BIND_SCENE ObjectId sceneCurrentEnvironment(const SceneTree& scene);
    VULTRA_BIND_SCENE void     sceneSetCurrentEnvironment(SceneTree& scene, ObjectId node);
    VULTRA_BIND_SCENE std::string_view sceneObjectType(const SceneTree& scene, ObjectId object);
    // Persist UUIDs, resolve runtime identities only in the receiving world. Zero means a missing loaded target.
    VULTRA_BIND_SCENE          std::string
                               sceneObjectPersistentId(const SceneTree& scene, ObjectId object, std::string_view type);
    VULTRA_BIND_SCENE ObjectId sceneFindNode(const SceneTree& scene, std::string_view nodeId);
    VULTRA_BIND_SCENE ObjectId sceneFindResource(const SceneTree& scene, std::string_view resourceId);
    VULTRA_BIND_SCENE ObjectId sceneCreateResource(SceneTree& scene, std::string_view type, std::string_view state);
    VULTRA_BIND_SCENE void     sceneRemoveResource(SceneTree& scene, ObjectId resource);
    VULTRA_BIND_SCENE ObjectId sceneCreateMaterial(SceneTree& scene, std::string_view name);
    VULTRA_BIND_SCENE std::string_view   sceneMaterialName(const SceneTree& scene, ObjectId material);
    VULTRA_BIND_SCENE MaterialParameters sceneMaterialParameters(const SceneTree& scene, ObjectId material);
    VULTRA_BIND_SCENE void
    sceneSetMaterialParameters(SceneTree& scene, ObjectId material, MaterialParameters parameters);
    VULTRA_BIND_SCENE void sceneRemoveMaterial(SceneTree& scene, ObjectId material);
    // Zero clears an override, restoring the imported material at this slot.
    VULTRA_BIND_SCENE ObjectId sceneMeshMaterial(const SceneTree& scene, ObjectId node, uint64_t slot);
    VULTRA_BIND_SCENE void     sceneSetMeshMaterial(SceneTree& scene, ObjectId node, uint64_t slot, ObjectId material);
} // namespace vultra
