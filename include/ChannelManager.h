#pragma once

#include "SessionId.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dnf
{
using ChannelId = std::uint32_t;

struct ChannelInfo
{
    ChannelId id = 0;
    std::string name;
    std::size_t currentPlayers = 0;
    std::size_t maxPlayers = 0;
};

enum class JoinChannelResult : std::uint8_t
{
    Success = 0,
    ChannelNotFound = 1,
    ChannelFull = 2,
    AlreadyJoined = 3
};

class ChannelManager
{
public:
    bool AddChannel(ChannelId channelId, std::string name, std::size_t maxPlayers);

    std::vector<ChannelInfo> GetChannelList() const;
    JoinChannelResult JoinChannel(SessionId sessionId, ChannelId channelId);
    bool LeaveChannel(SessionId sessionId);
    std::optional<ChannelId> GetJoinedChannel(SessionId sessionId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<ChannelId, ChannelInfo> channels_;
    std::unordered_map<SessionId, ChannelId> joinedChannels_;
};
} // namespace dnf
