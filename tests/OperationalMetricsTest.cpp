#include "OperationalMetrics.h"

#include <boost/asio/io_context.hpp>

#include <cassert>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
void TestMetricRecordProducesStableJson()
{
    dnf::MetricRecord record;
    record.Set("active_sessions", std::uint64_t{4});
    record.Set("queue_busy", false);
    record.Set("tick_p95_ms", 2.5);
    record.Set("state", std::string("ready\nnow"));

    const auto timestamp =
        std::chrono::system_clock::time_point(std::chrono::milliseconds(123));
    const std::string json = record.ToJson("game-server", timestamp);

    assert(json ==
           "{\"timestamp\":\"1970-01-01T00:00:00.123Z\","
           "\"level\":\"info\",\"event\":\"metrics\","
           "\"server\":\"game-server\",\"active_sessions\":4,"
           "\"queue_busy\":false,\"state\":\"ready\\nnow\","
           "\"tick_p95_ms\":2.5}");
}

void TestInvalidMetricValuesAreRejected()
{
    dnf::MetricRecord record;

    bool emptyNameRejected = false;
    try
    {
        record.Set({}, std::uint64_t{1});
    }
    catch (const std::invalid_argument&)
    {
        emptyNameRejected = true;
    }
    assert(emptyNameRejected);

    record.Set("invalid", std::numeric_limits<double>::infinity());
    bool nonFiniteRejected = false;
    try
    {
        static_cast<void>(record.ToJson("game-server"));
    }
    catch (const std::invalid_argument&)
    {
        nonFiniteRejected = true;
    }
    assert(nonFiniteRejected);
}

void TestReporterCanEmitOnDemand()
{
    boost::asio::io_context ioContext;
    std::ostringstream output;
    dnf::OperationalMetricsReporter reporter(
        ioContext,
        "auth-server",
        []
        {
            dnf::MetricRecord record;
            record.Set("database_queue_depth", std::uint64_t{3});
            return record;
        },
        std::chrono::seconds(10),
        output);

    reporter.EmitNow();
    assert(output.str().find("\"server\":\"auth-server\"") !=
           std::string::npos);
    assert(output.str().find("\"database_queue_depth\":3") !=
           std::string::npos);
}
} // namespace

int main()
{
    TestMetricRecordProducesStableJson();
    TestInvalidMetricValuesAreRejected();
    TestReporterCanEmitOnDemand();
    return 0;
}
