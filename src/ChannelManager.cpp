#include "ChannelManager.h"

#include <algorithm>
#include <utility>

namespace dnf
{
bool ChannelManager::AddChannel(
    ChannelId channelId,
    std::string name,
    std::size_t maxPlayers)
{
    std::lock_guard lock(mutex_);

    if (channelId == 0 || name.empty() || maxPlayers == 0 ||
        channels_.contains(channelId))
    {
        return false;
    }

    ChannelInfo channel;
    channel.id = channelId;
    channel.name = std::move(name);
    channel.maxPlayers = maxPlayers;

    channels_.emplace(channelId, std::move(channel));
    return true;
}

std::vector<ChannelInfo> ChannelManager::GetChannelList() const
{
    std::lock_guard lock(mutex_);

    std::vector<ChannelInfo> channelList;
    channelList.reserve(channels_.size());

    for (const auto& [channelId, channel] : channels_)
    {
        channelList.push_back(channel);
    }

    std::sort(
        channelList.begin(),
        channelList.end(),
        [](const ChannelInfo& left, const ChannelInfo& right)
        {
            return left.id < right.id;
        });

    return channelList;
}

JoinChannelResult ChannelManager::JoinChannel(
    SessionId sessionId,
    ChannelId channelId)
{
    std::lock_guard lock(mutex_);

    auto channelIt = channels_.find(channelId);
    if (channelIt == channels_.end())
    {
        return JoinChannelResult::ChannelNotFound;
    }

    if (joinedChannels_.contains(sessionId))
    {
        return JoinChannelResult::AlreadyJoined;
    }

    ChannelInfo& channel = channelIt->second;
    if (channel.currentPlayers >= channel.maxPlayers)
    {
        return JoinChannelResult::ChannelFull;
    }

    ++channel.currentPlayers;
    joinedChannels_.emplace(sessionId, channelId);
    return JoinChannelResult::Success;
}

bool ChannelManager::LeaveChannel(SessionId sessionId)
{
    std::lock_guard lock(mutex_);

    auto joinedIt = joinedChannels_.find(sessionId);
    if (joinedIt == joinedChannels_.end())
    {
        return false;
    }

    auto channelIt = channels_.find(joinedIt->second);
    if (channelIt != channels_.end() && channelIt->second.currentPlayers > 0)
    {
        --channelIt->second.currentPlayers;
    }

    joinedChannels_.erase(joinedIt);
    return true;
}

std::optional<ChannelId> ChannelManager::GetJoinedChannel(SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    auto joinedIt = joinedChannels_.find(sessionId);
    if (joinedIt == joinedChannels_.end())
    {
        return std::nullopt;
    }

    return joinedIt->second;
}
} // namespace dnf
