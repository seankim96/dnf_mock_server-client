#include "LoginProtocol.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
void TestSuccessfulLoginResponse()
{
    const auto payload =
        dnf::EncodeLoginResponsePayload(dnf::LoginSuccess, 0x0102030405060708);

    assert(payload.size() == 9);
    assert(payload[0] == 0);
    assert(payload[1] == 0x01);
    assert(payload[8] == 0x08);

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
        dnf::DecodeLoginResponsePayload({0});
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
    TestSuccessfulLoginResponse();
    TestFailedLoginResponse();
    TestInvalidLoginResponse();

    std::cout << "All login protocol tests passed.\n";
    return 0;
}
