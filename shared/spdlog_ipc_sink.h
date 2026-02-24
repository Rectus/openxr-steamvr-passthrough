
#pragma once

#include "spdlog/sinks/base_sink.h"
#include "spdlog/details/circular_q.h"
#include "spdlog/details/log_msg_buffer.h"
#include "menu_ipc_client.h"

template<typename Mutex>
class spdlog_ipc_sink : public spdlog::sinks::base_sink <Mutex>
{
public:
    spdlog_ipc_sink(std::shared_ptr<MenuIPCClient> ipc_client, size_t buffer_size)
        : spdlog::sinks::base_sink<Mutex>()
        , ipc_client_(ipc_client)
        , buffer_(buffer_size)
    {
    }

protected:

    bool write_next_to_ipc_(bool bBlock)
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(buffer_.front(), formatted);

        MenuIPCMessage message = {};

        int messageSize = static_cast<int>(min(formatted.size(), IPC_PAYLOAD_SIZE - 1));
        message.Header.Type = MessageType_Log;
        message.Header.PayloadSize = messageSize + 1;
        message.Payload[0] = static_cast<uint8_t>(buffer_.front().level);
        memcpy(message.Payload + 1, formatted.data(), messageSize);
        if (!ipc_client_.lock()->WriteMessage(message, bBlock))
        {
            return false;
        }
        buffer_.pop_front();
        return true;
    }

    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        // Drop new messages on buffer overrun.
        if (buffer_.full())
        {
            return;
        }

        buffer_.push_back(spdlog::details::log_msg_buffer{ msg });
    }

    void flush_() override
    {
        while (!buffer_.empty())
        {
            if (!write_next_to_ipc_(false))
            {
                break;
            }
        }
    }

private:
    spdlog::details::circular_q<spdlog::details::log_msg_buffer> buffer_;
    std::weak_ptr<MenuIPCClient> ipc_client_;
};

#include "spdlog/details/null_mutex.h"
#include <mutex>
using spdlog_ipc_sink_mt = spdlog_ipc_sink<std::mutex>;
using spdlog_ipc_sink_st = spdlog_ipc_sink<spdlog::details::null_mutex>;