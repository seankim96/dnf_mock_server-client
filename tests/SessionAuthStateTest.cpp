#include "SessionAuthState.h"

#include <cassert>
#include <iostream>

namespace
{
void TestAuthenticationStoresPlayerIdentity()
{
    dnf::SessionAuthState state;
    dnf::PlayerProfile profile;
    profile.playerId = 100;
    profile.name = "Player_1";

    assert(!state.IsAuthenticated());
    assert(state.Authenticate({10, 100}, profile));
    assert(state.IsAuthenticated());

    const auto snapshot = state.Snapshot();
    assert(snapshot.has_value());
    assert(snapshot->authContext.accountId == 10);
    assert(snapshot->authContext.playerId == 100);
    assert(snapshot->profile.name == "Player_1");
}

void TestInvalidIdentityIsRejected()
{
    dnf::SessionAuthState state;
    dnf::PlayerProfile profile;
    profile.playerId = 100;
    profile.name = "Player_1";

    assert(!state.Authenticate({0, 100}, profile));
    assert(!state.Authenticate({10, 200}, profile));
    assert(!state.IsAuthenticated());
}

void TestAuthenticatedIdentityCannotBeReplaced()
{
    dnf::SessionAuthState state;
    dnf::PlayerProfile firstProfile;
    firstProfile.playerId = 100;
    firstProfile.name = "Player_1";
    assert(state.Authenticate({10, 100}, firstProfile));

    dnf::PlayerProfile secondProfile;
    secondProfile.playerId = 200;
    secondProfile.name = "Player_2";
    assert(!state.Authenticate({20, 200}, secondProfile));

    const auto snapshot = state.Snapshot();
    assert(snapshot.has_value());
    assert(snapshot->authContext.playerId == 100);
}
} // namespace

int main()
{
    TestAuthenticationStoresPlayerIdentity();
    TestInvalidIdentityIsRejected();
    TestAuthenticatedIdentityCannotBeReplaced();

    std::cout << "All session auth state tests passed.\n";
    return 0;
}
