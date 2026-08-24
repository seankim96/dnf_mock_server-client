#pragma once

#include <stdexcept>

namespace dnf
{
class DatabaseError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};
} // namespace dnf
