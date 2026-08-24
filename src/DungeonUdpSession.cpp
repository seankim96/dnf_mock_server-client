#include "DungeonUdpSession.h"

#include "DungeonProtocol.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace dnf
{
using boost::asio::ip::udp;

namespace
{
constexpr std::uint32_t SEQUENCE_HALF_RANGE =
    std::uint32_t{1} << 31;

bool IsNewerSequence(
    std::uint32_t candidate,
    std::uint32_t previous)
{
    // unsigned 뺄셈의 wrap을 이용하고, 반 바퀴 미만만 앞선 값으로 본다.
    const std::uint32_t distance = candidate - previous;
    return distance != 0 && distance < SEQUENCE_HALF_RANGE;
}
} // namespace

DungeonUdpSession::DungeonUdpSession(
    DungeonId dungeonId,
    udp::socket socket,
    TokenMap tokens,
    std::shared_ptr<const std::atomic<std::uint32_t>> serverTick)
    : dungeonId_(dungeonId),
      serverTick_(std::move(serverTick)),
      socket_(std::move(socket)),
      strand_(boost::asio::make_strand(socket_.get_executor())),
      port_(socket_.local_endpoint().port()),
      tokens_(std::move(tokens))
{
}

void DungeonUdpSession::Start()
{
    const auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            self->StartReceive();
        });
}

void DungeonUdpSession::Stop()
{
    if (stopped_.exchange(true))
    {
        return;
    }

    {
        std::lock_guard lock(snapshotMutex_);
        pendingSnapshot_.reset();
        snapshotPumpScheduled_ = false;
        snapshotStats_.snapshotPending = false;
    }

    const auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            boost::system::error_code error;
            self->socket_.close(error);
        });
}

std::uint16_t DungeonUdpSession::Port() const
{
    return port_;
}

std::optional<DungeonUdpToken> DungeonUdpSession::FindToken(
    SessionId sessionId) const
{
    std::lock_guard lock(stateMutex_);

    const auto tokenIt = tokens_.find(sessionId);
    if (tokenIt == tokens_.end())
    {
        return std::nullopt;
    }

    return tokenIt->second;
}

std::optional<udp::endpoint> DungeonUdpSession::FindEndpoint(
    SessionId sessionId) const
{
    std::lock_guard lock(stateMutex_);

    const auto endpointIt = endpoints_.find(sessionId);
    if (endpointIt == endpoints_.end())
    {
        return std::nullopt;
    }

    return endpointIt->second;
}

bool DungeonUdpSession::DisconnectParticipant(SessionId sessionId)
{
    std::lock_guard lock(stateMutex_);
    if (!tokens_.contains(sessionId))
    {
        return false;
    }

    RemoveParticipantRuntimeStateLocked(sessionId);
    return true;
}

bool DungeonUdpSession::ReplaceParticipant(
    SessionId oldSessionId,
    SessionId newSessionId,
    DungeonUdpToken newToken)
{
    if (oldSessionId == 0 || newSessionId == 0 || newToken == 0 ||
        oldSessionId == newSessionId)
    {
        return false;
    }

    std::lock_guard lock(stateMutex_);
    if (!tokens_.contains(oldSessionId) ||
        tokens_.contains(newSessionId))
    {
        return false;
    }

    for (const auto& [sessionId, token] : tokens_)
    {
        (void)sessionId;
        if (token == newToken)
        {
            return false;
        }
    }

    RemoveParticipantRuntimeStateLocked(oldSessionId);
    tokens_.erase(oldSessionId);
    tokens_.emplace(newSessionId, newToken);
    return true;
}

bool DungeonUdpSession::RemoveParticipant(SessionId sessionId)
{
    std::lock_guard lock(stateMutex_);
    if (!tokens_.contains(sessionId))
    {
        return false;
    }

    RemoveParticipantRuntimeStateLocked(sessionId);
    tokens_.erase(sessionId);
    return true;
}

bool DungeonUdpSession::AllParticipantsAuthenticated() const
{
    std::lock_guard lock(stateMutex_);
    return endpoints_.size() == tokens_.size();
}

