#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dnf
{
struct LoginAttemptLimiterOptions
{
    std::size_t maxAttemptsPerIp = 30;
    std::size_t maxAttemptsPerAccount = 10;
    std::size_t maxConcurrentPasswordJobs = 2;
    std::size_t maxTrackedIps = 4096;
    std::size_t maxTrackedAccounts = 16384;
    std::chrono::milliseconds attemptWindow =
        std::chrono::minutes(1);

    bool IsValid() const;
};

struct LoginAttemptLimiterStats
{
    std::size_t trackedIps = 0;
    std::size_t trackedAccounts = 0;
    std::size_t concurrentPasswordJobs = 0;
    std::uint64_t acceptedAttempts = 0;
    std::uint64_t rejectedByIp = 0;
    std::uint64_t rejectedByAccount = 0;
    std::uint64_t rejectedByConcurrency = 0;
    std::uint64_t rejectedByTrackingCapacity = 0;
};

enum class LoginAdmissionStatus
{
    Accepted,
    RateLimited,
    ServiceBusy
};

class LoginAttemptPermit
{
public:
    explicit LoginAttemptPermit(std::function<void()> release);
    ~LoginAttemptPermit();

    LoginAttemptPermit(const LoginAttemptPermit&) = delete;
    LoginAttemptPermit& operator=(const LoginAttemptPermit&) = delete;

private:
    std::function<void()> release_;
};

struct LoginAdmission
{
    LoginAdmissionStatus status = LoginAdmissionStatus::ServiceBusy;
    std::shared_ptr<LoginAttemptPermit> permit;
};

class LoginAttemptLimiter
{
public:
    explicit LoginAttemptLimiter(
        LoginAttemptLimiterOptions options = {});

    LoginAdmission TryAcquire(
        const std::string& clientAddress,
        const std::string& accountLoginId);

    LoginAttemptLimiterStats Stats() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace dnf
