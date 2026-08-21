#include "AuthServerSessionState.h"

#include <cassert>
#include <iostream>

namespace
{
void TestStartsUnauthenticated()
{
    dnf::AuthServerSessionState state;

    assert(!state.IsAuthenticated());
    assert(!state.AuthenticatedAccount().has_value());
}

void TestStoresAuthenticatedAccount()
{
    dnf::AuthServerSessionState state;

    assert(state.MarkAuthenticated(10));
    assert(state.IsAuthenticated());
    assert(state.AuthenticatedAccount() == 10);
}

void TestInvalidAccountIsRejected()
{
    dnf::AuthServerSessionState state;

    assert(!state.MarkAuthenticated(0));
    assert(!state.IsAuthenticated());
}

void TestAuthenticatedAccountCannotBeReplaced()
{
    dnf::AuthServerSessionState state;

    assert(state.MarkAuthenticated(10));
    assert(!state.MarkAuthenticated(20));
    assert(state.AuthenticatedAccount() == 10);
}
} // namespace

int main()
{
    TestStartsUnauthenticated();
    TestStoresAuthenticatedAccount();
    TestInvalidAccountIsRejected();
    TestAuthenticatedAccountCannotBeReplaced();

    std::cout << "All auth server session state tests passed.\n";
    return 0;
}
