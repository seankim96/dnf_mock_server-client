#include "SqlitePlayerRepository.h"

#include <cassert>
#include <iostream>

namespace
{
void TestCreateAndFindPlayer()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);

    const auto created = repository.CreatePlayer("Player_1");
    assert(created.has_value());
    assert(created->playerId != 0);
    assert(created->name == "Player_1");
    assert(created->level == 1);
    assert(created->skillPoints == 0);
    assert(created->skills.empty());

    assert(!repository.CreatePlayer("Player_1").has_value());
    assert(!repository.CreatePlayer("Invalid Name").has_value());

    const auto byId = repository.FindPlayer(created->playerId);
    const auto byName = repository.FindPlayerByName("Player_1");
    assert(byId.has_value());
    assert(byName.has_value());
    assert(byId->playerId == byName->playerId);
    assert(!repository.FindPlayer(9999).has_value());
    assert(!repository.FindPlayerByName("Missing").has_value());
}

void TestSavePlayerAndSkills()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);

    dnf::PlayerProfile profile =
        repository.CreatePlayer("Player_2").value();
    profile.level = 15;
    profile.skillPoints = 4;
    profile.skills = {{1002, 1}, {1001, 3}};
    assert(repository.SavePlayer(profile));

    const auto loaded = repository.FindPlayer(profile.playerId);
    assert(loaded.has_value());
    assert(loaded->level == 15);
    assert(loaded->skillPoints == 4);
    assert(loaded->skills.size() == 2);
    assert(loaded->skills[0].skillId == 1001);
    assert(loaded->skills[0].level == 3);
    assert(loaded->skills[1].skillId == 1002);
    assert(loaded->skills[1].level == 1);

    profile.skills = {{1002, 2}};
    assert(repository.SavePlayer(profile));

    const auto updated = repository.FindPlayer(profile.playerId);
    assert(updated.has_value());
    assert(updated->skills.size() == 1);
    assert(updated->skills[0].skillId == 1002);
    assert(updated->skills[0].level == 2);
}

void TestInvalidAndMissingProfilesAreRejected()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);

    dnf::PlayerProfile invalid;
    assert(!repository.SavePlayer(invalid));

    dnf::PlayerProfile missing;
    missing.playerId = 9999;
    missing.name = "Missing";
    assert(!repository.SavePlayer(missing));
}

void TestFailedSaveRollsBack()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);

    const dnf::PlayerProfile first =
        repository.CreatePlayer("First").value();
    dnf::PlayerProfile second =
        repository.CreatePlayer("Second").value();
    second.level = 7;
    second.skills = {{1001, 2}};
    assert(repository.SavePlayer(second));

    second.name = first.name;
    bool saveFailed = false;
    try
    {
        repository.SavePlayer(second);
    }
    catch (const dnf::DatabaseError&)
    {
        saveFailed = true;
    }
    assert(saveFailed);

    const auto loaded = repository.FindPlayer(second.playerId);
    assert(loaded.has_value());
    assert(loaded->name == "Second");
    assert(loaded->level == 7);
    assert(loaded->skills.size() == 1);
    assert(loaded->skills[0].skillId == 1001);
    assert(loaded->skills[0].level == 2);
}
} // namespace

int main()
{
    TestCreateAndFindPlayer();
    TestSavePlayerAndSkills();
    TestInvalidAndMissingProfilesAreRejected();
    TestFailedSaveRollsBack();

    std::cout << "All SQLite player repository tests passed.\n";
    return 0;
}