void DungeonUdpSession::RefreshAllActivity()
{
    std::lock_guard lock(stateMutex_);
    const auto now = std::chrono::steady_clock::now();

    for (const auto& entry : endpoints_)
    {
        lastActivity_[entry.first] = now;
    }
}

std::vector<SessionId> DungeonUdpSession::RemoveInactiveEndpoints(
    std::chrono::milliseconds idleTimeout)
{
    std::vector<SessionId> removedSessions;
    if (idleTimeout <= std::chrono::milliseconds::zero())
    {
        return removedSessions;
    }

    std::lock_guard lock(stateMutex_);
    const auto now = std::chrono::steady_clock::now();

    for (auto activityIt = lastActivity_.begin();
         activityIt != lastActivity_.end();)
    {
        if (now - activityIt->second < idleTimeout)
        {
            ++activityIt;
            continue;
        }

        const SessionId sessionId = activityIt->first;
        removedSessions.push_back(sessionId);
        ++activityIt;
        RemoveParticipantRuntimeStateLocked(sessionId);
    }

    return removedSessions;
}

bool DungeonUdpSession::SendSnapshot(std::vector<std::uint8_t> bytes)
{
    if (bytes.empty())
    {
        return false;
    }

    if (bytes.size() > MAX_DUNGEON_DATAGRAM_SIZE)
    {
        std::lock_guard lock(snapshotMutex_);
        ++snapshotStats_.oversizedSnapshotCount;
        return false;
    }

    if (stopped_.load())
    {
        return false;
    }

    const auto sharedBytes =
        std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    bool schedulePump = false;

    {
        std::lock_guard lock(snapshotMutex_);

        if (stopped_.load())
        {
            return false;
        }

        ++snapshotStats_.acceptedSnapshotCount;
        if (pendingSnapshot_ != nullptr)
        {
            ++snapshotStats_.replacedSnapshotCount;
        }

        pendingSnapshot_ = sharedBytes;
        snapshotStats_.snapshotPending = true;

        if (!snapshotPumpScheduled_)
        {
            snapshotPumpScheduled_ = true;
            schedulePump = true;
        }
    }

    if (!schedulePump)
    {
        return true;
    }

    const auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            self->PumpSnapshotOnStrand();
        });

    return true;
}

bool DungeonUdpSession::TryPopMovement(
    AuthenticatedPlayerMovement& output)
{
    std::lock_guard lock(stateMutex_);

    if (pendingMovements_.empty())
    {
        return false;
    }

    output = std::move(pendingMovements_.front());
    pendingMovements_.pop_front();
    return true;
}

std::size_t DungeonUdpSession::PendingMovementCount() const
{
    std::lock_guard lock(stateMutex_);
    return pendingMovements_.size();
}

bool DungeonUdpSession::TryPopAttack(AuthenticatedPlayerAttack& output)
{
    std::lock_guard lock(stateMutex_);

    if (pendingAttacks_.empty())
    {
        return false;
    }

    output = std::move(pendingAttacks_.front());
    pendingAttacks_.pop_front();
    return true;
}

std::size_t DungeonUdpSession::PendingAttackCount() const
{
    std::lock_guard lock(stateMutex_);
    return pendingAttacks_.size();
}

DungeonUdpSessionStats DungeonUdpSession::Stats() const
{
    std::lock_guard lock(snapshotMutex_);
    return snapshotStats_;
}

void DungeonUdpSession::RemoveParticipantRuntimeStateLocked(
    SessionId sessionId)
{
    endpoints_.erase(sessionId);
    lastActivity_.erase(sessionId);
    lastMovementSequences_.erase(sessionId);
    lastAttackSequences_.erase(sessionId);
    pendingMovements_.erase(
        std::remove_if(
            pendingMovements_.begin(),
            pendingMovements_.end(),
            [sessionId](const AuthenticatedPlayerMovement& movement)
            {
                return movement.sessionId == sessionId;
            }),
        pendingMovements_.end());
    pendingAttacks_.erase(
        std::remove_if(
            pendingAttacks_.begin(),
            pendingAttacks_.end(),
            [sessionId](const AuthenticatedPlayerAttack& attack)
            {
                return attack.sessionId == sessionId;
            }),
        pendingAttacks_.end());
}

