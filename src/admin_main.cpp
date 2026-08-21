#include "AccountProvisioningService.h"
#include "ScryptPasswordHasher.h"
#include "SqliteAccountRepository.h"
#include "SqliteDatabase.h"

#include <openssl/crypto.h>

#include <exception>
#include <iostream>
#include <string>

namespace
{
class PasswordCleanup
{
public:
    explicit PasswordCleanup(std::string& password)
        : password_(password)
    {
    }

    ~PasswordCleanup()
    {
        OPENSSL_cleanse(password_.data(), password_.size());
    }

private:
    std::string& password_;
};

void PrintUsage(std::ostream& output, const char* executableName)
{
    output << "Usage:\n"
           << "  " << executableName
           << " create-account <database-path> <login-id>\n"
           << "\nThe password is read as one line from standard input.\n";
}

int PrintAccountResult(
    const dnf::AccountProvisioningResult& result)
{
    switch (result.status)
    {
    case dnf::AccountProvisioningStatus::Success:
        std::cout << "Account created"
                  << " accountId=" << result.accountId << '\n';
        return 0;

    case dnf::AccountProvisioningStatus::InvalidLoginId:
        std::cerr << "Invalid login ID\n";
        return 1;

    case dnf::AccountProvisioningStatus::InvalidPassword:
        std::cerr << "Invalid password\n";
        return 1;

    case dnf::AccountProvisioningStatus::PasswordHashFailed:
        std::cerr << "Failed to hash password\n";
        return 1;

    case dnf::AccountProvisioningStatus::LoginIdAlreadyExists:
        std::cerr << "Login ID already exists\n";
        return 1;
    }

    std::cerr << "Unknown account creation result\n";
    return 1;
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

    if (argc != 4 || std::string(argv[1]) != "create-account")
    {
        PrintUsage(std::cerr, argv[0]);
        return 1;
    }

    std::string password;
    if (!std::getline(std::cin, password))
    {
        std::cerr << "Failed to read password from standard input\n";
        return 1;
    }
    PasswordCleanup passwordCleanup(password);

    try
    {
        dnf::SqliteDatabase database(argv[2]);
        dnf::SqliteAccountRepository accountRepository(database);
        dnf::ScryptPasswordHasher passwordHasher;
        dnf::AccountProvisioningService provisioningService(
            accountRepository,
            passwordHasher);

        return PrintAccountResult(
            provisioningService.CreateAccount(
                argv[3],
                password));
    }
    catch (const std::exception& error)
    {
        std::cerr << "Admin tool error: " << error.what() << '\n';
        return 1;
    }
}
