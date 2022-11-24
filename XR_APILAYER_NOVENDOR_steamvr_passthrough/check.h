// From OpenXR SDK hello_xr
// Copyright (c) 2017-2021, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pch.h"

#include <openxr/openxr_reflection.h>

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
// clang-format off
#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline const char* to_string(enumType e) {         \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return "Unknown " #enumType;      \
        }                                              \
    }
// clang-format on

MAKE_TO_STRING_FUNC(XrResult);

#define CHK_STRINGIFY(x) #x
#define TOSTRING(x) CHK_STRINGIFY(x)
#define FILE_AND_LINE __FILE__ ":" TOSTRING(__LINE__)

[[noreturn]] inline void Throw(std::string failureMessage, const char* originator = nullptr, const char* sourceLocation = nullptr) {
    if (originator != nullptr) {
        failureMessage += std::format("\n    Origin: %s", originator);
    }
    if (sourceLocation != nullptr) {
        failureMessage += std::format("\n    Source: %s", sourceLocation);
    }

    throw std::logic_error(failureMessage);
}

#define THROW(msg) Throw(msg, nullptr, FILE_AND_LINE);
#define CHECK(exp)                                      \
    {                                                   \
        if (!(exp)) {                                   \
            Throw("Check failed", #exp, FILE_AND_LINE); \
        }                                               \
    }
#define CHECK_MSG(exp, msg)                  \
    {                                        \
        if (!(exp)) {                        \
            Throw(msg, #exp, FILE_AND_LINE); \
        }                                    \
    }

[[noreturn]] inline void ThrowXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
    Throw(std::format("XrResult failure [%s]", to_string(res)), originator, sourceLocation);
}

inline XrResult CheckXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
    if (XR_FAILED(res)) {
        ThrowXrResult(res, originator, sourceLocation);
    }

    return res;
}

#define THROW_XR(xr, cmd) ThrowXrResult(xr, #cmd, FILE_AND_LINE);
#define CHECK_XRCMD(cmd) CheckXrResult(cmd, #cmd, FILE_AND_LINE);
#define CHECK_XRRESULT(res, cmdStr) CheckXrResult(res, cmdStr, FILE_AND_LINE);

#ifdef XR_USE_PLATFORM_WIN32

[[noreturn]] inline void ThrowHResult(HRESULT hr, const char* originator = nullptr, const char* sourceLocation = nullptr) {
    Throw(std::format("HRESULT failure [%x]", hr), originator, sourceLocation);
}

inline HRESULT CheckHResult(HRESULT hr, const char* originator = nullptr, const char* sourceLocation = nullptr) {
    if (FAILED(hr)) {
        ThrowHResult(hr, originator, sourceLocation);
    }

    return hr;
}

#define THROW_HR(hr, cmd) ThrowHResult(hr, #cmd, FILE_AND_LINE);
#define CHECK_HRCMD(cmd) CheckHResult(cmd, #cmd, FILE_AND_LINE);
#define CHECK_HRESULT(res, cmdStr) CheckHResult(res, cmdStr, FILE_AND_LINE);

#endif