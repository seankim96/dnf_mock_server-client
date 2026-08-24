#include "OperationalMetrics.h"

#include <boost/asio/dispatch.hpp>

#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dnf
{
namespace
{
std::string EscapeJson(const std::string& value)
{
    std::ostringstream escaped;
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (character < 0x20)
            {
                escaped << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned int>(character)
                        << std::dec;
            }
            else
            {
                escaped << static_cast<char>(character);
            }
            break;
        }
    }

    return escaped.str();
}

std::tm UtcTime(std::time_t timestamp)
{
    std::tm result{};
#ifdef _WIN32
    if (gmtime_s(&result, &timestamp) != 0)
    {
        throw std::runtime_error("Failed to format metric timestamp");
    }
#else
    if (gmtime_r(&timestamp, &result) == nullptr)
    {
        throw std::runtime_error("Failed to format metric timestamp");
    }
#endif
    return result;
}

std::string FormatTimestamp(
    std::chrono::system_clock::time_point timestamp)
{
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch());
    const auto wholeSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(milliseconds);
    const auto fractionalMilliseconds = milliseconds - wholeSeconds;
    const std::time_t time =
        std::chrono::system_clock::to_time_t(timestamp);
    const std::tm utc = UtcTime(time);

    std::ostringstream formatted;
    formatted << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
              << '.' << std::setw(3) << std::setfill('0')
              << fractionalMilliseconds.count() << 'Z';
    return formatted.str();
}

void AppendMetricValue(std::ostringstream& output, const MetricValue& value)
{
    std::visit(
        [&output](const auto& typedValue)
        {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, bool>)
            {
                output << (typedValue ? "true" : "false");
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                output << '"' << EscapeJson(typedValue) << '"';
            }
            else if constexpr (std::is_same_v<ValueType, double>)
            {
                if (!std::isfinite(typedValue))
                {
                    throw std::invalid_argument(
                        "Metric floating-point value must be finite");
                }

                output << std::setprecision(15) << typedValue;
            }
            else
            {
                output << typedValue;
            }
        },
        value);
}
} // namespace

void MetricRecord::Set(std::string name, MetricValue value)
{
    if (name.empty())
    {
        throw std::invalid_argument("Metric name must not be empty");
    }

    values_.insert_or_assign(std::move(name), std::move(value));
}

std::string MetricRecord::ToJson(
    const std::string& serverName,
    std::chrono::system_clock::time_point timestamp) const
{
    if (serverName.empty())
    {
        throw std::invalid_argument("Metric server name must not be empty");
    }

    std::ostringstream output;
    output << "{\"timestamp\":\"" << FormatTimestamp(timestamp)
           << "\",\"level\":\"info\",\"event\":\"metrics\""
           << ",\"server\":\"" << EscapeJson(serverName) << '"';

    for (const auto& [name, value] : values_)
    {
        output << ",\"" << EscapeJson(name) << "\":";
        AppendMetricValue(output, value);
    }

    output << '}';
    return output.str();
}

OperationalMetricsReporter::OperationalMetricsReporter(
    boost::asio::io_context& ioContext,
    std::string serverName,
    SnapshotProvider snapshotProvider,
    std::chrono::milliseconds interval,
    std::ostream& output)
    : strand_(boost::asio::make_strand(ioContext)),
      timer_(strand_),
      serverName_(std::move(serverName)),
      snapshotProvider_(std::move(snapshotProvider)),
      interval_(interval),
      output_(output)
{
    if (serverName_.empty() || !snapshotProvider_ ||
        interval_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "Operational metrics reporter configuration is invalid");
    }
}

OperationalMetricsReporter::~OperationalMetricsReporter()
{
    try
    {
        static_cast<void>(timer_.cancel());
    }
    catch (...)
    {
    }
}

void OperationalMetricsReporter::Start()
{
    boost::asio::dispatch(
        strand_,
        [this]
        {
            if (running_)
            {
                return;
            }

            running_ = true;
            ScheduleNext();
        });
}

void OperationalMetricsReporter::Stop()
{
    boost::asio::dispatch(
        strand_,
        [this]
        {
            running_ = false;
            static_cast<void>(timer_.cancel());
        });
}

void OperationalMetricsReporter::EmitNow()
{
    const MetricRecord snapshot = snapshotProvider_();
    output_ << snapshot.ToJson(serverName_) << '\n';
}

std::ostream& OperationalMetricsReporter::DefaultOutput()
{
    return std::cout;
}

void OperationalMetricsReporter::ScheduleNext()
{
    timer_.expires_after(interval_);
    timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            HandleTimer(error);
        });
}

void OperationalMetricsReporter::HandleTimer(
    const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || !running_)
    {
        return;
    }

    if (error)
    {
        output_ << "{\"level\":\"error\",\"event\":"
                   "\"metrics_timer_error\",\"server\":\""
                << EscapeJson(serverName_) << "\",\"error\":\""
                << EscapeJson(error.message()) << "\"}\n";
        running_ = false;
        return;
    }

    try
    {
        EmitNow();
    }
    catch (const std::exception& exception)
    {
        output_ << "{\"level\":\"error\",\"event\":"
                   "\"metrics_snapshot_error\",\"server\":\""
                << EscapeJson(serverName_) << "\",\"error\":\""
                << EscapeJson(exception.what()) << "\"}\n";
    }

    if (running_)
    {
        ScheduleNext();
    }
}
} // namespace dnf
