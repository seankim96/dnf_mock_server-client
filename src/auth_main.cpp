#include "AuthServerApplication.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void PrintUsage(std::ostream& output, const char* executableName)
{
    output
        << "Usage: " << executableName
        << " <auth-port>"
        << " <database-path>"
        << " <certificate-chain-path>"
        << " <private-key-path>"
        << " <game-server-host>"
        << " <game-server-port>\n";
}

std::uint16_t ParsePort(
    const std::string& text,
    const std::string& argumentName)
{
    std::size_t parsedLength = 0;
    unsigned long value = 0;

    try
    {
        value = std::stoul(text, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument(
            argumentName + " must be a number between 1 and 65535");
    }

    if (parsedLength != text.size() || value == 0 || value > 65535)
    {
        throw std::invalid_argument(
            argumentName + " must be a number between 1 and 65535");
    }

    return static_cast<std::uint16_t>(value);
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc == 2 &&
        (std::string(argv[1]) == "--help" ||
         std::string(argv[1]) == "-h"))
    {
        PrintUsage(std::cout, argv[0]);
        return 0;
    }

    if (argc != 7)
    {
        PrintUsage(std::cerr, argv[0]);
        return 1;
    }

    try
    {
        const std::uint16_t authPort =
            ParsePort(argv[1], "Authentication port");
        const std::uint16_t gameServerPort =
            ParsePort(argv[6], "Game server port");

        dnf::AuthServerApplication application(
            authPort,
            argv[2],
            argv[3],
            argv[4],
            {argv[5], gameServerPort});
        application.Start();
        application.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Authentication server error: "
                  << error.what() << '\n';
        return 1;
    }

    return 0;
}
