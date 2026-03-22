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
#include "pathutil.h"

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/dup_filter_sink.h"
#include "spdlog/sinks/msvc_sink.h"

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

std::shared_ptr<spdlog::logger> g_logger = nullptr;
std::shared_ptr<spdlog::sinks::dup_filter_sink_mt> g_logSinkAggregator = nullptr;

using namespace LAYER_NAMESPACE;

extern "C" {

// Entry point for the loader.
XrResult __declspec(dllexport) XRAPI_CALL
    xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* const loaderInfo,
                                       const char* const apiLayerName,
                                       XrNegotiateApiLayerRequest* const apiLayerRequest) {
#if USE_TRACELOGGING
    TraceLoggingWrite(g_traceProvider, "xrNegotiateLoaderApiLayerInterface");
#endif

    // Start logging to file.
    if (!g_logger.get())
    {
        g_logSinkAggregator = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(std::chrono::minutes(10));

        std::string logFilePath = GetLocalAppData() + LOG_FILE_DIR +"\\client_" + GetProcessFileName(false) + ".log";
        CreateDirectoryPath(GetLocalAppData() + LOG_FILE_DIR);

        g_logSinkAggregator->add_sink(std::make_shared<spdlog::sinks::basic_file_sink_mt>(ToWideString(logFilePath), true));
        g_logSinkAggregator->add_sink(std::make_shared<spdlog::sinks::msvc_sink_mt>());

        spdlog::init_thread_pool(100, 1);
        g_logger = std::make_shared<spdlog::async_logger>("menu", g_logSinkAggregator, spdlog::thread_pool(), spdlog::async_overflow_policy::discard_new);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        g_logger->flush_on(spdlog::level::err);
        spdlog::set_default_logger(g_logger);
        spdlog::flush_every(std::chrono::seconds(10));
    }

#ifdef _DEBUG
    g_logger->debug("--> xrNegotiateLoaderApiLayerInterface");
#endif

    if (apiLayerName && apiLayerName != LayerName) {
        g_logger->error("Invalid apiLayerName \"{}\"", apiLayerName);
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (!loaderInfo || !apiLayerRequest || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
        apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION || loaderInfo->minApiVersion > XR_CURRENT_API_VERSION) {
        g_logger->error("xrNegotiateLoaderApiLayerInterface validation failed");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    // Setup our layer to intercept OpenXR calls.
    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(xrGetInstanceProcAddr);
    apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(xrCreateApiLayerInstance);

#ifdef _DEBUG
    g_logger->debug("<-- xrNegotiateLoaderApiLayerInterface");
#endif

    g_logger->info("{} layer ({}) is active", LayerName.c_str(), VersionString.c_str());

#if USE_TRACELOGGING
    TraceLoggingWrite(g_traceProvider, "xrNegotiateLoaderApiLayerInterface_Complete");
#endif

    return XR_SUCCESS;
}
}