void DungeonUdpSession::StartReceive()
{
    if (stopped_.load())
    {
        return;
    }

    const auto self = shared_from_this();
    socket_.async_receive_from(
        boost::asio::buffer(receiveBuffer_),
        senderEndpoint_,
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t receivedSize)
            {
                self->HandleReceive(error, receivedSize);
            }));
}

void DungeonUdpSession::HandleReceive(
    const boost::system::error_code& error,
    std::size_t receivedSize)
{
    if (stopped_.load() || error == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (!error)
    {
        const std::vector<std::uint8_t> bytes(
            receiveBuffer_.begin(),
            receiveBuffer_.begin() + receivedSize);

        UdpHelloMessage hello;
        if (DecodeUdpHello(bytes, hello))
        {
            if (hello.dungeonId == dungeonId_)
            {
                HandleHello(hello);
            }
            else
            {
                SendHelloAck(
                    hello.dungeonId,
                    UdpHelloResult::InvalidDungeon);
            }
        }
        else
        {
            PlayerMovementMessage movement;
            if (DecodePlayerMovement(bytes, movement) &&
                movement.dungeonId == dungeonId_)
            {
                HandlePlayerMovement(movement);
            }
            else
            {
                PlayerAttackMessage attack;
                if (DecodePlayerAttack(bytes, attack) &&
                    attack.dungeonId == dungeonId_)
                {
                    HandlePlayerAttack(attack);
                }
                else
                {
                    UdpHeartbeatMessage heartbeat;
                    if (DecodeUdpHeartbeat(bytes, heartbeat) &&
                        heartbeat.dungeonId == dungeonId_)
                    {
                        HandleHeartbeat(heartbeat);
                    }
                }
            }
        }
    }

    StartReceive();
}

void DungeonUdpSession::HandleHello(const UdpHelloMessage& hello)
{
    UdpHelloResult result = UdpHelloResult::AuthenticationFailed;

    {
        std::lock_guard lock(stateMutex_);

        const auto tokenIt = tokens_.find(hello.sessionId);
        if (tokenIt != tokens_.end() && tokenIt->second == hello.token)
        {
            const auto endpointIt = endpoints_.find(hello.sessionId);
            if (endpointIt == endpoints_.end())
            {
                endpoints_.emplace(hello.sessionId, senderEndpoint_);
                lastActivity_[hello.sessionId] =
                    std::chrono::steady_clock::now();
                result = UdpHelloResult::Success;
            }
            else if (endpointIt->second == senderEndpoint_)
            {
                lastActivity_[hello.sessionId] =
                    std::chrono::steady_clock::now();
                result = UdpHelloResult::Success;
            }
        }
    }

    SendHelloAck(hello.dungeonId, result);
}

void DungeonUdpSession::SendHelloAck(
    DungeonId requestedDungeonId,
    UdpHelloResult result)
{
    if (stopped_.load())
    {
        return;
    }

    const std::uint32_t serverTick =
        result == UdpHelloResult::Success
            ? serverTick_->load(std::memory_order_relaxed)
            : 0;
    const auto bytes =
        std::make_shared<const std::vector<std::uint8_t>>(
            EncodeUdpHelloAck(
                {requestedDungeonId, result, serverTick}));
    const auto destination =
        std::make_shared<const udp::endpoint>(senderEndpoint_);
    const auto self = shared_from_this();

    socket_.async_send_to(
        boost::asio::buffer(*bytes),
        *destination,
        boost::asio::bind_executor(
            strand_,
            [self, bytes, destination](
                const boost::system::error_code&,
                std::size_t)
            {
            }));
}

void DungeonUdpSession::HandleHeartbeat(
    const UdpHeartbeatMessage& heartbeat)
{
    std::lock_guard lock(stateMutex_);

    const auto endpointIt = endpoints_.find(heartbeat.sessionId);
    if (endpointIt == endpoints_.end() ||
        endpointIt->second != senderEndpoint_)
    {
        return;
    }

    lastActivity_[heartbeat.sessionId] =
        std::chrono::steady_clock::now();
}

void DungeonUdpSession::HandlePlayerMovement(
    const PlayerMovementMessage& movement)
{
    std::lock_guard lock(stateMutex_);

    SessionId sessionId = 0;
    for (const auto& [registeredSessionId, endpoint] : endpoints_)
    {
        if (endpoint == senderEndpoint_)
        {
            sessionId = registeredSessionId;
            break;
        }
    }

    if (sessionId == 0)
    {
        return;
    }

    lastActivity_[sessionId] = std::chrono::steady_clock::now();

    if (pendingMovements_.size() >= MAX_PENDING_DUNGEON_MOVEMENTS)
    {
        return;
    }

    const auto sequenceIt = lastMovementSequences_.find(sessionId);
    if (sequenceIt != lastMovementSequences_.end() &&
        !IsNewerSequence(movement.sequence, sequenceIt->second))
    {
        return;
    }

    lastMovementSequences_[sessionId] = movement.sequence;
    pendingMovements_.push_back({sessionId, movement});
}

void DungeonUdpSession::HandlePlayerAttack(
    const PlayerAttackMessage& attack)
{
    std::lock_guard lock(stateMutex_);

    SessionId sessionId = 0;
    for (const auto& [registeredSessionId, endpoint] : endpoints_)
    {
        if (endpoint == senderEndpoint_)
        {
            sessionId = registeredSessionId;
            break;
        }
    }

    if (sessionId == 0)
    {
        return;
    }

    lastActivity_[sessionId] = std::chrono::steady_clock::now();

    if (pendingAttacks_.size() >= MAX_PENDING_DUNGEON_ATTACKS)
    {
        return;
    }

    const auto sequenceIt = lastAttackSequences_.find(sessionId);
    if (sequenceIt != lastAttackSequences_.end() &&
        !IsNewerSequence(attack.sequence, sequenceIt->second))
    {
        return;
    }

    lastAttackSequences_[sessionId] = attack.sequence;
    pendingAttacks_.push_back({sessionId, attack});
}

void DungeonUdpSession::PumpSnapshotOnStrand()
{
    if (stopped_.load())
    {
        std::lock_guard lock(snapshotMutex_);
        pendingSnapshot_.reset();
        snapshotPumpScheduled_ = false;
        snapshotStats_.snapshotPending = false;
        snapshotStats_.snapshotSendInProgress = false;
        return;
    }

    {
        std::lock_guard lock(snapshotMutex_);
        activeSnapshot_ = std::move(pendingSnapshot_);
        snapshotStats_.snapshotPending = false;

        if (activeSnapshot_ == nullptr)
        {
            snapshotPumpScheduled_ = false;
            snapshotStats_.snapshotSendInProgress = false;
            return;
        }

        snapshotStats_.snapshotSendInProgress = true;
    }

    snapshotDestinations_.clear();
    nextSnapshotDestination_ = 0;

    {
        std::lock_guard lock(stateMutex_);
        snapshotDestinations_.reserve(endpoints_.size());

        for (const auto& entry : endpoints_)
        {
            snapshotDestinations_.push_back(entry.second);
        }
    }

    SendSnapshotToNextEndpoint();
}

void DungeonUdpSession::SendSnapshotToNextEndpoint()
{
    if (stopped_.load() ||
        nextSnapshotDestination_ >= snapshotDestinations_.size())
    {
        FinishSnapshotOnStrand();
        return;
    }

    const auto self = shared_from_this();
    socket_.async_send_to(
        boost::asio::buffer(*activeSnapshot_),
        snapshotDestinations_[nextSnapshotDestination_],
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t)
            {
                {
                    std::lock_guard lock(self->snapshotMutex_);
                    if (error)
                    {
                        ++self->snapshotStats_.snapshotSendErrorCount;
                    }
                    else
                    {
                        ++self->snapshotStats_.sentSnapshotDatagramCount;
                    }
                }

                ++self->nextSnapshotDestination_;
                self->SendSnapshotToNextEndpoint();
            }));
}

void DungeonUdpSession::FinishSnapshotOnStrand()
{
    activeSnapshot_.reset();
    snapshotDestinations_.clear();
    nextSnapshotDestination_ = 0;

    {
        std::lock_guard lock(snapshotMutex_);
        snapshotStats_.snapshotSendInProgress = false;
    }

    PumpSnapshotOnStrand();
}
} // namespace dnf
