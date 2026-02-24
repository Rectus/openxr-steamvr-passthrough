
#pragma once

#include "spdlog/details/circular_q.h"
#include "spdlog/details/log_msg_buffer.h"
#include "spdlog/details/null_mutex.h"
#include "spdlog/sinks/base_sink.h"
#include "imgui.h"


template <typename Mutex>
class spdlog_imgui_buffer_sink final : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit spdlog_imgui_buffer_sink(size_t n_items)
        : buffer_{ n_items }
    {
    }

    void draw_log(const char* alt_color_logger = "")
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);
        auto items_available = buffer_.size();

        spdlog::memory_buf_t formatted;

        for (size_t i = 0; i < items_available; i++)
        {
            formatted.clear();
            spdlog::sinks::base_sink<Mutex>::formatter_->format(buffer_.at(i), formatted);

            if (buffer_.at(i).level == spdlog::level::warn)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), SPDLOG_BUF_TO_STRING(formatted).data());
            }        
            else if (buffer_.at(i).level == spdlog::level::err)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), SPDLOG_BUF_TO_STRING(formatted).data());
            }
            else if (buffer_.at(i).logger_name == alt_color_logger)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), SPDLOG_BUF_TO_STRING(formatted).data());
            }
            else
            {
                ImGui::Text(SPDLOG_BUF_TO_STRING(formatted).data());
            }
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        buffer_.push_back(spdlog::details::log_msg_buffer{ msg });
    }
    void flush_() override {}

private:
    spdlog::details::circular_q<spdlog::details::log_msg_buffer> buffer_;
};

using spdlog_imgui_buffer_sink_mt = spdlog_imgui_buffer_sink<std::mutex>;
using spdlog_imgui_buffer_sink_st = spdlog_imgui_buffer_sink<spdlog::details::null_mutex>;

