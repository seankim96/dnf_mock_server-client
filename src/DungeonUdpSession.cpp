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

DungeonUdpSession::DungeonUdpSession(
    DungeonId dungeonId,
    udp::socket socket,
    TokenMap tokens)
    : dungeonId_(dungeonId),
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
    const auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            if (self->stopped_)
            {
                return;
            }

            self->stopped_ = true;
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
        endpoints_.erase(sessionId);
        lastSequences_.erase(sessionId);
        lastAttackSequences_.erase(sessionId);
        pendingInputs_.erase(
            std::remove_if(
                pendingInputs_.begin(),
                pendingInputs_.end(),
                [sessionId](const AuthenticatedPlayerInput& input)
                {
                    return input.sessionId == sessionId;
                }),
            pendingInputs_.end());
        pendingAttacks_.erase(
            std::remove_if(
                pendingAttacks_.begin(),
                pendingAttacks_.end(),
                [sessionId](const AuthenticatedPlayerAttack& attack)
                {
                    return attack.sessionId == sessionId;
                }),
            pendingAttacks_.end());

        removedSessions.push_back(sessionId);
        activityIt = lastActivity_.erase(activityIt);
    }

    return removedSessions;
}

bool DungeonUdpSession::SendSnapshot(std::vector<std::uint8_t> bytes)
{
    if (bytes.empty() || bytes.size() > MAX_DUNGEON_DATAGRAM_SIZE)
    {
        return false;
    }

    const auto sharedBytes =
        std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    const auto self = shared_from_this();

    boost::asio::dispatch(
        strand_,
        [self, sharedBytes]
        {
            self->SendSnapshotOnStrand(sharedBytes);
        });

    return true;
}

bool DungeonUdpSession::TryPopInput(AuthenticatedPlayerInput& output)
{
    std::lock_guard lock(stateMutex_);

    if (pendingInputs_.empty())
    {
        return false;
    }

    output = std::move(pendingInputs_.front());
    pendingInputs_.pop_front();
    return true;
}

std::size_t DungeonUdpSession::PendingInputCount() const
{
    std::lock_guard lock(stateMutex_);
    return pendingInputs_.size();
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

void DungeonUdpSession::StartReceive()
{
    if (stopped_)
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
    if (stopped_ || error == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (!error)
    {
        const std::vector<std::uint8_t> bytes(
            receiveBuffer_.begin(),
            receiveBuffer_.begin() + receivedSize);

        UdpHelloMessage hello;
        if (DecodeUdpHello(bytes, hello) &&
            hello.dungeonId == dungeonId_)
        {
            HandleHello(hello);
        }
        else
        {
            PlayerInputMessage input;
            if (DecodePlayerInput(bytes, input) &&
                input.dungeonId == dungeonId_)
            {
                HandlePlayerInput(input);
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
    std::lock_guard lock(stateMutex_);

    const auto tokenIt = tokens_.find(hello.sessionId);
    if (tokenIt == tokens_.end() || tokenIt->second != hello.token)
    {
        return;
    }

    const auto endpointIt = endpoints_.find(hello.sessionId);
    if (endpointIt == endpoints_.end())
    {
        endpoints_.emplace(hello.sessionId, senderEndpoint_);
        lastActivity_[hello.sessionId] = std::chrono::steady_clock::now();
    }
    else if (endpointIt->second == senderEndpoint_)
    {
        lastActivity_[hello.sessionId] = std::chrono::steady_clock::now();
    }
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

void DungeonUdpSession::HandlePlayerInput(
    const PlayerInputMessage& input)
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

    if (pendingInputs_.size() >= MAX_PENDING_DUNGEON_INPUTS)
    {
        return;
    }

    const auto sequenceIt = lastSequences_.find(sessionId);
    if (sequenceIt != lastSequences_.end() &&
        input.sequence <= sequenceIt->second)
    {
        return;
    }

    lastSequences_[sessionId] = input.sequence;
    pendingInputs_.push_back({sessionId, input});
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
        attack.sequence <= sequenceIt->second)
    {
        return;
    }

    lastAttackSequences_[sessionId] = attack.sequence;
    pendingAttacks_.push_back({sessionId, attack});
}

void DungeonUdpSession::SendSnapshotOnStrand(
    std::shared_ptr<const std::vector<std::uint8_t>> bytes)
{
    if (stopped_)
    {
        return;
    }

    std::vector<udp::endpoint> destinations;

    {
        std::lock_guard lock(stateMutex_);
        destinations.reserve(endpoints_.size());

        for (const auto& entry : endpoints_)
        {
            destinations.push_back(entry.second);
        }
    }

    const auto self = shared_from_this();

    for (const udp::endpoint& destination : destinations)
    {
        const auto sharedDestination =
            std::make_shared<const udp::endpoint>(destination);

        socket_.async_send_to(
            boost::asio::buffer(*bytes),
            *sharedDestination,
            boost::asio::bind_executor(
                strand_,
                [self, bytes, sharedDestination](
                    const boost::system::error_code&,
                    std::size_t)
                {
                }));
    }
}
} // namespace dnf
