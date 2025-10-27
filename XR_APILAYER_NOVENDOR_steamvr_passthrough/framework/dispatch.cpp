// MIT License
//
// Copyright(c) 2021-2022 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include <layer.h>

#include "dispatch.h"
#include "log.h"

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

using namespace LAYER_NAMESPACE::log;

namespace LAYER_NAMESPACE {

    // Entry point for creating the layer.
    XrResult xrCreateApiLayerInstance(const XrInstanceCreateInfo* const instanceCreateInfo,
                                      const struct XrApiLayerCreateInfo* const apiLayerInfo,
                                      XrInstance* const instance) {
#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance");
#endif
        DebugLog("--> xrCreateApiLayerInstance\n");

        if (!apiLayerInfo || apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
            apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
            apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) || !apiLayerInfo->nextInfo ||
            apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
            apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
            apiLayerInfo->nextInfo->layerName != LayerName || !apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
            !apiLayerInfo->nextInfo->nextCreateApiLayerInstance) {
            ErrorLog("xrCreateApiLayerInstance validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Dump the other layers.
        {
            auto info = apiLayerInfo->nextInfo;
            while (info) {
#if USE_TRACELOGGING
                TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance", TLArg(info->layerName, "LayerName"));
#endif
                Log("Using layer: %s\n", info->layerName);
                info = info->next;
            }
        }

        // The list of extensions to remove or implicitly add.
        std::vector<std::string> blockedExtensions = { 
            XR_EXT_COMPOSITION_LAYER_INVERTED_ALPHA_EXTENSION_NAME,
            XR_ANDROID_PASSTHROUGH_CAMERA_STATE_EXTENSION_NAME,
            XR_VARJO_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME, 
            XR_VARJO_ENVIRONMENT_DEPTH_ESTIMATION_EXTENSION_NAME };

        std::vector<std::string> implicitExtensions = { XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME };

        // Only request implicit extensions that are supported.
        //
        // While the OpenXR standard states that xrEnumerateInstanceExtensionProperties() can be queried without an
        // instance, this does not stand for API layers, since API layers implementation might rely on the next
        // xrGetInstanceProcAddr() pointer, which is not (yet) populated if no instance is created.
        // We create a dummy instance in order to do these checks.
        if (!implicitExtensions.empty()) {
            XrInstance dummyInstance = XR_NULL_HANDLE;

            // Call the chain to create a dummy instance. Request no extensions in order to speed things up.
            XrInstanceCreateInfo dummyCreateInfo = *instanceCreateInfo;
            dummyCreateInfo.enabledExtensionCount = 0;

            XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
            chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;

            if (XR_SUCCEEDED(apiLayerInfo->nextInfo->nextCreateApiLayerInstance(
                &dummyCreateInfo, &chainApiLayerInfo, &dummyInstance))) {

                PFN_xrDestroyInstance xrDestroyInstance;
                CHECK_XRCMD(apiLayerInfo->nextInfo->nextGetInstanceProcAddr(
                    dummyInstance, "xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyInstance)));

                // Check the available extensions.
                PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties;
                CHECK_XRCMD(apiLayerInfo->nextInfo->nextGetInstanceProcAddr(
                    dummyInstance,
                    "xrEnumerateInstanceExtensionProperties",
                    reinterpret_cast<PFN_xrVoidFunction*>(&xrEnumerateInstanceExtensionProperties)));

                uint32_t extensionsCount = 0;
                CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionsCount, nullptr));
                std::vector<XrExtensionProperties> extensions(extensionsCount, { XR_TYPE_EXTENSION_PROPERTIES });
                CHECK_XRCMD(
                    xrEnumerateInstanceExtensionProperties(nullptr, extensionsCount, &extensionsCount, extensions.data()));

                for (auto it = implicitExtensions.begin(); it != implicitExtensions.end();) {
                    const auto matchExtensionName = [&](const XrExtensionProperties& properties) {
                        return properties.extensionName == *it;
                    };
                    if (std::find_if(extensions.cbegin(), extensions.cend(), matchExtensionName) != extensions.cend()) {
                        it = ++it;
                    }
                    else {
                        Log("Cannot satisfy implicit extension request: %s\n", it->c_str());
                        it = implicitExtensions.erase(it);
                    }
                }

                CHECK_XRCMD(xrDestroyInstance(dummyInstance));
            }
        }

        // Dump the requested extensions.
        XrInstanceCreateInfo chainInstanceCreateInfo = *instanceCreateInfo;
        std::vector<std::string> requestedExtensions;
        std::vector<const char*> newEnabledExtensionNames;
        for (uint32_t i = 0; i < chainInstanceCreateInfo.enabledExtensionCount; i++) {
            const std::string_view ext(chainInstanceCreateInfo.enabledExtensionNames[i]);
#if USE_TRACELOGGING
            TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance", TLArg(ext.data(), "ExtensionName"));
#endif
            requestedExtensions.push_back(std::string(ext));

            if (std::find(blockedExtensions.cbegin(), blockedExtensions.cend(), ext) == blockedExtensions.cend()) {
                Log("Requested extension: %s\n", ext.data());
                newEnabledExtensionNames.push_back(ext.data());
            } else {
                Log("Blocking extension: %s\n", ext.data());
            }
        }
        for (const auto& ext : implicitExtensions) {
            Log("Requesting extension: %s\n", ext.c_str());
            newEnabledExtensionNames.push_back(ext.c_str());
        }
        chainInstanceCreateInfo.enabledExtensionNames = newEnabledExtensionNames.data();
        chainInstanceCreateInfo.enabledExtensionCount = (uint32_t)newEnabledExtensionNames.size();

        // Call the chain to create the instance.
        XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
        chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
        XrResult result =
            apiLayerInfo->nextInfo->nextCreateApiLayerInstance(&chainInstanceCreateInfo, &chainApiLayerInfo, instance);
        if (result == XR_SUCCESS) {
            // Create our layer.
            LAYER_NAMESPACE::GetInstance()->SetGetInstanceProcAddr(apiLayerInfo->nextInfo->nextGetInstanceProcAddr,
                                                                   *instance);
            LAYER_NAMESPACE::GetInstance()->SetGrantedExtensions(implicitExtensions);
            LAYER_NAMESPACE::GetInstance()->SetRequestedExtensions(requestedExtensions);

            // Forward the xrCreateInstance() call to the layer.
            try {
                result = LAYER_NAMESPACE::GetInstance()->xrCreateInstance(instanceCreateInfo);
            } catch (std::runtime_error exc) {
#if USE_TRACELOGGING
                TraceLoggingWrite(g_traceProvider, "xrCreateInstance_Error", TLArg(exc.what(), "Error"));
#endif
                ErrorLog("xrCreateInstance: %s\n", exc.what());
                result = XR_ERROR_RUNTIME_FAILURE;
            }

            // Cleanup attempt before returning an error.
            if (XR_FAILED(result)) {
                PFN_xrDestroyInstance xrDestroyInstance = nullptr;
                if (XR_SUCCEEDED(apiLayerInfo->nextInfo->nextGetInstanceProcAddr(
                        *instance, "xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyInstance)))) {
                    xrDestroyInstance(*instance);
                }
            }
        }

#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance_Result", TLArg(xr::ToCString(result), "Result"));
#endif
        if (XR_FAILED(result)) {
            ErrorLog("xrCreateApiLayerInstance failed with %d\n", result);
        }

        DebugLog("<-- xrCreateApiLayerInstance %d\n", result);

        return result;
    }

    // Handle cleanup of the layer's singleton.
    XrResult xrDestroyInstance(XrInstance instance) {
#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrDestroyInstance");
#endif

        XrResult result;
        try {
            result = LAYER_NAMESPACE::GetInstance()->xrDestroyInstance(instance);
            if (XR_SUCCEEDED(result)) {
                LAYER_NAMESPACE::ResetInstance();
            }
        } catch (std::runtime_error exc) {
#if USE_TRACELOGGING
            TraceLoggingWrite(g_traceProvider, "xrDestroyInstance_Error", TLArg(exc.what(), "Error"));
#endif
            ErrorLog("xrDestroyInstance: %s\n", exc.what());
            result = XR_ERROR_RUNTIME_FAILURE;
        }

#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrDestroyInstance_Result", TLArg(xr::ToCString(result), "Result"));
#endif
        if (XR_FAILED(result)) {
            ErrorLog("xrDestroyInstance failed with %d\n", result);
        }

        return result;
    }

    // Forward the xrGetInstanceProcAddr() call to the dispatcher.
    XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr");
#endif

        XrResult result;
        try {
            result = LAYER_NAMESPACE::GetInstance()->xrGetInstanceProcAddr(instance, name, function);
        } catch (std::runtime_error exc) {
#if USE_TRACELOGGING
            TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr_Error", TLArg(exc.what(), "Error"));
#endif
            ErrorLog("xrGetInstanceProcAddr: %s\n", exc.what());
            result = XR_ERROR_RUNTIME_FAILURE;
        }

#if USE_TRACELOGGING
        TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr_Result", TLArg(xr::ToCString(result), "Result"));
#endif

        return result;
    }

} // namespace LAYER_NAMESPACE
