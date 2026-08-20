#include "LoginProtocol.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
void TestLoginRequest()
{
    const auto payload = dnf::EncodeLoginRequestPayload("Mock_Player");
    assert(!payload.empty());
    assert(dnf::DecodeLoginRequestPayload(payload) == "Mock_Player");

    bool threw = false;
    try
    {
        dnf::DecodeLoginRequestPayload({0});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestSuccessfulLoginResponse()
{
    const auto payload =
        dnf::EncodeLoginResponsePayload(dnf::LoginSuccess, 0x0102030405060708);

    assert(!payload.empty());

    const auto response = dnf::DecodeLoginResponsePayload(payload);
    assert(response.result == dnf::LoginSuccess);
    assert(response.sessionId == 0x0102030405060708);
}

void TestFailedLoginResponse()
{
    const auto payload = dnf::EncodeLoginResponsePayload(
        dnf::EmptyPlayerName,
        0);
    const auto response = dnf::DecodeLoginResponsePayload(payload);

    assert(response.result == dnf::EmptyPlayerName);
    assert(response.sessionId == 0);
}

void TestInvalidLoginResponse()
{
    bool threw = false;
    try
    {
        dnf::EncodeLoginResponsePayload(dnf::LoginSuccess, 0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::EncodeLoginResponsePayload(
            static_cast<dnf::LoginResult>(4),
            0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodeLoginResponsePayload(
            dnf::EncodeLoginRequestPayload("Mock"));
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}
} // namespace

int main()
{
    TestLoginRequest();
    TestSuccessfulLoginResponse();
    TestFailedLoginResponse();
    TestInvalidLoginResponse();

    std::cout << "All login protocol tests passed.\n";
    return 0;
}
