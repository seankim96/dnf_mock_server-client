#include "AuthTlsSession.h"

#include "AccountAuthenticationService.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace dnf
{
AuthTlsSession::AuthTlsSession(
    boost::asio::ip::tcp::socket socket,
    boost::asio::ssl::context& tlsContext,
    AccountAuthenticationService& authenticationService,
    CharacterListService& characterListService,
    CharacterSelectionService& characterSelectionService,
    GameServerAddress gameServerAddress,
    NetworkSessionOptions options)
    : stream_(std::move(socket), tlsContext),
      strand_(boost::asio::make_strand(stream_.get_executor())),
      options_(std::move(options)),
      handshakeDeadline_(strand_),
      authenticationDeadline_(strand_),
      readDeadline_(strand_),
      writeDeadline_(strand_),
      dispatcher_(
          authenticationService,
          characterListService,
          characterSelectionService,
          std::move(gameServerAddress))
{
    if (!options_.IsValid())
    {
        throw std::invalid_argument(
            "Auth TLS session options are invalid");
    }
}

void AuthTlsSession::Start()
{
    const auto self = shared_from_this();
    boost::asio::dispatch(
        strand_,
        [self]
        {
            self->StartHandshake();
        });
}

void AuthTlsSession::StartHandshake()
{
    StartHandshakeTimeout();
    const auto self = shared_from_this();
    stream_.async_handshake(
        boost::asio::ssl::stream_base::server,
        boost::asio::bind_executor(
            strand_,
            [self](const boost::system::error_code& error)
            {
                self->handshakeDeadline_.Cancel();

                if (error)
                {
                    if (error != boost::asio::error::operation_aborted)
                    {
                        std::cerr << "Auth TLS handshake error: "
                                  << error.message() << '\n';
                    }

                    self->Close();
                    return;
                }

                self->StartAuthenticationTimeout();
                self->StartRead();
            }));
}

void AuthTlsSession::StartRead()
{
    if (closed_)
    {
        return;
    }

    StartReadTimeout();
    const auto self = shared_from_this();
    stream_.async_read_some(
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
                        std::cerr << "Auth TLS read error: "
                                  << error.message() << '\n';
                    }

                    self->Close();
                    return;
                }

                self->HandleRead(receivedSize);
            }));
}

void AuthTlsSession::HandleRead(std::size_t receivedSize)
{
    const std::vector<std::uint8_t> data(
        receivedBytes_.begin(),
        receivedBytes_.begin() + receivedSize);
    receiveBuffer_.Append(data);
    DispatchNextPacket();
}

void AuthTlsSession::DispatchNextPacket()
{
    if (closed_ || requestInProgress_)
    {
        return;
    }

    Packet request;

    try
    {
        if (!receiveBuffer_.TryPop(request))
        {
            StartRead();
            return;
        }

        requestInProgress_ = true;
        // 인증 세션은 별도 매니저가 보관하지 않으므로 비동기 서비스가
        // 끝날 때까지 콜백이 세션의 수명을 유지한다.
        const auto self = shared_from_this();
        dispatcher_.DispatchAsync(
            std::move(request),
            [self](std::vector<std::uint8_t> response) mutable
            {
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
        std::cerr << "Auth packet error: "
                  << exception.what() << '\n';
        requestInProgress_ = false;
        Close();
    }
}

void AuthTlsSession::HandleResponse(
    std::vector<std::uint8_t> response)
{
    if (closed_)
    {
        return;
    }

    requestInProgress_ = false;

    if (dispatcher_.AuthenticatedAccount().has_value())
    {
        authenticationDeadline_.Cancel();
    }

    if (!QueueWrite(std::move(response)))
    {
        return;
    }

    DispatchNextPacket();
}

bool AuthTlsSession::QueueWrite(std::vector<std::uint8_t> data)
{
    const bool messageLimitReached =
        writeQueue_.size() >= options_.maxPendingWriteMessages;
    const bool byteLimitReached =
        data.size() > options_.maxPendingWriteBytes ||
        pendingWriteBytes_ >
            options_.maxPendingWriteBytes - data.size();

    if (messageLimitReached || byteLimitReached)
    {
        std::cerr << "Auth TLS pending write limit exceeded\n";
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

void AuthTlsSession::StartWrite()
{
    StartWriteTimeout();
    const auto self = shared_from_this();
    boost::asio::async_write(
        stream_,
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
                    if (error !=
                        boost::asio::error::operation_aborted)
                    {
                        std::cerr << "Auth TLS write error: "
                                  << error.message() << '\n';
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

void AuthTlsSession::StartHandshakeTimeout()
{
    const auto self = shared_from_this();
    handshakeDeadline_.Start(
        options_.handshakeTimeout,
        [self]
        {
            std::cerr << "Auth TLS handshake timeout\n";
            self->Close();
        });
}

void AuthTlsSession::StartAuthenticationTimeout()
{
    const auto self = shared_from_this();
    authenticationDeadline_.Start(
        options_.authenticationTimeout,
        [self]
        {
            std::cerr << "Auth TLS authentication timeout\n";
            self->Close();
        });
}

void AuthTlsSession::StartReadTimeout()
{
    const auto self = shared_from_this();
    readDeadline_.Start(
        options_.readTimeout,
        [self]
        {
            std::cerr << "Auth TLS read timeout\n";
            self->Close();
        });
}

void AuthTlsSession::StartWriteTimeout()
{
    const auto self = shared_from_this();
    writeDeadline_.Start(
        options_.writeTimeout,
        [self]
        {
            std::cerr << "Auth TLS write timeout\n";
            self->Close();
        });
}

void AuthTlsSession::Close()
{
    if (closed_)
    {
        return;
    }

    closed_ = true;
    handshakeDeadline_.Cancel();
    authenticationDeadline_.Cancel();
    readDeadline_.Cancel();
    writeDeadline_.Cancel();

    boost::system::error_code ignoredError;
    stream_.next_layer().shutdown(
        boost::asio::ip::tcp::socket::shutdown_both,
        ignoredError);
    stream_.next_layer().close(ignoredError);
}
} // namespace dnf
