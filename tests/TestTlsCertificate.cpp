#include "TestTlsCertificate.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace dnf::test
{
namespace
{
using KeyContextPointer = std::unique_ptr<
    EVP_PKEY_CTX,
    decltype(&EVP_PKEY_CTX_free)>;
using KeyPointer = std::unique_ptr<
    EVP_PKEY,
    decltype(&EVP_PKEY_free)>;
using CertificatePointer = std::unique_ptr<
    X509,
    decltype(&X509_free)>;
using BioPointer = std::unique_ptr<BIO, decltype(&BIO_free)>;

struct TestTlsMaterial
{
    KeyPointer key{nullptr, EVP_PKEY_free};
    CertificatePointer certificate{nullptr, X509_free};
};

TestTlsMaterial GenerateTestTlsMaterial()
{
    KeyContextPointer keyContext(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
        EVP_PKEY_CTX_free);
    if (!keyContext ||
        EVP_PKEY_keygen_init(keyContext.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(
            keyContext.get(), 2048) <= 0)
    {
        throw std::runtime_error("Failed to prepare test TLS key");
    }

    EVP_PKEY* generatedKey = nullptr;
    if (EVP_PKEY_keygen(keyContext.get(), &generatedKey) <= 0)
    {
        throw std::runtime_error("Failed to generate test TLS key");
    }

    TestTlsMaterial material;
    material.key.reset(generatedKey);
    material.certificate.reset(X509_new());

    if (!material.certificate ||
        X509_set_version(material.certificate.get(), 2) != 1 ||
        ASN1_INTEGER_set(
            X509_get_serialNumber(material.certificate.get()), 1) != 1 ||
        X509_gmtime_adj(
            X509_get_notBefore(material.certificate.get()), 0) == nullptr ||
        X509_gmtime_adj(
            X509_get_notAfter(material.certificate.get()), 3600) == nullptr ||
        X509_set_pubkey(
            material.certificate.get(), material.key.get()) != 1)
    {
        throw std::runtime_error(
            "Failed to prepare test TLS certificate");
    }

    X509_NAME* subject =
        X509_get_subject_name(material.certificate.get());
    const unsigned char commonName[] = "localhost";
    if (subject == nullptr ||
        X509_NAME_add_entry_by_txt(
            subject,
            "CN",
            MBSTRING_ASC,
            commonName,
            -1,
            -1,
            0) != 1 ||
        X509_set_issuer_name(
            material.certificate.get(), subject) != 1 ||
        X509_sign(
            material.certificate.get(),
            material.key.get(),
            EVP_sha256()) <= 0)
    {
        throw std::runtime_error("Failed to sign test TLS certificate");
    }

    return material;
}

void InstallTestTlsMaterial(
    boost::asio::ssl::context& tlsContext,
    const TestTlsMaterial& material)
{
    SSL_CTX* nativeContext = tlsContext.native_handle();
    if (SSL_CTX_use_certificate(
            nativeContext, material.certificate.get()) != 1 ||
        SSL_CTX_use_PrivateKey(
            nativeContext, material.key.get()) != 1 ||
        SSL_CTX_check_private_key(nativeContext) != 1)
    {
        throw std::runtime_error(
            "Failed to install test TLS certificate");
    }
}

std::string MakeRandomSuffix()
{
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    std::array<unsigned char, 8> randomBytes{};
    if (RAND_bytes(randomBytes.data(), randomBytes.size()) != 1)
    {
        throw std::runtime_error(
            "Failed to create test TLS directory name");
    }

    std::string suffix;
    suffix.reserve(randomBytes.size() * 2);
    for (const unsigned char byte : randomBytes)
    {
        suffix.push_back(HEX_DIGITS[byte >> 4]);
        suffix.push_back(HEX_DIGITS[byte & 0x0F]);
    }
    return suffix;
}

void WriteTestTlsMaterial(
    const TestTlsMaterial& material,
    const std::filesystem::path& certificatePath,
    const std::filesystem::path& privateKeyPath)
{
    BioPointer certificateFile(
        BIO_new_file(certificatePath.string().c_str(), "wb"),
        BIO_free);
    if (!certificateFile ||
        PEM_write_bio_X509(
            certificateFile.get(), material.certificate.get()) != 1)
    {
        throw std::runtime_error(
            "Failed to write test TLS certificate");
    }

    BioPointer privateKeyFile(
        BIO_new_file(privateKeyPath.string().c_str(), "wb"),
        BIO_free);
    if (!privateKeyFile ||
        PEM_write_bio_PrivateKey(
            privateKeyFile.get(),
            material.key.get(),
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr) != 1)
    {
        throw std::runtime_error(
            "Failed to write test TLS private key");
    }
}
} // namespace

void ConfigureTestTlsContext(
    boost::asio::ssl::context& tlsContext)
{
    const TestTlsMaterial material = GenerateTestTlsMaterial();
    InstallTestTlsMaterial(tlsContext, material);
}

TestTlsCertificateFiles::TestTlsCertificateFiles()
{
    std::filesystem::path directory;

    try
    {
        directory = std::filesystem::temp_directory_path() /
            ("dnf-auth-tls-test-" + MakeRandomSuffix());
        if (!std::filesystem::create_directory(directory))
        {
            throw std::runtime_error(
                "Failed to create test TLS directory");
        }

        const std::filesystem::path certificatePath =
            directory / "certificate.pem";
        const std::filesystem::path privateKeyPath =
            directory / "private-key.pem";
        const TestTlsMaterial material = GenerateTestTlsMaterial();
        WriteTestTlsMaterial(
            material, certificatePath, privateKeyPath);

        directoryPath_ = directory.string();
        certificatePath_ = certificatePath.string();
        privateKeyPath_ = privateKeyPath.string();
    }
    catch (...)
    {
        std::error_code ignoredError;
        if (!directory.empty())
        {
            std::filesystem::remove_all(directory, ignoredError);
        }
        throw;
    }
}

TestTlsCertificateFiles::~TestTlsCertificateFiles()
{
    std::error_code ignoredError;
    if (!directoryPath_.empty())
    {
        std::filesystem::remove_all(directoryPath_, ignoredError);
    }
}

const std::string& TestTlsCertificateFiles::CertificatePath() const
{
    return certificatePath_;
}

const std::string& TestTlsCertificateFiles::PrivateKeyPath() const
{
    return privateKeyPath_;
}
} // namespace dnf::test
