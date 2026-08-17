#include "TcpSession.h"

#include "SessionManager.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <utility>

namespace dnf
{
TcpSession::TcpSession(
    SessionId sessionId,
    boost::asio::ip::tcp::socket socket,
    SessionManager& sessionManager)
    : sessionId_(sessionId),
      socket_(std::move(socket)),
      strand_(boost::asio::make_strand(socket_.get_executor())),
      sessionManager_(sessionManager)
{
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
            self->StartRead();
        });
}

SessionId TcpSession::Id() const
{
    return sessionId_;
}

void TcpSession::StartRead()
{
    auto self = shared_from_this();

    socket_.async_read_some(
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
    const std::vector<std::uint8_t> data(
        receivedBytes_.begin(),
        receivedBytes_.begin() + receivedSize);
    receiveBuffer_.Append(data);

    try
    {
        Packet packet;

        while (receiveBuffer_.TryPop(packet))
        {
            std::cout << "Packet received"
                      << " sessionId=" << sessionId_
                      << " type=" << packet.header.type
                      << " requestId=" << packet.header.requestId
                      << '\n';

            QueueWrite(dispatcher_.Dispatch(packet));
        }
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Packet error sessionId=" << sessionId_
                  << " message=" << exception.what() << '\n';
        Close();
        return;
    }

    StartRead();
}

void TcpSession::QueueWrite(std::vector<std::uint8_t> data)
{
    const bool writeInProgress = !writeQueue_.empty();
    writeQueue_.push_back(std::move(data));

    if (!writeInProgress)
    {
        StartWrite();
    }
}

void TcpSession::StartWrite()
{
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

                self->writeQueue_.pop_front();

                if (!self->writeQueue_.empty())
                {
                    self->StartWrite();
                }
            }));
}

void TcpSession::Close()
{
    if (closed_)
    {
        return;
    }

    closed_ = true;

    boost::system::error_code ignoredError;
    socket_.shutdown(
        boost::asio::ip::tcp::socket::shutdown_both,
        ignoredError);
    socket_.close(ignoredError);

    sessionManager_.RemoveSession(sessionId_);
}
} // namespace dnf
