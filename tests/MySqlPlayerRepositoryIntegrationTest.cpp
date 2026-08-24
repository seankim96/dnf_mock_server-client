#include "MySqlConnectionPool.h"
#include "MySqlPlayerRepository.h"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
std::optional<std::string> Environment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }

    return std::string(value);
}

std::uint16_t ParsePort(const std::optional<std::string>& text)
{
    if (!text.has_value())
    {
        return 3306;
    }

    unsigned int port = 0;
    const auto [end, error] = std::from_chars(
        text->data(),
        text->data() + text->size(),
        port);
    if (error != std::errc{} || end != text->data() + text->size() ||
        port == 0 || port > 65535)
    {
        throw std::invalid_argument("Invalid DNF_TEST_MYSQL_PORT");
    }

    return static_cast<std::uint16_t>(port);
}

dnf::MySqlTlsMode ParseTlsMode(
    const std::optional<std::string>& text)
{
    if (!text.has_value() || *text == "required")
    {
        return dnf::MySqlTlsMode::Required;
    }

    if (*text == "preferred")
    {
        return dnf::MySqlTlsMode::Preferred;
    }

    if (*text == "disabled")
    {
        return dnf::MySqlTlsMode::Disabled;
    }

    throw std::invalid_argument("Invalid DNF_TEST_MYSQL_TLS");
}

std::string UniquePlayerName()
{
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream output;
    output << "Mysql" << std::hex << std::setw(11) << std::setfill('0')
           << (ticks & 0x7ffffffffffULL);
    return output.str();
}

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool HasSamePersistedState(
    const dnf::PlayerProfile& left,
    const dnf::PlayerProfile& right)
{
    if (left.playerId != right.playerId || left.name != right.name ||
        left.level != right.level ||
        left.skillPoints != right.skillPoints ||
        left.skills.size() != right.skills.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.skills.size(); ++index)
    {
        if (left.skills[index].skillId != right.skills[index].skillId ||
            left.skills[index].level != right.skills[index].level)
        {
            return false;
        }
    }

    return true;
}
} // namespace

int main()
{
    const std::optional<std::string> username =
        Environment("DNF_TEST_MYSQL_USER");
    const std::optional<std::string> database =
        Environment("DNF_TEST_MYSQL_DATABASE");
    if (!username.has_value() || username->empty() ||
        !database.has_value() || database->empty())
    {
        std::cout << "MySQL integration environment is not configured; skipped.\n";
        return 77;
    }

    try
    {
        dnf::MySqlConnectionPoolOptions options;
        options.host = Environment("DNF_TEST_MYSQL_HOST")
            .value_or("127.0.0.1");
        options.port = ParsePort(Environment("DNF_TEST_MYSQL_PORT"));
        options.username = *username;
        options.password = Environment("DNF_TEST_MYSQL_PASSWORD")
            .value_or(std::string{});
        options.database = *database;
        options.initialSize = 1;
        options.maxSize = 2;
        options.tlsMode = ParseTlsMode(
            Environment("DNF_TEST_MYSQL_TLS"));

        dnf::MySqlConnectionPool pool(std::move(options));
        dnf::MySqlPlayerRepository repository(pool);
        const std::string playerName = UniquePlayerName();

        std::optional<dnf::PlayerProfile> created =
            repository.CreatePlayer(playerName);
        Check(created.has_value(), "Player creation failed");
        Check(!repository.CreatePlayer(playerName).has_value(),
            "Duplicate player name was accepted");

        std::optional<dnf::PlayerProfile> foundById =
            repository.FindPlayer(created->playerId);
        std::optional<dnf::PlayerProfile> foundByName =
            repository.FindPlayerByName(playerName);
        Check(foundById.has_value() && foundByName.has_value(),
            "Created player could not be loaded");
        Check(foundById->playerId == foundByName->playerId,
            "Player queries returned different IDs");

        foundById->level = 7;
        foundById->skillPoints = 3;
        foundById->skills = {{1001, 2}, {2001, 1}};
        Check(repository.SavePlayer(*foundById), "Player save failed");

        std::optional<dnf::PlayerProfile> saved =
            repository.FindPlayer(created->playerId);
        Check(saved.has_value() && saved->level == 7 &&
            saved->skillPoints == 3 && saved->skills.size() == 2,
            "Saved player data did not round-trip");
        Check(repository.SavePlayer(*saved),
            "Idempotent player save failed");
        std::optional<dnf::PlayerProfile> savedAgain =
            repository.FindPlayer(created->playerId);
        Check(savedAgain.has_value() &&
            HasSamePersistedState(*saved, *savedAgain),
            "Repeated player save changed persisted state");

        dnf::PlayerProfile firstConcurrent = *savedAgain;
        firstConcurrent.level = 11;
        firstConcurrent.skillPoints = 1;
        firstConcurrent.skills = {{1001, 1}};

        dnf::PlayerProfile secondConcurrent = *savedAgain;
        secondConcurrent.level = 12;
        secondConcurrent.skillPoints = 2;
        secondConcurrent.skills = {{2001, 2}};

        std::future<bool> firstSave = std::async(
            std::launch::async,
            [&repository, firstConcurrent]
            {
                return repository.SavePlayer(firstConcurrent);
            });
        std::future<bool> secondSave = std::async(
            std::launch::async,
            [&repository, secondConcurrent]
            {
                return repository.SavePlayer(secondConcurrent);
            });
        Check(firstSave.get() && secondSave.get(),
            "Concurrent player save failed");

        std::optional<dnf::PlayerProfile> concurrentResult =
            repository.FindPlayer(created->playerId);
        Check(concurrentResult.has_value() &&
            (HasSamePersistedState(
                 *concurrentResult,
                 firstConcurrent) ||
             HasSamePersistedState(
                 *concurrentResult,
                 secondConcurrent)),
            "Concurrent saves produced a partially mixed player state");
        Check(!repository.FindPlayer(0).has_value(),
            "Zero player ID was accepted");
        Check(!repository.CreatePlayer("").has_value(),
            "Invalid player name was accepted");
    }
    catch (const std::exception& error)
    {
        std::cerr << "MySQL integration test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
