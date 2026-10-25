#include "lua_context.hpp"

#ifdef VULTRA_SCRIPT_GUI
#include <vultra/api/ui_bridge.hpp>
#endif
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/lua_values.generated.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace vultra
{
    struct LuaContext::Api
    {
        static LuaContext* self(lua_State* state)
        {
            return static_cast<LuaContext*>(lua_touserdata(state, lua_upvalueindex(1)));
        }

        static int result(lua_State* state, VultraStatus status, int valueCount = 1)
        {
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return valueCount;
        }

        static VultraSceneFrame inputFrame(lua_State* state)
        {
            const VultraSceneFrame frame {lua_touserdata(state, lua_upvalueindex(2)),
                                          uint64_t(lua_tointeger(state, lua_upvalueindex(3)))};
            const auto             current = self(state)->m_SceneFrame;
            if (!current.context || current.context != frame.context || current.serial != frame.serial)
            {
                luaL_error(state, "Input event requires its original callback");
            }
            return frame;
        }

        static int acceptInput(lua_State* state)
        {
            return result(
                state,
                sceneApi().set_input_handled(inputFrame(state), uint64_t(lua_tointeger(state, lua_upvalueindex(4)))),
                0);
        }

        static int inputAction(lua_State* state)
        {
            const auto  frame  = inputFrame(state);
            size_t      size   = 0;
            const char* name   = luaL_checklstring(state, 2, &size);
            uint64_t    value  = 0;
            const auto  status = sceneApi().input_action_state(frame,
                                                               uint64_t(lua_tointeger(state, lua_upvalueindex(4))),
                                                               name,
                                                               size,
                                                               &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            lua_newtable(state);
            lua_pushboolean(state, (value & 1) != 0);
            lua_setfield(state, -2, "held");
            lua_pushboolean(state, (value & 2) != 0);
            lua_setfield(state, -2, "pressed");
            lua_pushboolean(state, (value & 4) != 0);
            lua_setfield(state, -2, "released");
            return 1;
        }

        static int inputEvent(lua_State* state)
        {
            auto*                context = self(state);
            const auto           frame   = context->m_SceneFrame;
            VultraSceneInputData value {};
            auto                 status = sceneApi().input_event(frame, &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            scripting::detail::pushLuaValue(state, value);
            const char* text = nullptr;
            uint64_t    size = 0;
            status           = sceneApi().input_text(frame, value.serial, &text, &size);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            lua_pushlstring(state, text ? text : "", size);
            lua_setfield(state, -2, "text");
            lua_pushlightuserdata(state, context);
            lua_pushlightuserdata(state, frame.context);
            lua_pushinteger(state, lua_Integer(frame.serial));
            lua_pushinteger(state, lua_Integer(value.serial));
            lua_pushcclosure(state, acceptInput, 4);
            lua_setfield(state, -2, "accept");
            lua_pushlightuserdata(state, context);
            lua_pushlightuserdata(state, frame.context);
            lua_pushinteger(state, lua_Integer(frame.serial));
            lua_pushinteger(state, lua_Integer(value.serial));
            lua_pushcclosure(state, inputAction, 4);
            lua_setfield(state, -2, "action_state");
            return 1;
        }

        static int inputEnabled(lua_State* state)
        {
            const auto function =
                lua_toboolean(state, 2) ? sceneApi().node_unhandled_input_enabled : sceneApi().node_input_enabled;
            uint64_t   value  = 0;
            const auto status = function(self(state)->m_SceneFrame, uint64_t(luaL_checkinteger(state, 1)), &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushboolean(state, value != 0);
            }
            return result(state, status);
        }

        static int setInputEnabled(lua_State* state)
        {
            const auto function = lua_toboolean(state, 3) ? sceneApi().set_node_unhandled_input_enabled :
                                                            sceneApi().set_node_input_enabled;
            return result(state,
                          function(self(state)->m_SceneFrame,
                                   uint64_t(luaL_checkinteger(state, 1)),
                                   lua_toboolean(state, 2) ? 1 : 0),
                          0);
        }

        static int rootId(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().root_id(context->m_SceneFrame, &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int nodeExecution(lua_State* state)
        {
            const auto frame    = self(state)->m_SceneFrame;
            const auto id       = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            uint64_t   mode     = 0;
            int32_t    priority = 0;
            uint64_t   inside   = 0;
            uint64_t   queued   = 0;
            auto       status   = sceneApi().node_process_mode(frame, id, &mode);
            if (status == VULTRA_STATUS_OK)
            {
                status = sceneApi().node_process_priority(frame, id, &priority);
            }
            if (status == VULTRA_STATUS_OK)
            {
                status = sceneApi().node_inside_tree(frame, id, &inside);
            }
            if (status == VULTRA_STATUS_OK)
            {
                status = sceneApi().node_queued_for_deletion(frame, id, &queued);
            }
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(mode));
                lua_pushinteger(state, priority);
                lua_pushboolean(state, inside != 0);
                lua_pushboolean(state, queued != 0);
            }
            return result(state, status, 4);
        }

        static int setNodeProcessMode(lua_State* state)
        {
            return result(state,
                          sceneApi().set_node_process_mode(self(state)->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 2))),
                          0);
        }

        static int setNodeProcessPriority(lua_State* state)
        {
            const auto priority = luaL_checkinteger(state, 2);
            if (priority < INT32_MIN || priority > INT32_MAX)
            {
                return luaL_error(state, "Process priority must fit int32");
            }
            return result(state,
                          sceneApi().set_node_process_priority(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               static_cast<int32_t>(priority)),
                          0);
        }

        static int queueFree(lua_State* state)
        {
            return result(
                state,
                sceneApi().queue_free(self(state)->m_SceneFrame, static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                0);
        }

        static int execution(lua_State* state)
        {
            const auto frame  = self(state)->m_SceneFrame;
            uint64_t   paused = 0;
            uint64_t   tick   = 0;
            double     scale  = 0;
            auto       status = sceneApi().paused(frame, &paused);
            if (status == VULTRA_STATUS_OK)
            {
                status = sceneApi().physics_frame(frame, &tick);
            }
            if (status == VULTRA_STATUS_OK)
            {
                status = sceneApi().time_scale(frame, &scale);
            }
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushboolean(state, paused != 0);
                lua_pushinteger(state, static_cast<lua_Integer>(tick));
                lua_pushnumber(state, scale);
            }
            return result(state, status, 3);
        }

        static int setPaused(lua_State* state)
        {
            luaL_checktype(state, 1, LUA_TBOOLEAN);
            return result(state, sceneApi().set_paused(self(state)->m_SceneFrame, lua_toboolean(state, 1)), 0);
        }

        static int setTimeScale(lua_State* state)
        {
            return result(state, sceneApi().set_time_scale(self(state)->m_SceneFrame, luaL_checknumber(state, 1)), 0);
        }

        static int childCount(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().child_count(context->m_SceneFrame,
                                                        static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                        &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int nodeParentId(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().node_parent_id(context->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int nodeOwnerId(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().node_owner_id(context->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int childId(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().child_id(context->m_SceneFrame,
                                                     static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                     static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                     &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int nodeName(lua_State* state)
        {
            auto*       context = self(state);
            const char* data    = nullptr;
            uint64_t    size    = 0;
            const auto  status  = sceneApi().node_name(context->m_SceneFrame,
                                                       static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                       &data,
                                                       &size);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushlstring(state, data, size);
            }
            return result(state, status);
        }

        static int nodeType(lua_State* state)
        {
            auto*       context = self(state);
            const char* data    = nullptr;
            uint64_t    size    = 0;
            const auto  status  = sceneApi().node_type(context->m_SceneFrame,
                                                       static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                       &data,
                                                       &size);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushlstring(state, data, size);
            }
            return result(state, status);
        }

        static int setNodeOwner(lua_State* state)
        {
            auto*      context = self(state);
            const auto node    = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            const auto owner   = lua_isnoneornil(state, 2) ? 0 : static_cast<uint64_t>(luaL_checkinteger(state, 2));
            const auto status  = sceneApi().set_node_owner(context->m_SceneFrame, node, owner);
            return result(state, status);
        }

        static int nodeTranslation(lua_State* state)
        {
            auto*                  context = self(state);
            VultraSceneTranslation value {};
            const auto status = sceneApi().node_translation(context->m_SceneFrame,
                                                            static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                            &value);
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            lua_pushnumber(state, value.x);
            lua_pushnumber(state, value.y);
            lua_pushnumber(state, value.z);
            return 3;
        }

        static int setNodeTranslation(lua_State* state)
        {
            auto*                        context = self(state);
            const auto                   node    = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            const VultraSceneTranslation value {static_cast<float>(luaL_checknumber(state, 2)),
                                                static_cast<float>(luaL_checknumber(state, 3)),
                                                static_cast<float>(luaL_checknumber(state, 4))};
            const auto                   status = sceneApi().set_node_translation(context->m_SceneFrame, node, value);
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
        }

        static int createNode(lua_State* state)
        {
            auto*       context = self(state);
            const auto  parent  = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            size_t      size    = 0;
            const char* name    = luaL_checklstring(state, 2, &size);
            uint64_t    value   = 0;
            const auto  status  = sceneApi().create_node(context->m_SceneFrame, parent, name, size, &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int createNode3D(lua_State* state)
        {
            auto*       context = self(state);
            const auto  parent  = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            size_t      size    = 0;
            const char* name    = luaL_checklstring(state, 2, &size);
            uint64_t    value   = 0;
            const auto  status  = sceneApi().create_node3d(context->m_SceneFrame, parent, name, size, &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int createMesh(lua_State* state)
        {
            auto*       context   = self(state);
            const auto  parent    = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            size_t      nameSize  = 0;
            const char* name      = luaL_checklstring(state, 2, &nameSize);
            size_t      assetSize = 0;
            const char* assetId   = luaL_checklstring(state, 3, &assetSize);
            uint64_t    value     = 0;
            const auto  status =
                sceneApi().create_mesh(context->m_SceneFrame, parent, name, nameSize, assetId, assetSize, &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int reparentNode(lua_State* state)
        {
            auto*      context = self(state);
            const auto status  = sceneApi().reparent_node(context->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                          lua_isnoneornil(state, 3) || lua_toboolean(state, 3) ? 1 : 0);
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
        }

        static int duplicateMesh(lua_State* state)
        {
            auto*      context = self(state);
            uint64_t   value   = 0;
            const auto status  = sceneApi().duplicate_mesh(context->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                           &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(value));
            }
            return result(state, status);
        }

        static int copyMeshModel(lua_State* state)
        {
            auto*      context = self(state);
            const auto status  = sceneApi().copy_mesh_model(context->m_SceneFrame,
                                                            static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                            static_cast<uint64_t>(luaL_checkinteger(state, 2)));
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
        }

        static int setMeshModel(lua_State* state)
        {
            auto*       context = self(state);
            const auto  node    = static_cast<uint64_t>(luaL_checkinteger(state, 1));
            size_t      size    = 0;
            const char* text    = luaL_checklstring(state, 2, &size);
            const auto  status  = sceneApi().set_mesh_model(context->m_SceneFrame, node, text, size);
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
        }

        static int removeNode(lua_State* state)
        {
            auto*      context = self(state);
            const auto status =
                sceneApi().remove_node(context->m_SceneFrame, static_cast<uint64_t>(luaL_checkinteger(state, 1)));
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
        }

        static int createCamera(lua_State* state)
        {
            auto*       context = self(state);
            size_t      size    = 0;
            const auto* name    = luaL_checklstring(state, 2, &size);
            uint64_t    id      = 0;
            const auto  status  = sceneApi().create_camera(context->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           name,
                                                           size,
                                                           &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int cameraSettings(lua_State* state)
        {
            VultraCameraSettings value {};
            const auto           status = sceneApi().camera_settings(self(state)->m_SceneFrame,
                                                                     static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                     &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            scripting::detail::pushLuaValue(state, value);
            return 1;
        }

        static int components(lua_State* state)
        {
            const auto  frame  = self(state)->m_SceneFrame;
            const auto  entity = uint64_t(luaL_checkinteger(state, 1));
            const auto& api    = sceneApi();
            uint64_t    count  = 0;
            auto        status = api.component_count(frame, entity, &count);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            lua_createtable(state, int(count), 0);
            for (uint64_t index = 0; index < count; ++index)
            {
                const char* name = nullptr;
                uint64_t    size = 0;
                status           = api.component_name(frame, entity, index, &name, &size);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                lua_pushlstring(state, name, size);
                lua_rawseti(state, -2, lua_Integer(index + 1));
            }
            return 1;
        }

        template<class Read, class Push>
        static int readSceneJson(lua_State* state, const char* operation, Read read, Push push)
        {
            // lua_error skips C++ destructors. Callers capture only borrowed ABI arguments.
            static_assert(std::is_trivially_destructible_v<Read> && std::is_trivially_destructible_v<Push>);
            {
                try
                {
                    uint64_t size   = 0;
                    auto     status = read(nullptr, 0, &size);
                    if (status != VULTRA_STATUS_OK)
                    {
                        throw std::invalid_argument(std::string(operation) + ": API status " +
                                                    std::to_string(int(status)));
                    }
                    std::string json(size, '\0');
                    status = read(json.data(), json.size(), &size);
                    if (status != VULTRA_STATUS_OK)
                    {
                        throw std::invalid_argument(std::string(operation) + ": API status " +
                                                    std::to_string(int(status)));
                    }
                    push(state, nlohmann::json::parse(json));
                    return 1;
                }
                catch (const std::exception& error)
                {
                    lua_pushstring(state, error.what());
                }
            }
            return lua_error(state);
        }

        template<class Read>
        static int readSceneJson(lua_State* state, const char* operation, Read read)
        {
            return readSceneJson(state,
                                 operation,
                                 read,
                                 [](lua_State* target, const nlohmann::json& value)
                                 {
                                     pushLuaProperty(target, value);
                                 });
        }

        template<class Write, class Read>
        static int writeSceneJson(lua_State* state, int index, const char* operation, Write write, Read read)
        {
            static_assert(std::is_trivially_destructible_v<Write> && std::is_trivially_destructible_v<Read>);
            {
                try
                {
                    const auto json   = read(state, index).dump();
                    const auto status = write(json.data(), json.size());
                    if (status != VULTRA_STATUS_OK)
                    {
                        throw std::invalid_argument(std::string(operation) + ": API status " +
                                                    std::to_string(int(status)));
                    }
                    return 0;
                }
                catch (const std::exception& error)
                {
                    lua_pushstring(state, error.what());
                }
            }
            return lua_error(state);
        }

        template<class Write>
        static int writeSceneJson(lua_State* state, int index, const char* operation, Write write)
        {
            return writeSceneJson(state,
                                  index,
                                  operation,
                                  write,
                                  [](lua_State* source, int position)
                                  {
                                      return readLuaProperty(source, position);
                                  });
        }

        static int getComponentProperty(lua_State* state)
        {
            const auto  frame    = self(state)->m_SceneFrame;
            const auto  entity   = uint64_t(luaL_checkinteger(state, 1));
            size_t      typeSize = 0;
            size_t      nameSize = 0;
            const auto* type     = luaL_checklstring(state, 2, &typeSize);
            const auto* name     = luaL_checklstring(state, 3, &nameSize);
            return readSceneJson(
                state,
                "Read component property",
                [=](char* data, uint64_t capacity, uint64_t* size)
                {
                    return sceneApi()
                        .read_component_property(frame, entity, type, typeSize, name, nameSize, data, capacity, size);
                });
        }

        static int setComponentProperty(lua_State* state)
        {
            const auto  frame    = self(state)->m_SceneFrame;
            const auto  entity   = uint64_t(luaL_checkinteger(state, 1));
            size_t      typeSize = 0;
            size_t      nameSize = 0;
            const auto* type     = luaL_checklstring(state, 2, &typeSize);
            const auto* name     = luaL_checklstring(state, 3, &nameSize);
            return writeSceneJson(
                state,
                4,
                "Write component property",
                [=](const char* data, uint64_t size)
                {
                    return sceneApi()
                        .write_component_property(frame, entity, type, typeSize, name, nameSize, data, size);
                });
        }

        static int typeNames(lua_State* state)
        {
            const auto  frame  = self(state)->m_SceneFrame;
            const auto& api    = sceneApi();
            uint64_t    count  = 0;
            auto        status = api.type_count(frame, &count);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            lua_createtable(state, int(count), 0);
            for (uint64_t index = 0; index < count; ++index)
            {
                const char* name = nullptr;
                uint64_t    size = 0;
                status           = api.type_name(frame, index, &name, &size);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                lua_pushlstring(state, name, size);
                lua_rawseti(state, -2, lua_Integer(index + 1));
            }
            return 1;
        }

        static int describeType(lua_State* state)
        {
            const auto  frame  = self(state)->m_SceneFrame;
            size_t      length = 0;
            const auto* name   = luaL_checklstring(state, 1, &length);
            return readSceneJson(state,
                                 "Describe scene type",
                                 [=](char* data, uint64_t capacity, uint64_t* size)
                                 {
                                     return sceneApi().describe_type(frame, name, length, data, capacity, size);
                                 });
        }

        static int propertyList(lua_State* state)
        {
            const auto frame = self(state)->m_SceneFrame;
            const auto id    = uint64_t(luaL_checkinteger(state, 1));
            return readSceneJson(state,
                                 "List object properties",
                                 [=](char* data, uint64_t capacity, uint64_t* size)
                                 {
                                     return sceneApi().property_list(frame, id, data, capacity, size);
                                 });
        }

        static int getProperty(lua_State* state)
        {
            auto*       context = self(state);
            const auto  frame   = context->m_SceneFrame;
            const auto  node    = uint64_t(luaL_checkinteger(state, 1));
            size_t      length  = 0;
            const auto* name    = luaL_checklstring(state, 2, &length);
            return readSceneJson(
                state,
                "Read scene property",
                [=](char* data, uint64_t capacity, uint64_t* size)
                {
                    return sceneApi().read_property(frame, node, name, length, data, capacity, size);
                },
                [=](lua_State* target, const nlohmann::json& value)
                {
                    context->pushValue(context->scene().propertySchema(ObjectId {node}, std::string_view(name, length)),
                                       target,
                                       value);
                });
        }

        static int actionState(lua_State* state)
        {
            size_t      size   = 0;
            const auto* name   = luaL_checklstring(state, 1, &size);
            uint64_t    value  = 0;
            const auto  status = sceneApi().action_state(self(state)->m_SceneFrame, name, size, &value);
            if (status == VULTRA_STATUS_OK)
            {
                lua_createtable(state, 0, 3);
                lua_pushboolean(state, (value & 1) != 0);
                lua_setfield(state, -2, "held");
                lua_pushboolean(state, (value & 2) != 0);
                lua_setfield(state, -2, "pressed");
                lua_pushboolean(state, (value & 4) != 0);
                lua_setfield(state, -2, "released");
            }
            return result(state, status);
        }

        static int randomUInt(lua_State* state)
        {
            size_t       size  = 0;
            const auto*  name  = luaL_checklstring(state, 1, &size);
            const auto   frame = self(state)->m_SceneFrame;
            uint64_t     value = 0;
            VultraStatus status;
            if (lua_isnoneornil(state, 2))
            {
                status = sceneApi().random_uint(frame, name, size, &value);
            }
            else
            {
                const auto bound = luaL_checkinteger(state, 2);
                if (bound <= 0 || uint64_t(bound) > std::numeric_limits<uint32_t>::max())
                {
                    return luaL_error(state, "Random bound must be a positive 32-bit integer");
                }
                status = sceneApi().random_bounded_uint(frame, name, size, uint64_t(bound), &value);
            }
            lua_pushinteger(state, lua_Integer(value));
            return result(state, status);
        }

        static int randomUnit(lua_State* state)
        {
            size_t      size   = 0;
            const auto* name   = luaL_checklstring(state, 1, &size);
            double      value  = 0;
            const auto  status = sceneApi().random_unit(self(state)->m_SceneFrame, name, size, &value);
            lua_pushnumber(state, value);
            return result(state, status);
        }

        static int randomSeed(lua_State* state)
        {
            uint64_t   value  = 0;
            const auto status = sceneApi().random_seed(self(state)->m_SceneFrame, &value);
            lua_pushinteger(state, lua_Integer(value));
            return result(state, status);
        }

        static int setProperty(lua_State* state)
        {
            auto*       context = self(state);
            const auto  frame   = context->m_SceneFrame;
            const auto  node    = uint64_t(luaL_checkinteger(state, 1));
            size_t      length  = 0;
            const auto* name    = luaL_checklstring(state, 2, &length);
            return writeSceneJson(
                state,
                3,
                "Write scene property",
                [=](const char* data, uint64_t size)
                {
                    return sceneApi().write_property(frame, node, name, length, data, size);
                },
                [=](lua_State* source, int position)
                {
                    if (!frame.context)
                    {
                        throw std::invalid_argument("No active scene callback");
                    }
                    return context->readValue(
                        context->scene().propertySchema(ObjectId {node}, std::string_view(name, length)),
                        source,
                        position);
                });
        }

        static int setCameraSettings(lua_State* state)
        {
            const auto value  = scripting::detail::readLuaCameraSettings(state, 2);
            const auto status = sceneApi().set_camera_settings(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               value);
            return result(state, status, 0);
        }

        static int setCurrentCamera(lua_State* state)
        {
            return result(state,
                          sceneApi().set_current_camera(self(state)->m_SceneFrame,
                                                        static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                          0);
        }

        static int createEnvironment(lua_State* state)
        {
            size_t      size   = 0;
            const auto* name   = luaL_checklstring(state, 2, &size);
            uint64_t    id     = 0;
            const auto  status = sceneApi().create_environment(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               name,
                                                               size,
                                                               &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int environmentSettings(lua_State* state)
        {
            VultraEnvironmentSettings value {};
            const auto status = sceneApi().environment_settings(self(state)->m_SceneFrame,
                                                                static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            scripting::detail::pushLuaValue(state, value);
            return 1;
        }

        static int setEnvironmentSettings(lua_State* state)
        {
            const auto value = scripting::detail::readLuaEnvironmentSettings(state, 2);
            return result(state,
                          sceneApi().set_environment_settings(self(state)->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              value),
                          0);
        }

        static int setEnvironmentAsset(lua_State* state)
        {
            size_t      size  = 0;
            const auto* asset = luaL_checklstring(state, 2, &size);
            return result(state,
                          sceneApi().set_environment_asset(self(state)->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           asset,
                                                           size),
                          0);
        }

        static int setCurrentEnvironment(lua_State* state)
        {
            return result(state,
                          sceneApi().set_current_environment(self(state)->m_SceneFrame,
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                          0);
        }

        static int createLight(lua_State* state)
        {
            auto*       context = self(state);
            size_t      size    = 0;
            const auto* name    = luaL_checklstring(state, 2, &size);
            uint64_t    id      = 0;
            const auto  status  = sceneApi().create_light(context->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          name,
                                                          size,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 3)),
                                                          &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int lightKind(lua_State* state)
        {
            uint64_t   kind   = 0;
            const auto status = sceneApi().light_kind(self(state)->m_SceneFrame,
                                                      static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                      &kind);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(kind));
            }
            return result(state, status);
        }

        static int lightSettings(lua_State* state)
        {
            VultraLightSettings value {};
            const auto          status = sceneApi().light_settings(self(state)->m_SceneFrame,
                                                                   static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                   &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            scripting::detail::pushLuaValue(state, value);
            return 1;
        }

        static int setLightSettings(lua_State* state)
        {
            const auto value = scripting::detail::readLuaLightSettings(state, 2);
            return result(state,
                          sceneApi().set_light_settings(self(state)->m_SceneFrame,
                                                        static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                        value),
                          0);
        }

        static int objectType(lua_State* state)
        {
            const char* name = nullptr;
            uint64_t    size = 0;
            const auto  status =
                sceneApi().object_type(self(state)->m_SceneFrame, uint64_t(luaL_checkinteger(state, 1)), &name, &size);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushlstring(state, name, size);
            }
            return result(state, status);
        }

        static int createResource(lua_State* state)
        {
            size_t      typeSize  = 0;
            size_t      stateSize = 0;
            const auto* type      = luaL_checklstring(state, 1, &typeSize);
            const auto* data      = luaL_checklstring(state, 2, &stateSize);
            uint64_t    id        = 0;
            const auto  status =
                sceneApi().create_resource(self(state)->m_SceneFrame, type, typeSize, data, stateSize, &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int findResource(lua_State* state)
        {
            size_t      size   = 0;
            const auto* asset  = luaL_checklstring(state, 1, &size);
            uint64_t    id     = 0;
            const auto  status = sceneApi().find_resource(self(state)->m_SceneFrame, asset, size, &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int removeResource(lua_State* state)
        {
            return result(state,
                          sceneApi().remove_resource(self(state)->m_SceneFrame, uint64_t(luaL_checkinteger(state, 1))),
                          0);
        }

        static int createMaterial(lua_State* state)
        {
            size_t      size   = 0;
            const auto* name   = luaL_checklstring(state, 1, &size);
            uint64_t    id     = 0;
            const auto  status = sceneApi().create_material(self(state)->m_SceneFrame, name, size, &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int materialName(lua_State* state)
        {
            const char* name   = nullptr;
            uint64_t    size   = 0;
            const auto  status = sceneApi().material_name(self(state)->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          &name,
                                                          &size);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushlstring(state, name, size);
            }
            return result(state, status);
        }

        static int materialParameters(lua_State* state)
        {
            VultraMaterialParameters value {};
            const auto status = sceneApi().material_parameters(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               &value);
            if (status != VULTRA_STATUS_OK)
            {
                return result(state, status);
            }
            scripting::detail::pushLuaValue(state, value);
            return 1;
        }

        static int setMaterialParameters(lua_State* state)
        {
            const auto value = scripting::detail::readLuaMaterialParameters(state, 2);
            return result(state,
                          sceneApi().set_material_parameters(self(state)->m_SceneFrame,
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                             value),
                          0);
        }

        static int meshMaterial(lua_State* state)
        {
            uint64_t   id     = 0;
            const auto status = sceneApi().mesh_material(self(state)->m_SceneFrame,
                                                         static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                         static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                         &id);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(id));
            }
            return result(state, status);
        }

        static int setMeshMaterial(lua_State* state)
        {
            return result(state,
                          sceneApi().set_mesh_material(self(state)->m_SceneFrame,
                                                       static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                       static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                       static_cast<uint64_t>(luaL_checkinteger(state, 3))),
                          0);
        }

        static int uiText(lua_State* state)
        {
#ifdef VULTRA_SCRIPT_GUI
            auto*       context = self(state);
            size_t      size    = 0;
            const char* data    = luaL_checklstring(state, 1, &size);
            const auto  status  = uiApi().text(context->m_UiFrame, data, size);
            if (status != VULTRA_STATUS_OK)
            {
                return luaL_error(state, "Vultra API status %d", int(status));
            }
            return 0;
#else
            return luaL_error(state, "Lua UI requires the editor_gui capability");
#endif
        }

        static int uiButton(lua_State* state)
        {
#ifdef VULTRA_SCRIPT_GUI
            auto*       context = self(state);
            size_t      size    = 0;
            const char* data    = luaL_checklstring(state, 1, &size);
            uint8_t     clicked = 0;
            const auto  status  = uiApi().button(context->m_UiFrame, data, size, &clicked);
            if (status == VULTRA_STATUS_OK)
            {
                lua_pushboolean(state, clicked != 0);
            }
            return result(state, status);
#else
            return luaL_error(state, "Lua UI requires the editor_gui capability");
#endif
        }

        static constexpr const char* kLuaPrelude = R"lua(
local scene_api, gui_api = scene, gui
local node = {}
node.__index = node
function node:__eq(other) return self.id == other.id end
function node:get_child(index) return setmetatable({ id = scene_api.child_id(self.id, index) }, node) end
function node:child_count() return scene_api.child_count(self.id) end
function node:name() return scene_api.node_name(self.id) end
function node:get_property(name) return scene_api.get_property(self.id, name) end
function node:get_property_list() return scene_api.property_list(self.id) end
function node:set_property(name, value) return scene_api.set_property(self.id, name, value) end
function node:get_components() return scene_api.components(self.id) end
function node:get_component_property(component, name)
    return scene_api.get_component_property(self.id, component, name)
end
function node:set_component_property(component, name, value)
    return scene_api.set_component_property(self.id, component, name, value)
end
function node:get_class() return scene_api.node_type(self.id) end
function node:get_process_mode() local mode = scene_api.node_execution(self.id); return mode end
function node:get_process_priority() local _, priority = scene_api.node_execution(self.id); return priority end
function node:is_inside_tree() local _, _, inside = scene_api.node_execution(self.id); return inside end
function node:is_queued_for_deletion() local _, _, _, queued = scene_api.node_execution(self.id); return queued end
function node:set_process_mode(mode) return scene_api.set_node_process_mode(self.id, mode) end
function node:set_process_priority(priority) return scene_api.set_node_process_priority(self.id, priority) end
function node:queue_free() return scene_api.queue_free(self.id) end
function node:get_parent()
    local id = scene_api.node_parent_id(self.id)
    if id == 0 then return nil end
    return setmetatable({ id = id }, node)
end
function node:get_owner()
    local id = scene_api.node_owner_id(self.id)
    if id == 0 then return nil end
    return setmetatable({ id = id }, node)
end
function node:set_owner(owner) return scene_api.set_node_owner(self.id, owner and owner.id or 0) end
function node:is_input_enabled() return scene_api.input_enabled(self.id, false) end
function node:set_input_enabled(enabled) return scene_api.set_input_enabled(self.id, enabled, false) end
function node:is_unhandled_input_enabled() return scene_api.input_enabled(self.id, true) end
function node:set_unhandled_input_enabled(enabled) return scene_api.set_input_enabled(self.id, enabled, true) end
function node:position() return scene_api.node_translation(self.id) end
function node:set_position(x, y, z) return scene_api.set_node_translation(self.id, x, y, z) end
function node:create_child(name)
    return setmetatable({ id = scene_api.create_node(self.id, name) }, node)
end
function node:create_child_3d(name)
    return setmetatable({ id = scene_api.create_node3d(self.id, name) }, node)
end
function node:create_mesh_child(name, asset_id)
    return setmetatable({ id = scene_api.create_mesh(self.id, name, asset_id) }, node)
end
function node:reparent(parent, keep_global) return scene_api.reparent_node(self.id, parent.id, keep_global) end
function node:duplicate_mesh(parent)
    return setmetatable({ id = scene_api.duplicate_mesh(self.id, parent.id) }, node)
end
function node:copy_mesh_model(source) return scene_api.copy_mesh_model(self.id, source.id) end
function node:set_mesh_model(asset_id) return scene_api.set_mesh_model(self.id, asset_id) end
function node:remove() return scene_api.remove_node(self.id) end
function node:create_camera_child(name)
    return setmetatable({ id = scene_api.create_camera(self.id, name) }, node)
end
local light_kinds = { directional = 0, point = 1, spot = 2 }
function node:create_light_child(name, kind)
    assert(light_kinds[kind] ~= nil, "Unknown light kind")
    return setmetatable({ id = scene_api.create_light(self.id, name, light_kinds[kind]) }, node)
end
function node:camera_settings() return scene_api.camera_settings(self.id) end
function node:set_camera_settings(settings) return scene_api.set_camera_settings(self.id, settings) end
function node:make_current() return scene_api.set_current_camera(self.id) end
function node:light_kind() return ({ "directional", "point", "spot" })[scene_api.light_kind(self.id) + 1] end
function node:create_environment_child(name)
    return setmetatable({ id = scene_api.create_environment(self.id, name) }, node)
end
function node:environment_settings() return scene_api.environment_settings(self.id) end
function node:set_environment_settings(settings) return scene_api.set_environment_settings(self.id, settings) end
function node:set_environment_asset(asset_id) return scene_api.set_environment_asset(self.id, asset_id or "") end
function node:make_environment_current() return scene_api.set_current_environment(self.id) end
function node:light_settings() return scene_api.light_settings(self.id) end
function node:set_light_settings(settings) return scene_api.set_light_settings(self.id, settings) end
local world = { null = scene_api.null }
function world:get_type_names() return scene_api.type_names() end
function world:describe_type(name) return scene_api.describe_type(name) end
local resource = {}
resource.__index = resource
function resource:__eq(other) return self.id == other.id end
function resource:get_class() return scene_api.object_type(self.id) end
function resource:get_property(name) return scene_api.get_property(self.id, name) end
function resource:set_property(name, value) return scene_api.set_property(self.id, name, value) end
function resource:remove() return scene_api.remove_resource(self.id) end
local material = setmetatable({}, { __index = resource })
material.__index = material
material.__eq = resource.__eq
function material:name() return scene_api.material_name(self.id) end
function material:parameters() return scene_api.material_parameters(self.id) end
function material:set_parameters(parameters) return scene_api.set_material_parameters(self.id, parameters) end
function node:material(slot)
    local id = scene_api.mesh_material(self.id, slot)
    if id == 0 then return nil end
    return setmetatable({ id = id }, material)
end
function node:set_material(slot, value) return scene_api.set_mesh_material(self.id, slot, value and value.id or 0) end
local function wrap_resource(id)
    if id == 0 then return nil end
    local name = scene_api.object_type(id)
    while name ~= 'MaterialResource' and name ~= 'Resource' do
        name = scene_api.describe_type(name).base
        assert(name, 'Resource type has no bound native ancestor')
    end
    return setmetatable({ id = id }, name == 'MaterialResource' and material or resource)
end
function world:create_resource(type_name, state_json)
    return wrap_resource(scene_api.create_resource(type_name, state_json or "{}"))
end
function world:find_resource(asset_id) return wrap_resource(scene_api.find_resource(asset_id)) end
function __vultra_wrap_reference(id, is_node)
    if is_node then return setmetatable({ id = id }, node) end
    return wrap_resource(id)
end
function world:create_material(name)
    return setmetatable({ id = scene_api.create_material(name) }, material)
end
function world:root() return setmetatable({ id = scene_api.root_id() }, node) end
function world:action(name) return scene_api.action(name) end
function world:random_seed() return scene_api.random_seed() end
function world:random_uint(stream, bound) return scene_api.random_uint(stream, bound) end
function world:random_unit(stream) return scene_api.random_unit(stream) end
function world:is_paused() local paused = scene_api.execution(); return paused end
function world:physics_frame() local _, tick = scene_api.execution(); return tick end
function world:time_scale() local _, _, scale = scene_api.execution(); return scale end
function world:set_paused(paused) return scene_api.set_paused(paused) end
function world:set_time_scale(scale) return scene_api.set_time_scale(scale) end
function world:clear_current_camera() return scene_api.set_current_camera(0) end
function world:clear_current_environment() return scene_api.set_current_environment(0) end
local editor = {}
function editor:text(message) return gui_api.text(message) end
function editor:button(label) return gui_api.button(label) end
__vultra_editor_gui = editor
function __vultra_attach(script, id)
    script.node = setmetatable({ id = id }, node)
    script.scene = world
end
scene, gui = nil, nil
)lua";
    };

    void LuaContext::pushInputEvent()
    {
        lua_pushlightuserdata(m_State, this);
        lua_pushcclosure(m_State, Api::inputEvent, 1);
        if (lua_pcall(m_State, 0, 1, 0) != LUA_OK)
        {
            throw std::runtime_error(std::string("Build Lua input event: ") + lua_tostring(m_State, -1));
        }
    }

    void LuaContext::initializeApi()
    {
        scripting::detail::installLuaEnums(m_State);
        luaL_openlibs(m_State);
        lua_newtable(m_State);
        bind("root_id", Api::rootId);
        bind("child_count", Api::childCount);
        bind("child_id", Api::childId);
        bind("node_name", Api::nodeName);
        bind("get_property", Api::getProperty);
        bind("property_list", Api::propertyList);
        bind("set_property", Api::setProperty);
        lua_pushlightuserdata(m_State, nullptr);
        lua_setfield(m_State, -2, "null");
        bind("node_type", Api::nodeType);
        bind("type_names", Api::typeNames);
        bind("describe_type", Api::describeType);
        bind("components", Api::components);
        bind("get_component_property", Api::getComponentProperty);
        bind("set_component_property", Api::setComponentProperty);
        bind("node_parent_id", Api::nodeParentId);
        bind("node_owner_id", Api::nodeOwnerId);
        bind("set_node_owner", Api::setNodeOwner);
        bind("node_execution", Api::nodeExecution);
        bind("set_node_process_mode", Api::setNodeProcessMode);
        bind("set_node_process_priority", Api::setNodeProcessPriority);
        bind("queue_free", Api::queueFree);
        bind("input_enabled", Api::inputEnabled);
        bind("set_input_enabled", Api::setInputEnabled);
        bind("execution", Api::execution);
        bind("action", Api::actionState);
        bind("random_seed", Api::randomSeed);
        bind("random_uint", Api::randomUInt);
        bind("random_unit", Api::randomUnit);
        bind("set_paused", Api::setPaused);
        bind("set_time_scale", Api::setTimeScale);
        bind("node_translation", Api::nodeTranslation);
        bind("set_node_translation", Api::setNodeTranslation);
        bind("create_node", Api::createNode);
        bind("create_node3d", Api::createNode3D);
        bind("create_mesh", Api::createMesh);
        bind("reparent_node", Api::reparentNode);
        bind("duplicate_mesh", Api::duplicateMesh);
        bind("copy_mesh_model", Api::copyMeshModel);
        bind("set_mesh_model", Api::setMeshModel);
        bind("remove_node", Api::removeNode);
        bind("create_camera", Api::createCamera);
        bind("camera_settings", Api::cameraSettings);
        bind("set_camera_settings", Api::setCameraSettings);
        bind("set_current_camera", Api::setCurrentCamera);
        bind("create_light", Api::createLight);
        bind("create_environment", Api::createEnvironment);
        bind("environment_settings", Api::environmentSettings);
        bind("set_environment_settings", Api::setEnvironmentSettings);
        bind("set_environment_asset", Api::setEnvironmentAsset);
        bind("set_current_environment", Api::setCurrentEnvironment);
        bind("light_kind", Api::lightKind);
        bind("light_settings", Api::lightSettings);
        bind("set_light_settings", Api::setLightSettings);
        bind("object_type", Api::objectType);
        bind("create_resource", Api::createResource);
        bind("find_resource", Api::findResource);
        bind("remove_resource", Api::removeResource);
        bind("create_material", Api::createMaterial);
        bind("material_name", Api::materialName);
        bind("material_parameters", Api::materialParameters);
        bind("set_material_parameters", Api::setMaterialParameters);
        bind("mesh_material", Api::meshMaterial);
        bind("set_mesh_material", Api::setMeshMaterial);
        lua_setglobal(m_State, "scene");
        lua_newtable(m_State);
        bind("text", Api::uiText);
        bind("button", Api::uiButton);
        lua_setglobal(m_State, "gui");
        if (luaL_dostring(m_State, Api::kLuaPrelude) != LUA_OK)
        {
            throw std::runtime_error(std::string("Initialize Lua script API: ") + lua_tostring(m_State, -1));
        }

        lua_getglobal(m_State, "__vultra_attach");
        m_AttachRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
        lua_getglobal(m_State, "__vultra_editor_gui");
        m_GuiRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
        lua_getglobal(m_State, "__vultra_wrap_reference");
        m_WrapRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
        lua_pushnil(m_State);
        lua_setglobal(m_State, "__vultra_wrap_reference");
        lua_pushnil(m_State);
        lua_setglobal(m_State, "__vultra_attach");
        lua_pushnil(m_State);
        lua_setglobal(m_State, "__vultra_editor_gui");
    }
} // namespace vultra
