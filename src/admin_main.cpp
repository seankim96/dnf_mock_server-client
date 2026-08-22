#include "AccountProvisioningService.h"
#include "ScryptPasswordHasher.h"
#include "SqliteAccountRepository.h"
#include "SqliteCharacterProvisioner.h"
#include "SqliteDatabase.h"

#include <openssl/crypto.h>

#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
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
           << "  " << executableName
           << " create-character <database-path>"
           << " <account-id> <player-name>\n"
           << "\nThe password is read as one line from standard input.\n";
}

dnf::AccountId ParseAccountId(const std::string& text)
{
    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            throw std::invalid_argument(
                "Account ID must be a positive number");
        }
    }

    std::size_t parsedLength = 0;
    unsigned long long value = 0;

    try
    {
        value = std::stoull(text, &parsedLength);
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument(
            "Account ID must be a positive number");
    }

    if (text.empty() ||
        parsedLength != text.size() ||
        value == 0 ||
        value > std::numeric_limits<dnf::AccountId>::max())
    {
        throw std::invalid_argument(
            "Account ID must be a positive number");
    }

    return static_cast<dnf::AccountId>(value);
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

int PrintCharacterResult(
    const dnf::CharacterProvisioningResult& result)
{
    switch (result.status)
    {
    case dnf::CharacterProvisioningStatus::Success:
        std::cout << "Character created"
                  << " playerId=" << result.playerId << '\n';
        return 0;

    case dnf::CharacterProvisioningStatus::InvalidInput:
        std::cerr << "Invalid account ID or player name\n";
        return 1;

    case dnf::CharacterProvisioningStatus::AccountNotFound:
        std::cerr << "Account was not found\n";
        return 1;

    case dnf::CharacterProvisioningStatus::PlayerNameAlreadyExists:
        std::cerr << "Player name already exists\n";
        return 1;
    }

    std::cerr << "Unknown character creation result\n";
    return 1;
}

int RunCreateAccount(
    const std::string& databasePath,
    const std::string& loginId)
{
    std::string password;
    if (!std::getline(std::cin, password))
    {
        std::cerr << "Failed to read password from standard input\n";
        return 1;
    }
    PasswordCleanup passwordCleanup(password);

    dnf::SqliteDatabase database(databasePath);
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::ScryptPasswordHasher passwordHasher;
    dnf::AccountProvisioningService provisioningService(
        accountRepository,
        passwordHasher);

    return PrintAccountResult(
        provisioningService.CreateAccount(loginId, password));
}

int RunCreateCharacter(
    const std::string& databasePath,
    const std::string& accountIdText,
    const std::string& playerName)
{
    const dnf::AccountId accountId = ParseAccountId(accountIdText);
    dnf::SqliteDatabase database(databasePath);
    dnf::SqliteCharacterProvisioner provisioner(database);

    return PrintCharacterResult(
        provisioner.CreateOwnedPlayer(accountId, playerName));
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

    try
    {
        if (argc == 4 && std::string(argv[1]) == "create-account")
        {
            return RunCreateAccount(argv[2], argv[3]);
        }

        if (argc == 5 && std::string(argv[1]) == "create-character")
        {
            return RunCreateCharacter(argv[2], argv[3], argv[4]);
        }

        PrintUsage(std::cerr, argv[0]);
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Admin tool error: " << error.what() << '\n';
        return 1;
    }
}
