#include "ScryptPasswordHasher.h"

#include "Account.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace dnf
{
namespace
{
constexpr std::uint64_t SCRYPT_N = 1ULL << 17;
constexpr std::uint64_t SCRYPT_R = 8;
constexpr std::uint64_t SCRYPT_P = 1;
constexpr std::uint64_t SCRYPT_MAX_MEMORY = 256ULL * 1024 * 1024;
constexpr std::size_t SALT_BYTE_COUNT = 16;
constexpr std::size_t HASH_BYTE_COUNT = 32;
constexpr char HEX_DIGITS[] = "0123456789abcdef";

struct ScryptHashData
{
    std::uint64_t n = 0;
    std::uint64_t r = 0;
    std::uint64_t p = 0;
    std::array<unsigned char, SALT_BYTE_COUNT> salt{};
    std::array<unsigned char, HASH_BYTE_COUNT> hash{};
};

bool IsValidPassword(const std::string& password)
{
    return !password.empty() &&
           password.size() <= MAX_PASSWORD_LENGTH;
}

template <std::size_t Size>
std::string EncodeHex(const std::array<unsigned char, Size>& bytes)
{
    std::string encoded;
    encoded.reserve(bytes.size() * 2);

    for (const unsigned char byte : bytes)
    {
        encoded.push_back(HEX_DIGITS[byte >> 4]);
        encoded.push_back(HEX_DIGITS[byte & 0x0F]);
    }

    return encoded;
}

int HexValue(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }

    return -1;
}

template <std::size_t Size>
bool DecodeHex(
    std::string_view encoded,
    std::array<unsigned char, Size>& output)
{
    if (encoded.size() != output.size() * 2)
    {
        return false;
    }

    for (std::size_t index = 0; index < output.size(); ++index)
    {
        const int high = HexValue(encoded[index * 2]);
        const int low = HexValue(encoded[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }

        output[index] = static_cast<unsigned char>((high << 4) | low);
    }

    return true;
}

std::vector<std::string_view> Split(std::string_view value)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;

    while (true)
    {
        const std::size_t delimiter = value.find('$', start);
        if (delimiter == std::string_view::npos)
        {
            parts.push_back(value.substr(start));
            return parts;
        }

        parts.push_back(value.substr(start, delimiter - start));
        start = delimiter + 1;
    }
}

bool ParseUint64(std::string_view text, std::uint64_t& output)
{
    if (text.empty())
    {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [parsedEnd, error] =
        std::from_chars(begin, end, output);
    return error == std::errc{} && parsedEnd == end;
}

bool HasSafeScryptParameters(const ScryptHashData& data)
{
    if (data.n < (1ULL << 14) ||
        data.n > (1ULL << 20) ||
        (data.n & (data.n - 1)) != 0 ||
        data.r == 0 || data.r > 32 ||
        data.p == 0 || data.p > 16)
    {
        return false;
    }

    if (data.n >
        std::numeric_limits<std::uint64_t>::max() / 128 / data.r)
    {
        return false;
    }

    return 128 * data.n * data.r <= SCRYPT_MAX_MEMORY;
}

std::optional<ScryptHashData> ParseEncodedHash(
    const std::string& encodedPasswordHash)
{
    if (!IsValidEncodedPasswordHash(encodedPasswordHash))
    {
        return std::nullopt;
    }

    const std::vector<std::string_view> parts =
        Split(encodedPasswordHash);
    if (parts.size() != 6 || parts[0] != "scrypt")
    {
        return std::nullopt;
    }

    ScryptHashData data;
    if (!ParseUint64(parts[1], data.n) ||
        !ParseUint64(parts[2], data.r) ||
        !ParseUint64(parts[3], data.p) ||
        !HasSafeScryptParameters(data) ||
        !DecodeHex(parts[4], data.salt) ||
        !DecodeHex(parts[5], data.hash))
    {
        return std::nullopt;
    }

    return data;
}

bool DeriveScryptHash(
    const std::string& password,
    const ScryptHashData& data,
    std::array<unsigned char, HASH_BYTE_COUNT>& output)
{
    return EVP_PBE_scrypt(
               password.data(),
               password.size(),
               data.salt.data(),
               data.salt.size(),
               data.n,
               data.r,
               data.p,
               SCRYPT_MAX_MEMORY,
               output.data(),
               output.size()) == 1;
}

std::string EncodeHash(const ScryptHashData& data)
{
    return "scrypt$" +
           std::to_string(data.n) + "$" +
           std::to_string(data.r) + "$" +
           std::to_string(data.p) + "$" +
           EncodeHex(data.salt) + "$" +
           EncodeHex(data.hash);
}
} // namespace

std::optional<std::string> ScryptPasswordHasher::Hash(
    const std::string& password)
{
    if (!IsValidPassword(password))
    {
        return std::nullopt;
    }

    ScryptHashData data;
    data.n = SCRYPT_N;
    data.r = SCRYPT_R;
    data.p = SCRYPT_P;

    if (RAND_bytes(
            data.salt.data(),
            static_cast<int>(data.salt.size())) != 1)
    {
        throw std::runtime_error("Failed to generate password salt");
    }

    if (!DeriveScryptHash(password, data, data.hash))
    {
        OPENSSL_cleanse(data.hash.data(), data.hash.size());
        throw std::runtime_error("Failed to hash password with scrypt");
    }

    const std::string encoded = EncodeHash(data);
    OPENSSL_cleanse(data.hash.data(), data.hash.size());
    return encoded;
}

bool ScryptPasswordHasher::Verify(
    const std::string& password,
    const std::string& encodedPasswordHash)
{
    if (!IsValidPassword(password))
    {
        return false;
    }

    const std::optional<ScryptHashData> parsed =
        ParseEncodedHash(encodedPasswordHash);
    if (!parsed.has_value())
    {
        return false;
    }

    std::array<unsigned char, HASH_BYTE_COUNT> derivedHash{};
    if (!DeriveScryptHash(password, parsed.value(), derivedHash))
    {
        OPENSSL_cleanse(derivedHash.data(), derivedHash.size());
        return false;
    }

    const bool matches = CRYPTO_memcmp(
        derivedHash.data(),
        parsed->hash.data(),
        derivedHash.size()) == 0;
    OPENSSL_cleanse(derivedHash.data(), derivedHash.size());
    return matches;
}
} // namespace dnf
