#include "ServerApplication.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[])
{
    try
    {
        unsigned long portValue = 7777;

        if (argc >= 2)
        {
            portValue = std::stoul(argv[1]);
        }

        if (portValue == 0 || portValue > 65535)
        {
            throw std::runtime_error("Port must be between 1 and 65535");
        }

        const auto port = static_cast<std::uint16_t>(portValue);

        dnf::ServerApplication application(port);
        application.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Server error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
