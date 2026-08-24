#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

namespace dnf
{
class SessionDeadline
{
public:
    using TimeoutHandler = std::function<void()>;

    explicit SessionDeadline(boost::asio::any_io_executor executor)
        : timer_(std::move(executor))
    {
    }

    void Start(
        std::chrono::milliseconds timeout,
        TimeoutHandler timeoutHandler)
    {
        // 취소된 이전 타이머의 콜백이 늦게 도착해도 새 작업을 닫지 않는다.
        const std::uint64_t generation = ++generation_;
        timer_.expires_after(timeout);
        timer_.async_wait(
            [this,
             generation,
             timeoutHandler = std::move(timeoutHandler)](
                const boost::system::error_code& error)
            {
                if (!error && generation == generation_)
                {
                    timeoutHandler();
                }
            });
    }

    void Cancel()
    {
        ++generation_;
        timer_.cancel();
    }

private:
    boost::asio::steady_timer timer_;
    std::uint64_t generation_ = 0;
};
} // namespace dnf
