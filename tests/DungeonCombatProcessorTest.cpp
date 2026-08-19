#include "DungeonCombatProcessor.h"
#include "DungeonProtocol.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
void WaitForAttackCount(
    const dnf::DungeonUdpManager& udpManager,
    dnf::DungeonId dungeonId,
    std::size_t expectedCount)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (udpManager.PendingAttackCount(dungeonId) == expectedCount)
        {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

dnf::SkillTemplate MakeIceSlash()
{
    dnf::SkillTemplate skill;
    skill.id = 1001;
    skill.name = "Ice Slash";
    skill.cooldownTicks = 2;
    skill.manaCost = 60;
    skill.activeTicks = 3;
    skill.hitBox = {150.0f, 0.0f, 60.0f, 120.0f};
    skill.effects = {
        {dnf::SkillEffectType::Damage,
         dnf::SkillTargetType::Enemy,
         dnf::SkillStat::None,
         1.5f,
         0}};
    return skill;
}

void TestRegisteredSkillAttackIsAccepted()
{
    using boost::asio::ip::udp;

    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);

    const dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    assert(dungeonCatalog.AddDungeon(1001, "Forest", {room}));

    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(dungeonManager.StartDungeon(dungeonId));

    dnf::SkillCatalog skillCatalog;
    assert(skillCatalog.AddSkill(MakeIceSlash()));

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager udpManager(serverIoContext);
    const auto port = udpManager.Allocate(dungeonId, {100});
    const auto token = udpManager.FindToken(dungeonId, 100);
    assert(port.has_value());
    assert(token.has_value());

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    boost::asio::io_context clientIoContext;
    udp::socket client(clientIoContext, udp::endpoint(udp::v4(), 0));
    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    const auto hello = dnf::EncodeUdpHello(
        {dungeonId, 100, token.value()});
    client.send_to(boost::asio::buffer(hello), serverEndpoint);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (udpManager.FindEndpoint(dungeonId, 100).has_value())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    dnf::PlayerAttackMessage attack;
    attack.dungeonId = dungeonId;
    attack.sequence = 1;
    attack.skillId = 1001;
    attack.directionX = 1.0f;
    const auto validAttack = dnf::EncodePlayerAttack(attack);

    attack.sequence = 2;
    attack.skillId = 9999;
    const auto unknownSkillAttack = dnf::EncodePlayerAttack(attack);

    client.send_to(boost::asio::buffer(validAttack), serverEndpoint);
    client.send_to(boost::asio::buffer(unknownSkillAttack), serverEndpoint);
    WaitForAttackCount(udpManager, dungeonId, 2);

    dnf::DungeonCombatProcessor processor(
        dungeonManager,
        udpManager,
        skillCatalog);
    const dnf::CombatProcessResult result = processor.Process(dungeonId);

    assert(result.receivedCount == 2);
    assert(result.acceptedCount == 1);
    assert(result.rejectedCount == 1);
    assert(udpManager.PendingAttackCount(dungeonId) == 0);

    const auto player = created.dungeon->FindPlayer(100);
    assert(player != nullptr);
    assert(player->CurrentMp() == 40);
    assert(player->RemainingCooldown(1001) == 2);

    attack.sequence = 3;
    attack.skillId = 1001;
    const auto cooldownAttack = dnf::EncodePlayerAttack(attack);
    client.send_to(boost::asio::buffer(cooldownAttack), serverEndpoint);
    WaitForAttackCount(udpManager, dungeonId, 1);

    const dnf::CombatProcessResult onCooldown =
        processor.Process(dungeonId);
    assert(onCooldown.receivedCount == 1);
    assert(onCooldown.acceptedCount == 0);
    assert(onCooldown.rejectedCount == 1);
    assert(player->CurrentMp() == 40);
    assert(player->RemainingCooldown(1001) == 1);

    const dnf::CombatProcessResult cooldownAdvanced =
        processor.Process(dungeonId);
    assert(cooldownAdvanced.receivedCount == 0);
    assert(player->RemainingCooldown(1001) == 0);

    attack.sequence = 4;
    const auto insufficientManaAttack = dnf::EncodePlayerAttack(attack);
    client.send_to(
        boost::asio::buffer(insufficientManaAttack),
        serverEndpoint);
    WaitForAttackCount(udpManager, dungeonId, 1);

    const dnf::CombatProcessResult insufficientMana =
        processor.Process(dungeonId);
    assert(insufficientMana.receivedCount == 1);
    assert(insufficientMana.acceptedCount == 0);
    assert(insufficientMana.rejectedCount == 1);
    assert(player->CurrentMp() == 40);

    const dnf::CombatProcessResult missingDungeon = processor.Process(9999);
    assert(missingDungeon.receivedCount == 0);

    udpManager.Release(dungeonId);
    serverThread.join();
}
} // namespace

int main()
{
    TestRegisteredSkillAttackIsAccepted();

    std::cout << "All dungeon combat processor tests passed.\n";
    return 0;
}
