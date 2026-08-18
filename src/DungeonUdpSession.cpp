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
    std::lock_guard lock(authenticationMutex_);

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
    std::lock_guard lock(authenticationMutex_);

    const auto endpointIt = endpoints_.find(sessionId);
    if (endpointIt == endpoints_.end())
    {
        return std::nullopt;
    }

    return endpointIt->second;
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
            std::lock_guard lock(authenticationMutex_);

            const auto tokenIt = tokens_.find(hello.sessionId);
            if (tokenIt != tokens_.end() && tokenIt->second == hello.token)
            {
                const auto endpointIt = endpoints_.find(hello.sessionId);
                if (endpointIt == endpoints_.end())
                {
                    endpoints_.emplace(hello.sessionId, senderEndpoint_);
                }
            }
        }
    }

    StartReceive();
}
} // namespace dnf
