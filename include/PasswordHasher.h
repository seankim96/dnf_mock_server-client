#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace dnf
{
constexpr std::size_t MAX_PASSWORD_LENGTH = 1024;

class PasswordHasher
{
public:
    virtual ~PasswordHasher() = default;

    virtual std::optional<std::string> Hash(
        const std::string& password) = 0;
    virtual bool Verify(
        const std::string& password,
        const std::string& encodedPasswordHash) = 0;
};
} // namespace dnf
