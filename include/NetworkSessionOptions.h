#pragma once

#include <chrono>
#include <cstddef>

namespace dnf
{
struct NetworkSessionOptions
{
    std::chrono::milliseconds handshakeTimeout{
        std::chrono::seconds(10)};
    std::chrono::milliseconds authenticationTimeout{
        std::chrono::seconds(30)};
    std::chrono::milliseconds readTimeout{
        std::chrono::minutes(5)};
    std::chrono::milliseconds writeTimeout{
        std::chrono::seconds(10)};
    std::size_t maxPendingWriteMessages = 64;
    std::size_t maxPendingWriteBytes = 512 * 1024;

    bool IsValid() const
    {
        return handshakeTimeout.count() > 0 &&
               authenticationTimeout.count() > 0 &&
               readTimeout.count() > 0 &&
               writeTimeout.count() > 0 &&
               maxPendingWriteMessages > 0 &&
               maxPendingWriteBytes > 0;
    }
};
} // namespace dnf
