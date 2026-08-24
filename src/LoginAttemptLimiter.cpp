#include "LoginAttemptLimiter.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace dnf
{
namespace
{
using Clock = std::chrono::steady_clock;
using AttemptHistory = std::deque<Clock::time_point>;

std::string NormalizeAccountLoginId(std::string loginId)
{
    std::transform(
        loginId.begin(),
        loginId.end(),
        loginId.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return loginId;
}

template <typename Map>
void PruneExpired(
    Map& attempts,
    Clock::time_point cutoff)
{
    for (auto entry = attempts.begin(); entry != attempts.end();)
    {
        AttemptHistory& history = entry->second;
        while (!history.empty() && history.front() <= cutoff)
        {
            history.pop_front();
        }

        if (history.empty())
        {
            entry = attempts.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
}

template <typename Map>
typename Map::iterator FindAndPrune(
    Map& attempts,
    const std::string& key,
    Clock::time_point cutoff)
{
    auto entry = attempts.find(key);
    if (entry == attempts.end())
    {
        return entry;
    }

    AttemptHistory& history = entry->second;
    while (!history.empty() && history.front() <= cutoff)
    {
        history.pop_front();
    }

    if (history.empty())
    {
        attempts.erase(entry);
        return attempts.end();
    }

    return entry;
}
} // namespace

struct LoginAttemptLimiter::State
{
    explicit State(LoginAttemptLimiterOptions value)
        : options(std::move(value))
    {
    }

    LoginAttemptLimiterOptions options;
    mutable std::mutex mutex;
    std::unordered_map<std::string, AttemptHistory> ipAttempts;
    std::unordered_map<std::string, AttemptHistory> accountAttempts;
    LoginAttemptLimiterStats stats;
};

bool LoginAttemptLimiterOptions::IsValid() const
{
    return maxAttemptsPerIp > 0 &&
           maxAttemptsPerAccount > 0 &&
           maxConcurrentPasswordJobs > 0 &&
           maxTrackedIps > 0 &&
           maxTrackedAccounts > 0 &&
           attemptWindow > std::chrono::milliseconds::zero();
}

LoginAttemptPermit::LoginAttemptPermit(
    std::function<void()> release)
    : release_(std::move(release))
{
    if (!release_)
    {
        throw std::invalid_argument(
            "Login attempt permit release is required");
    }
}

LoginAttemptPermit::~LoginAttemptPermit()
{
    release_();
}

LoginAttemptLimiter::LoginAttemptLimiter(
    LoginAttemptLimiterOptions options)
{
    if (!options.IsValid())
    {
        throw std::invalid_argument(
            "Login attempt limiter options are invalid");
    }

    state_ = std::make_shared<State>(std::move(options));
}

LoginAdmission LoginAttemptLimiter::TryAcquire(
    const std::string& clientAddress,
    const std::string& accountLoginId)
{
    const std::string ipKey = clientAddress.empty()
        ? "unknown"
        : clientAddress;
    const std::string accountKey =
        NormalizeAccountLoginId(accountLoginId);
    const auto now = Clock::now();

    std::lock_guard lock(state_->mutex);
    const auto cutoff = now - state_->options.attemptWindow;
    auto ip = FindAndPrune(state_->ipAttempts, ipKey, cutoff);
    if (ip != state_->ipAttempts.end() &&
        ip->second.size() >= state_->options.maxAttemptsPerIp)
    {
        ++state_->stats.rejectedByIp;
        return {LoginAdmissionStatus::RateLimited, nullptr};
    }

    auto account = FindAndPrune(
        state_->accountAttempts,
        accountKey,
        cutoff);
    if (account != state_->accountAttempts.end() &&
        account->second.size() >=
            state_->options.maxAttemptsPerAccount)
    {
        ++state_->stats.rejectedByAccount;
        return {LoginAdmissionStatus::RateLimited, nullptr};
    }

    bool needsIpEntry = ip == state_->ipAttempts.end();
    bool needsAccountEntry = account == state_->accountAttempts.end();

    if (needsIpEntry &&
        state_->ipAttempts.size() >= state_->options.maxTrackedIps)
    {
        PruneExpired(state_->ipAttempts, cutoff);
        needsIpEntry = !state_->ipAttempts.contains(ipKey);
    }
    if (needsAccountEntry &&
        state_->accountAttempts.size() >=
            state_->options.maxTrackedAccounts)
    {
        PruneExpired(state_->accountAttempts, cutoff);
        needsAccountEntry =
            !state_->accountAttempts.contains(accountKey);
    }

    if ((needsIpEntry &&
         state_->ipAttempts.size() >= state_->options.maxTrackedIps) ||
        (needsAccountEntry &&
         state_->accountAttempts.size() >=
             state_->options.maxTrackedAccounts))
    {
        ++state_->stats.rejectedByTrackingCapacity;
        return {LoginAdmissionStatus::ServiceBusy, nullptr};
    }

    if (state_->stats.concurrentPasswordJobs >=
        state_->options.maxConcurrentPasswordJobs)
    {
        ++state_->stats.rejectedByConcurrency;
        return {LoginAdmissionStatus::ServiceBusy, nullptr};
    }

    state_->ipAttempts[ipKey].push_back(now);
    state_->accountAttempts[accountKey].push_back(now);
    ++state_->stats.concurrentPasswordJobs;
    ++state_->stats.acceptedAttempts;

    const std::shared_ptr<State> state = state_;
    auto permit = std::make_shared<LoginAttemptPermit>(
        [state]
        {
            std::lock_guard permitLock(state->mutex);
            if (state->stats.concurrentPasswordJobs > 0)
            {
                --state->stats.concurrentPasswordJobs;
            }
        });

    return {LoginAdmissionStatus::Accepted, std::move(permit)};
}

LoginAttemptLimiterStats LoginAttemptLimiter::Stats() const
{
    std::lock_guard lock(state_->mutex);
    LoginAttemptLimiterStats output = state_->stats;
    output.trackedIps = state_->ipAttempts.size();
    output.trackedAccounts = state_->accountAttempts.size();
    return output;
}
} // namespace dnf
