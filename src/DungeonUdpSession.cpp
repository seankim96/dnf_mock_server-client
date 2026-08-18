#include "DungeonUdpSession.h"

#include "DungeonProtocol.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>

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

    if (!endpoints_.contains(hello.sessionId))
    {
        endpoints_.emplace(hello.sessionId, senderEndpoint_);
    }
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

    if (sessionId == 0 ||
        pendingInputs_.size() >= MAX_PENDING_DUNGEON_INPUTS)
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
