
#pragma once

#include "spdlog/details/circular_q.h"
#include "spdlog/details/log_msg_buffer.h"
#include "spdlog/details/null_mutex.h"
#include "spdlog/sinks/base_sink.h"
#include "imgui.h"


// Modified log_msg_buffer that formats messages when inserted.
class log_msg_formatted : public spdlog::details::log_msg
{
    fmt::basic_memory_buffer<char, 250> buffer;

    void update_string_views()
    {
        logger_name = fmt::v12::string_view{ buffer.data(), logger_name.size() };
        payload = fmt::v12::string_view{ buffer.data() + logger_name.size(), payload.size() };
    }

public:
    log_msg_formatted() = default;

    explicit log_msg_formatted(const log_msg& orig_msg, spdlog::formatter* formatter)
        : log_msg{ orig_msg }
    {
        buffer.append(logger_name.begin(), logger_name.end());

        formatter->format(orig_msg, buffer);

        // Overwrite line end, assuming \r\n
        buffer[buffer.size() - 1] = '\0';
        buffer[buffer.size() - 2] = '\0';

        logger_name = fmt::v12::string_view{ buffer.data(), logger_name.size() };

        // The formatter adds line endsings without increasing the payload size, ofsetting by 2 so they get read.
        payload = fmt::v12::string_view{ buffer.data() + logger_name.size(), payload.size() + 2 };
    }

    explicit log_msg_formatted(const log_msg& orig_msg)
        : log_msg{ orig_msg }
    {
        buffer.append(logger_name.begin(), logger_name.end());
        buffer.append(payload.begin(), payload.end());
        update_string_views();
    }

    log_msg_formatted(const log_msg_formatted& other)
        : log_msg{ other }
    {
        buffer.append(logger_name.begin(), logger_name.end());
        buffer.append(payload.begin(), payload.end());
        update_string_views();
    }

    log_msg_formatted(log_msg_formatted&& other) noexcept
        : log_msg{ other }
        , buffer{ std::move(other.buffer) }
    {
        update_string_views();
    }

    log_msg_formatted& operator=(const log_msg_formatted& other)
    {
        log_msg::operator=(other);
        buffer.clear();
        buffer.append(other.buffer.data(), other.buffer.data() + other.buffer.size());
        update_string_views();
        return *this;
    }

    log_msg_formatted& operator=(log_msg_formatted&& other) noexcept
    {
        log_msg::operator=(other);
        buffer = std::move(other.buffer);
        update_string_views();
        return *this;
    }
};

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

        for (size_t i = 0; i < items_available; i++)
        {
            const char* formatted = buffer_.at(i).payload.data();

            if (buffer_.at(i).level == spdlog::level::warn)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), formatted);
            }        
            else if (buffer_.at(i).level == spdlog::level::err)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), formatted);
            }
            else if (buffer_.at(i).logger_name == alt_color_logger)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.95f, 1.0f), formatted);
            }
            else
            {
                ImGui::Text(formatted);
            }
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
 
        buffer_.push_back(log_msg_formatted{ msg, spdlog::sinks::base_sink<Mutex>::formatter_.get()}); 
    }
    void flush_() override {}

private:
    spdlog::details::circular_q<log_msg_formatted> buffer_;

};

using spdlog_imgui_buffer_sink_mt = spdlog_imgui_buffer_sink<std::mutex>;
using spdlog_imgui_buffer_sink_st = spdlog_imgui_buffer_sink<spdlog::details::null_mutex>;

