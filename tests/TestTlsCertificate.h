#pragma once

#include <boost/asio/ssl/context.hpp>

#include <string>

namespace dnf::test
{
void ConfigureTestTlsContext(
    boost::asio::ssl::context& tlsContext);

class TestTlsCertificateFiles
{
public:
    TestTlsCertificateFiles();
    ~TestTlsCertificateFiles();

    TestTlsCertificateFiles(const TestTlsCertificateFiles&) = delete;
    TestTlsCertificateFiles& operator=(
        const TestTlsCertificateFiles&) = delete;

    const std::string& CertificatePath() const;
    const std::string& PrivateKeyPath() const;

private:
    std::string directoryPath_;
    std::string certificatePath_;
    std::string privateKeyPath_;
};
} // namespace dnf::test
