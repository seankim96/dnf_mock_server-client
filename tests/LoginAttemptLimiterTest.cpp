#include "LoginAttemptLimiter.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
dnf::LoginAttemptLimiterOptions MakeOptions()
{
    dnf::LoginAttemptLimiterOptions options;
    options.maxAttemptsPerIp = 10;
    options.maxAttemptsPerAccount = 10;
    options.maxConcurrentPasswordJobs = 2;
    options.maxTrackedIps = 10;
    options.maxTrackedAccounts = 10;
    return options;
}

void TestConcurrentPasswordJobsAreBounded()
{
    dnf::LoginAttemptLimiterOptions options = MakeOptions();
    options.maxConcurrentPasswordJobs = 1;
    dnf::LoginAttemptLimiter limiter(options);

    dnf::LoginAdmission first =
        limiter.TryAcquire("127.0.0.1", "account_1");
    assert(first.status == dnf::LoginAdmissionStatus::Accepted);
    assert(first.permit);

    const dnf::LoginAdmission busy =
        limiter.TryAcquire("127.0.0.2", "account_2");
    assert(busy.status == dnf::LoginAdmissionStatus::ServiceBusy);
    assert(!busy.permit);

    first.permit.reset();
    const dnf::LoginAdmission afterRelease =
        limiter.TryAcquire("127.0.0.2", "account_2");
    assert(afterRelease.status ==
           dnf::LoginAdmissionStatus::Accepted);

    const dnf::LoginAttemptLimiterStats stats = limiter.Stats();
    assert(stats.acceptedAttempts == 2);
    assert(stats.rejectedByConcurrency == 1);
}

void TestIpAndAccountWindowsAreIndependent()
{
    dnf::LoginAttemptLimiterOptions ipOptions = MakeOptions();
    ipOptions.maxAttemptsPerIp = 2;
    dnf::LoginAttemptLimiter ipLimiter(ipOptions);

    auto first = ipLimiter.TryAcquire("10.0.0.1", "account_1");
    first.permit.reset();
    auto second = ipLimiter.TryAcquire("10.0.0.1", "account_2");
    second.permit.reset();
    const dnf::LoginAdmission ipLimited =
        ipLimiter.TryAcquire("10.0.0.1", "account_3");
    assert(ipLimited.status ==
           dnf::LoginAdmissionStatus::RateLimited);
    assert(ipLimiter.Stats().rejectedByIp == 1);

    dnf::LoginAttemptLimiterOptions accountOptions = MakeOptions();
    accountOptions.maxAttemptsPerAccount = 1;
    dnf::LoginAttemptLimiter accountLimiter(accountOptions);

    auto accountFirst =
        accountLimiter.TryAcquire("10.0.0.1", "Account_1");
    accountFirst.permit.reset();
    const dnf::LoginAdmission accountLimited =
        accountLimiter.TryAcquire("10.0.0.2", "account_1");
    assert(accountLimited.status ==
           dnf::LoginAdmissionStatus::RateLimited);
    assert(accountLimiter.Stats().rejectedByAccount == 1);
}

void TestTrackingCapacityIsBounded()
{
    dnf::LoginAttemptLimiterOptions options = MakeOptions();
    options.maxTrackedIps = 1;
    options.maxTrackedAccounts = 1;
    dnf::LoginAttemptLimiter limiter(options);

    auto first = limiter.TryAcquire("10.0.0.1", "account_1");
    first.permit.reset();

    const dnf::LoginAdmission full =
        limiter.TryAcquire("10.0.0.2", "account_2");
    assert(full.status == dnf::LoginAdmissionStatus::ServiceBusy);
    assert(limiter.Stats().rejectedByTrackingCapacity == 1);
}

void TestInvalidOptionsAreRejected()
{
    dnf::LoginAttemptLimiterOptions options = MakeOptions();
    options.maxConcurrentPasswordJobs = 0;

    bool rejected = false;
    try
    {
        dnf::LoginAttemptLimiter limiter(options);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    assert(rejected);
}
} // namespace

int main()
{
    TestConcurrentPasswordJobsAreBounded();
    TestIpAndAccountWindowsAreIndependent();
    TestTrackingCapacityIsBounded();
    TestInvalidOptionsAreRejected();

    std::cout << "All login attempt limiter tests passed.\n";
    return 0;
}
