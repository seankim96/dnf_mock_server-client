#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <variant>

namespace dnf
{
using MetricValue = std::variant<
    bool,
    std::int64_t,
    std::uint64_t,
    double,
    std::string>;

class MetricRecord
{
public:
    void Set(std::string name, MetricValue value);

    std::string ToJson(
        const std::string& serverName,
        std::chrono::system_clock::time_point timestamp =
            std::chrono::system_clock::now()) const;

private:
    std::map<std::string, MetricValue> values_;
};

class OperationalMetricsReporter
{
public:
    using SnapshotProvider = std::function<MetricRecord()>;

    OperationalMetricsReporter(
        boost::asio::io_context& ioContext,
        std::string serverName,
        SnapshotProvider snapshotProvider,
        std::chrono::milliseconds interval = std::chrono::seconds(10),
        std::ostream& output = DefaultOutput());
    ~OperationalMetricsReporter();

    OperationalMetricsReporter(const OperationalMetricsReporter&) = delete;
    OperationalMetricsReporter& operator=(
        const OperationalMetricsReporter&) = delete;

    void Start();
    void Stop();
    void EmitNow();

private:
    static std::ostream& DefaultOutput();
    void ScheduleNext();
    void HandleTimer(const boost::system::error_code& error);

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer timer_;
    std::string serverName_;
    SnapshotProvider snapshotProvider_;
    std::chrono::milliseconds interval_;
    std::ostream& output_;
    bool running_ = false;
};
} // namespace dnf
