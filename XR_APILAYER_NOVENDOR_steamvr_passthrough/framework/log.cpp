// MIT License
//
// Copyright(c) 2022 Matthieu Bucchianeri
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
#include "pathutil.h"


namespace LAYER_NAMESPACE::logging {

#if USE_TRACELOGGING
    // {030e3bd6-23f4-442a-b87e-f1fe947c123e}
    TRACELOGGING_DEFINE_PROVIDER(g_traceProvider,
                                 "OpenXRSteamVRPassthrough",
                                 (0x030e3bd6, 0x23f4, 0x442a, 0xb8, 0x7e, 0xf1, 0xfe, 0x94, 0x7c, 0x12, 0x3e));

    TraceLoggingActivity<g_traceProvider> g_traceActivity;
#endif


} // namespace LAYER_NAMESPACE::logging
