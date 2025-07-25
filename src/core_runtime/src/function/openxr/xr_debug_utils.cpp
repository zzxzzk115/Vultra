#include "vultra/function/openxr/xr_debug_utils.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/function/openxr/xr_helper.hpp"
#include "vultra/function/openxr/xr_utils.hpp"

#include <sstream>
#include <string>

XrBool32 OpenXRMessageCallbackFunction(XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
                                       XrDebugUtilsMessageTypeFlagsEXT             messageType,
                                       const XrDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                       void* /*pUserData*/)
{
    // Lambda to covert an XrDebugUtilsMessageSeverityFlagsEXT to std::string. Bitwise check to concatenate multiple
    // severities to the output string.
    auto GetMessageSeverityString = [](XrDebugUtilsMessageSeverityFlagsEXT messageSeverity) -> std::string {
        bool separator = false;

        std::string msgFlags;
        if (BitwiseCheck(messageSeverity, XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT))
        {
            msgFlags += "VERBOSE";
            separator = true;
        }
        if (BitwiseCheck(messageSeverity, XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT))
        {
            if (separator)
            {
                msgFlags += ",";
            }
            msgFlags += "INFO";
            separator = true;
        }
        if (BitwiseCheck(messageSeverity, XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT))
        {
            if (separator)
            {
                msgFlags += ",";
            }
            msgFlags += "WARN";
            separator = true;
        }
        if (BitwiseCheck(messageSeverity, XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
        {
            if (separator)
            {
                msgFlags += ",";
            }
            msgFlags += "ERROR";
        }
        return msgFlags;
    };
    // Lambda to covert an XrDebugUtilsMessageTypeFlagsEXT to std::string. Bitwise check to concatenate multiple
    // types to the output string.
    auto GetMessageTypeString = [](XrDebugUtilsMessageTypeFlagsEXT messageType) -> std::string {
        bool separator = false;

        std::string msgFlags;
        if (BitwiseCheck(messageType, XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT))
        {
            msgFlags += "GEN";
            separator = true;
        }
        if (BitwiseCheck(messageType, XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT))
        {
            if (separator)
            {
                msgFlags += ",";
            }
            msgFlags += "SPEC";
            separator = true;
        }
        if (BitwiseCheck(messageType, XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT))
        {
            if (separator)
            {
                msgFlags += ",";
            }
            msgFlags += "PERF";
        }
        return msgFlags;
    };

    // Collect message data.
    std::string functionName       = (pCallbackData->functionName) ? pCallbackData->functionName : "";
    std::string messageSeverityStr = GetMessageSeverityString(messageSeverity);
    std::string messageTypeStr     = GetMessageTypeString(messageType);
    std::string messageId          = (pCallbackData->messageId) ? pCallbackData->messageId : "";
    std::string message            = (pCallbackData->message) ? pCallbackData->message : "";

    // String stream final message.
    std::stringstream errorMessage;
    errorMessage << functionName << "(" << messageSeverityStr << " / " << messageTypeStr << "): msgNum: " << messageId
                 << " - " << message;

    // Log and debug break.
    VULTRA_CORE_ERROR("[OpenXR] {}", errorMessage.str());
    if (BitwiseCheck(messageSeverity, XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
    {
        DEBUG_BREAK();
    }
    return XrBool32();
}

// NOLINTBEGIN
XrDebugUtilsMessengerEXT CreateOpenXRDebugUtilsMessenger(XrInstance m_XrInstance)
{
    // Fill out a XrDebugUtilsMessengerCreateInfoEXT structure specifying all severities and types.
    // Set the userCallback to OpenXRMessageCallbackFunction().
    XrDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI {};
    debugUtilsMessengerCI.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugUtilsMessengerCI.messageSeverities =
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugUtilsMessengerCI.messageTypes =
        XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
    debugUtilsMessengerCI.userCallback =
        static_cast<PFN_xrDebugUtilsMessengerCallbackEXT>(OpenXRMessageCallbackFunction);
    debugUtilsMessengerCI.userData = nullptr;

    // Load xrCreateDebugUtilsMessengerEXT() function pointer as it is not default loaded by the OpenXR loader.
    XrDebugUtilsMessengerEXT           debugUtilsMessenger {};
    PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT;
    OPENXR_CHECK(xrGetInstanceProcAddr(m_XrInstance,
                                       "xrCreateDebugUtilsMessengerEXT",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateDebugUtilsMessengerEXT)),
                 "Failed to get InstanceProcAddr.");

    // Finally create and return the XrDebugUtilsMessengerEXT.
    OPENXR_CHECK(xrCreateDebugUtilsMessengerEXT(m_XrInstance, &debugUtilsMessengerCI, &debugUtilsMessenger),
                 "Failed to create DebugUtilsMessenger.");
    return debugUtilsMessenger;
}

void DestroyOpenXRDebugUtilsMessenger(XrInstance m_XrInstance, XrDebugUtilsMessengerEXT debugUtilsMessenger)
{
    // Load xrDestroyDebugUtilsMessengerEXT() function pointer as it is not default loaded by the OpenXR loader.
    PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT;
    OPENXR_CHECK(xrGetInstanceProcAddr(m_XrInstance,
                                       "xrDestroyDebugUtilsMessengerEXT",
                                       reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyDebugUtilsMessengerEXT)),
                 "Failed to get InstanceProcAddr.");

    // Destroy the provided XrDebugUtilsMessengerEXT.
    OPENXR_CHECK(xrDestroyDebugUtilsMessengerEXT(debugUtilsMessenger), "Failed to destroy DebugUtilsMessenger.");
}
// NOLINTEND