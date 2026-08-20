#include "DevAuthTicketVerifier.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
void TestRegisteredTicketIsConsumedOnce()
{
    dnf::DevAuthTicketVerifier verifier;
    assert(verifier.RegisterTicket("ticket-100", {10, 100}));
    assert(verifier.TicketCount() == 1);

    const auto context = verifier.Verify("ticket-100");
    assert(context.has_value());
    assert(context->accountId == 10);
    assert(context->playerId == 100);
    assert(verifier.TicketCount() == 0);

    assert(!verifier.Verify("ticket-100").has_value());
}

void TestInvalidTicketIsRejected()
{
    dnf::DevAuthTicketVerifier verifier;

    assert(!verifier.RegisterTicket("", {10, 100}));
    assert(!verifier.RegisterTicket("ticket", {0, 100}));
    assert(!verifier.RegisterTicket("ticket", {10, 0}));

    const std::string longTicket(
        dnf::MAX_AUTH_TICKET_LENGTH + 1,
        'a');
    assert(!verifier.RegisterTicket(longTicket, {10, 100}));
    assert(!verifier.Verify(longTicket).has_value());
    assert(verifier.TicketCount() == 0);
}

void TestDuplicateTicketDoesNotReplaceContext()
{
    dnf::DevAuthTicketVerifier verifier;
    assert(verifier.RegisterTicket("same-ticket", {10, 100}));
    assert(!verifier.RegisterTicket("same-ticket", {20, 200}));

    const auto context = verifier.Verify("same-ticket");
    assert(context.has_value());
    assert(context->accountId == 10);
    assert(context->playerId == 100);
}
} // namespace

int main()
{
    TestRegisteredTicketIsConsumedOnce();
    TestInvalidTicketIsRejected();
    TestDuplicateTicketDoesNotReplaceContext();

    std::cout << "All dev auth ticket verifier tests passed.\n";
    return 0;
}
