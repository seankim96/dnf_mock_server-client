#include "AuthTlsSession.h"

#include "AccountAuthenticationService.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <utility>

namespace dnf
{
AuthTlsSession::AuthTlsSession(
    boost::asio::ip::tcp::socket socket,
    boost::asio::ssl::context& tlsContext,
    AccountAuthenticationService& authenticationService,
    CharacterListService& characterListService,
    CharacterSelectionService& characterSelectionService,
    GameServerAddress gameServerAddress)
    : stream_(std::move(socket), tlsContext),
      strand_(boost::asio::make_strand(stream_.get_executor())),
      dispatcher_(
          authenticationService,
          characterListService,
          characterSelectionService,
          std::move(gameServerAddress))
{
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
    const auto self = shared_from_this();
    stream_.async_handshake(
        boost::asio::ssl::stream_base::server,
        boost::asio::bind_executor(
            strand_,
            [self](const boost::system::error_code& error)
            {
                if (error)
                {
                    std::cerr << "Auth TLS handshake error: "
                              << error.message() << '\n';
                    self->Close();
                    return;
                }

                self->StartRead();
            }));
}

void AuthTlsSession::StartRead()
{
    const auto self = shared_from_this();
    stream_.async_read_some(
        boost::asio::buffer(receivedBytes_),
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t receivedSize)
            {
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
        const auto self = shared_from_this();
        dispatcher_.DispatchAsync(
            std::move(request),
            [self](std::vector<std::uint8_t> response) mutable
            {
                boost::asio::dispatch(
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

    responseBytes_ = std::move(response);
    StartWrite();
}

void AuthTlsSession::StartWrite()
{
    const auto self = shared_from_this();
    boost::asio::async_write(
        stream_,
        boost::asio::buffer(responseBytes_),
        boost::asio::bind_executor(
            strand_,
            [self](
                const boost::system::error_code& error,
                std::size_t /*sentSize*/)
            {
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

                self->responseBytes_.clear();
                self->requestInProgress_ = false;
                self->DispatchNextPacket();
            }));
}

void AuthTlsSession::Close()
{
    if (closed_)
    {
        return;
    }

    closed_ = true;

    boost::system::error_code ignoredError;
    stream_.next_layer().shutdown(
        boost::asio::ip::tcp::socket::shutdown_both,
        ignoredError);
    stream_.next_layer().close(ignoredError);
}
} // namespace dnf
