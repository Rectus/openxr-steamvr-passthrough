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

namespace {
    constexpr uint32_t k_maxLoggedErrors = 100;
    uint32_t g_globalErrorCount = 0;
    constexpr uint32_t k_maxBufferedLines = 200;
    std::deque<std::string> g_logBuffer;
    std::shared_mutex g_logBufferMutex;
} // namespace

namespace LAYER_NAMESPACE::log {
    extern std::ofstream logStream;

#if USE_TRACELOGGING
    // {030e3bd6-23f4-442a-b87e-f1fe947c123e}
    TRACELOGGING_DEFINE_PROVIDER(g_traceProvider,
                                 "OpenXRSteamVRPassthrough",
                                 (0x030e3bd6, 0x23f4, 0x442a, 0xb8, 0x7e, 0xf1, 0xfe, 0x94, 0x7c, 0x12, 0x3e));

    TraceLoggingActivity<g_traceProvider> g_traceActivity;
#endif

    namespace {

        void BufferLog(const char* buf)
        {
            std::unique_lock writeLock(g_logBufferMutex);

            if (!writeLock.owns_lock())
            {
                return;
            }

            g_logBuffer.emplace_back(buf);

            if (g_logBuffer.size() >= k_maxBufferedLines)
            {
                g_logBuffer.pop_front();
            }
        }

        // Utility logging function.
        void InternalLog(const char* fmt, va_list va) {
            const std::time_t now = std::time(nullptr);

            char buf[1024];
            size_t offset = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z: ", std::localtime(&now));
            vsnprintf_s(buf + offset, sizeof(buf) - offset, _TRUNCATE, fmt, va);

            BufferLog(buf);
            OutputDebugStringA(buf);
            if (logStream.is_open()) {
                logStream << buf;
                logStream.flush();
            }
        }
    } // namespace

    void Log(const char* fmt, ...) {
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
    }

    void ErrorLog(const char* fmt, ...) {
        if (g_globalErrorCount++ < k_maxLoggedErrors) {
            va_list va;
            va_start(va, fmt);
            InternalLog(fmt, va);
            va_end(va);
            if (g_globalErrorCount == k_maxLoggedErrors) {
                Log("Maximum number of errors logged. Going silent.");
            }
        }
    }

    void DebugLog(const char* fmt, ...) {
#ifdef _DEBUG
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
#endif
    }

    void ReadLogBuffer(void (*printFunc)(std::deque<std::string>& logBuffer))
    {
        std::shared_lock readLock(g_logBufferMutex);

        if (!readLock.owns_lock())
        {
            return;
        }

        printFunc(g_logBuffer);
    }

} // namespace LAYER_NAMESPACE::log
