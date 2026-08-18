#include "PartyManager.h"

#include <cassert>
#include <iostream>

namespace
{
void TestCreateParty()
{
    dnf::PartyManager manager;

    const auto partyId = manager.CreateParty(100);
    assert(partyId.has_value());

    const auto party = manager.GetParty(partyId.value());
    assert(party.has_value());
    assert(party->leaderSessionId == 100);
    assert(party->members.size() == 1);
    assert(party->members[0] == 100);

    assert(!manager.CreateParty(100).has_value());
}

void TestPartyCapacity()
{
    dnf::PartyManager manager;
    const dnf::PartyId partyId = manager.CreateParty(100).value();

    assert(manager.JoinParty(partyId, 200) == dnf::JoinPartyResult::Success);
    assert(manager.JoinParty(partyId, 300) == dnf::JoinPartyResult::Success);
    assert(manager.JoinParty(partyId, 400) == dnf::JoinPartyResult::Success);
    assert(manager.JoinParty(partyId, 500) == dnf::JoinPartyResult::PartyFull);
}

void TestOnePartyPerSession()
{
    dnf::PartyManager manager;
    const dnf::PartyId firstPartyId = manager.CreateParty(100).value();
    const dnf::PartyId secondPartyId = manager.CreateParty(200).value();

    assert(manager.JoinParty(firstPartyId, 300) == dnf::JoinPartyResult::Success);
    assert(manager.JoinParty(secondPartyId, 300) ==
           dnf::JoinPartyResult::AlreadyJoined);
}

void TestLeaderTransferAndDisband()
{
    dnf::PartyManager manager;
    const dnf::PartyId partyId = manager.CreateParty(100).value();
    manager.JoinParty(partyId, 200);

    assert(manager.LeaveParty(100));

    const auto party = manager.GetParty(partyId);
    assert(party.has_value());
    assert(party->leaderSessionId == 200);
    assert(party->members.size() == 1);

    assert(manager.LeaveParty(200));
    assert(!manager.GetParty(partyId).has_value());
    assert(!manager.LeaveParty(999));
}
} // namespace

int main()
{
    TestCreateParty();
    TestPartyCapacity();
    TestOnePartyPerSession();
    TestLeaderTransferAndDisband();

    std::cout << "All party manager tests passed.\n";
    return 0;
}
