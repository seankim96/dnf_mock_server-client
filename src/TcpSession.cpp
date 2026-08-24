#include "TcpSession.h"

#include "ChannelManager.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "PartyManager.h"
#include "PlayerLoginService.h"
#include "SessionManager.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace dnf
{
TcpSession::TcpSession(
    SessionId sessionId,
    boost::asio::ip::tcp::socket socket,
    SessionManager& sessionManager,
    ChannelManager& channelManager,
    PartyManager& partyManager,
    DungeonManager& dungeonManager,
    DungeonUdpManager& dungeonUdpManager,
    PlayerLoginService& playerLoginService,
    NetworkSessionOptions options)
    : sessionId_(sessionId),
      socket_(std::move(socket)),
      strand_(boost::asio::make_strand(socket_.get_executor())),
      sessionManager_(sessionManager),
      options_(std::move(options)),
      authenticationDeadline_(strand_),
      readDeadline_(strand_),
      writeDeadline_(strand_),
      dispatcher_(
          channelManager,
          partyManager,
          dungeonManager,
          dungeonUdpManager,
          playerLoginService,
          sessionId)
{
    if (!options_.IsValid())
    {
        throw std::invalid_argument(
            "TCP session options are invalid");
    }
}

void TcpSession::Start()
{
    auto self = shared_from_this();

    boost::asio::dispatch(
        strand_,
        [self]
        {
            std::cout << "Session connected id="
                      << self->sessionId_ << '\n';
            self->StartAuthenticationTimeout();
            self->StartRead();
        });
}

SessionId TcpSession::Id() const
{
    return sessionId_;
}

void TcpSession::StartRead()
{
    if (closed_)
    {
        return;
    }

    StartReadTimeout();
    auto self = shared_from_this();

    socket_.async_read_some(
        boost::asio::buffer(receivedBytes_),
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t receivedSize)
            {
                self->readDeadline_.Cancel();

                if (error)
                {
                    if (error != boost::asio::error::eof &&
                        error != boost::asio::error::operation_aborted)
                    {
                        std::cerr << "Receive error sessionId="
                                  << self->sessionId_
                                  << " message=" << error.message() << '\n';
                    }

                    self->Close();
                    return;
                }

                self->HandleRead(receivedSize);
            }));
}

void TcpSession::HandleRead(std::size_t receivedSize)
{
    receiveBuffer_.Append(std::span(receivedBytes_.data(), receivedSize));
    DispatchNextPacket();
}

void TcpSession::DispatchNextPacket()
{
    if (closed_ || requestInProgress_)
    {
        return;
    }

    try
    {
        Packet packet;

        if (!receiveBuffer_.TryPop(packet))
        {
            StartRead();
            return;
        }

        std::cout << "Packet received"
                  << " sessionId=" << sessionId_
                  << " type=" << packet.header.type
                  << " requestId=" << packet.header.requestId
                  << '\n';

        requestInProgress_ = true;
        const auto weakSelf = weak_from_this();

        dispatcher_.DispatchAsync(
            std::move(packet),
            [weakSelf](std::vector<std::uint8_t> response) mutable
            {
                const auto self = weakSelf.lock();
                if (!self)
                {
                    return;
                }

                boost::asio::post(
                    self->strand_,
                    [self, response = std::move(response)]() mutable
                    {
                        self->HandleResponse(std::move(response));
                    });
            });
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Packet error sessionId=" << sessionId_
                  << " message=" << exception.what() << '\n';
        requestInProgress_ = false;
        Close();
    }
}

void TcpSession::HandleResponse(std::vector<std::uint8_t> response)
{
    if (closed_)
    {
        return;
    }

    requestInProgress_ = false;

    if (dispatcher_.AuthSnapshot().has_value())
    {
        authenticationDeadline_.Cancel();
    }

    if (!QueueWrite(std::move(response)))
    {
        return;
    }

    DispatchNextPacket();
}

bool TcpSession::QueueWrite(std::vector<std::uint8_t> data)
{
    const bool messageLimitReached =
        writeQueue_.size() >= options_.maxPendingWriteMessages;
    const bool byteLimitReached =
        data.size() > options_.maxPendingWriteBytes ||
        pendingWriteBytes_ >
            options_.maxPendingWriteBytes - data.size();

    if (messageLimitReached || byteLimitReached)
    {
        std::cerr << "TCP pending write limit exceeded sessionId="
                  << sessionId_ << '\n';
        Close();
        return false;
    }

    const bool writeInProgress = !writeQueue_.empty();
    pendingWriteBytes_ += data.size();
    writeQueue_.push_back(std::move(data));

    if (!writeInProgress)
    {
        StartWrite();
    }

    return true;
}

void TcpSession::StartWrite()
{
    StartWriteTimeout();
    auto self = shared_from_this();

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(writeQueue_.front()),
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t /*sentSize*/)
            {
                self->writeDeadline_.Cancel();

                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                    {
                        std::cerr << "Send error sessionId="
                                  << self->sessionId_
                                  << " message=" << error.message() << '\n';
                    }

                    self->Close();
                    return;
                }

                self->pendingWriteBytes_ -=
                    self->writeQueue_.front().size();
                self->writeQueue_.pop_front();

                if (!self->writeQueue_.empty())
                {
                    self->StartWrite();
                }
            }));
}

void TcpSession::StartAuthenticationTimeout()
{
    const auto self = shared_from_this();
    authenticationDeadline_.Start(
        options_.authenticationTimeout,
        [self]
        {
            std::cerr << "Authentication timeout sessionId="
                      << self->sessionId_ << '\n';
            self->Close();
        });
}

void TcpSession::StartReadTimeout()
{
    const auto self = shared_from_this();
    readDeadline_.Start(
        options_.readTimeout,
        [self]
        {
            std::cerr << "Read timeout sessionId="
                      << self->sessionId_ << '\n';
            self->Close();
        });
}

void TcpSession::StartWriteTimeout()
{
    const auto self = shared_from_this();
    writeDeadline_.Start(
        options_.writeTimeout,
        [self]
        {
            std::cerr << "Write timeout sessionId="
                      << self->sessionId_ << '\n';
            self->Close();
        });
}

void TcpSession::Close()
{
    if (closed_)
    {
        return;
    }

    closed_ = true;
    authenticationDeadline_.Cancel();
    readDeadline_.Cancel();
    writeDeadline_.Cancel();

    boost::system::error_code ignoredError;
    socket_.shutdown(
        boost::asio::ip::tcp::socket::shutdown_both,
        ignoredError);
    socket_.close(ignoredError);

    sessionManager_.RemoveSession(sessionId_);
}
} // namespace dnf
