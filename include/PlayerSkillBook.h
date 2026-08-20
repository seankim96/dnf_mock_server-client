#pragma once

#include "SkillCatalog.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dnf
{
struct OwnedSkill
{
    SkillId skillId = 0;
    std::uint32_t level = 0;
};

class PlayerSkillBook
{
public:
    explicit PlayerSkillBook(const SkillCatalog& skillCatalog);

    bool LearnSkill(SkillId skillId);
    bool HasSkill(SkillId skillId) const;
    std::uint32_t SkillLevel(SkillId skillId) const;
    std::vector<OwnedSkill> Snapshot() const;

private:
    const SkillCatalog& skillCatalog_;

    mutable std::mutex mutex_;
    std::unordered_map<SkillId, std::uint32_t> skillLevels_;
};
} // namespace dnf
