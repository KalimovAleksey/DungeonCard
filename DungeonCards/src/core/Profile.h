#pragma once
#include <array>
#include <set>
#include <string>
#include <vector>
#include "Boss.h"
#include "Meta.h"
#include "Party.h"

namespace dc {

constexpr int kBossArtifacts = 2;         // артефактов за победу над боссом
constexpr int kAchievementArtifacts = 3;  // артефактов за каждое достижение

// Достижения. id: 0..7 - победа над боссом (id босса), 100+роль - победа с героем в отряде,
// 200 - победа без потерь, 201 - победа, когда выжил один герой.
struct Achievement {
    int id = 0;
    std::string group;
    std::string name;
    std::string desc;
    int reward = kAchievementArtifacts;
};
const std::vector<Achievement>& achievements();

// Итог забега для профиля.
struct RunSummary {
    bool victory = false;
    int bossId = -1;
    std::vector<Role> squad;
    int casualties = 0;    // сколько героев погибало за забег (воскрешённый тоже считается)
    int aliveAtEnd = 0;
};

struct RunReward {
    int bossArtifacts = 0;
    std::vector<int> newAchievements;   // id только что полученных достижений
    int achievementArtifacts = 0;
    int total() const { return bossArtifacts + achievementArtifacts; }
};

// Постоянный профиль игрока: артефакты (не сбрасываются между забегами), статистика,
// достижения и уровни «Наследия». Хранится в файле рядом с игрой.
class Profile {
public:
    int artifacts = 0;        // доступные артефакты
    int artifactsEarned = 0;  // всего заработано
    int runs = 0, wins = 0;
    std::array<int, kBossCount> bossWins{};
    std::array<int, 8> heroWins{};      // по ролям (Role)
    int flawlessWins = 0, lastStandWins = 0;
    std::set<int> unlocked;
    Meta meta;

    RunReward recordRun(const RunSummary& s);
    bool isUnlocked(int achievementId) const { return unlocked.count(achievementId) > 0; }
    int unlockedCount() const { return static_cast<int>(unlocked.size()); }
    bool buyUpgrade(int upgrade);

    std::string serialize() const;
    static Profile parse(const std::string& text);
    bool saveFile(const std::string& path) const;
    static Profile loadFile(const std::string& path);
};

}  // namespace dc
