#include "AuthServerApplication.h"
#include "TestTlsCertificate.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/system/error_code.hpp>

#include <cassert>
#include <exception>
#include <iostream>
#include <thread>

using boost::asio::ip::tcp;

namespace
{
void TestApplicationRunsTlsListener()
{
    dnf::test::TestTlsCertificateFiles certificateFiles;
    dnf::AuthServerApplication application(
        0,
        ":memory:",
        certificateFiles.CertificatePath(),
        certificateFiles.PrivateKeyPath(),
        {"127.0.0.1", 7777});

    application.Start();
    const std::uint16_t port = application.Port();
    assert(port != 0);

    std::exception_ptr serverException;
    std::thread serverThread(
        [&]
        {
            try
            {
                application.Run();
            }
            catch (...)
            {
                serverException = std::current_exception();
            }
        });

    try
    {
        boost::asio::io_context clientIoContext;
        boost::asio::ssl::context clientTlsContext(
            boost::asio::ssl::context::tls_client);
        clientTlsContext.set_verify_mode(
            boost::asio::ssl::verify_none);
        boost::asio::ssl::stream<tcp::socket> client(
            clientIoContext,
            clientTlsContext);

        client.next_layer().connect(tcp::endpoint(
            boost::asio::ip::address_v4::loopback(),
            port));
        client.handshake(boost::asio::ssl::stream_base::client);

        boost::system::error_code ignoredError;
        client.shutdown(ignoredError);
        client.next_layer().close(ignoredError);
    }
    catch (...)
    {
        const std::exception_ptr clientException =
            std::current_exception();
        application.Stop();
        serverThread.join();

        if (serverException)
        {
            std::rethrow_exception(serverException);
        }

        std::rethrow_exception(clientException);
    }

    application.Stop();
    serverThread.join();

    if (serverException)
    {
        std::rethrow_exception(serverException);
    }
}
} // namespace

int main()
{
    TestApplicationRunsTlsListener();

    std::cout << "All auth server application tests passed.\n";
    return 0;
}
