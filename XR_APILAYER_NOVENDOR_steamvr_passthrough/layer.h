// MIT License
//
// Copyright(c) 2022 Rectus
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

#pragma once

#include "framework/dispatch.gen.h"

namespace steamvr_passthrough
{

    const std::string LayerName = "XR_APILAYER_NOVENDOR_steamvr_passthrough";
    const std::string VersionString = "0.1.3";

    // Singleton accessor.
    OpenXrApi* GetInstance();

    // A function to reset (delete) the singleton.
    void ResetInstance();

} // namespace steamvr_passthrough

enum ERenderAPI
{
    None,
    DirectX11,
    DirectX12,
    Vulkan,
    OpenGL
};