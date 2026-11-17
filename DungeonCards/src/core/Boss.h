#pragma once
#include <string>
#include <vector>
//test_commit
namespace dc {

// Идентификаторы финальных боссов.
enum BossId {
    BossSlime = 0,      // делится на 2, затем на 4
    BossDevourer = 1,   // проглатывает героя
    BossKnight = 2,     // очень защищённый рыцарь
    BossArchmage = 3,   // хрупкий, но смертельный маг
    BossDruid = 4,      // крадёт лечение отряда
    BossTroll = 5,      // медленный, живучий, сильный
    BossNecro = 6,      // призывает скелетов и прячется за барьером
    BossDragon = 7,     // дыхание по всему отряду, ярость
    kBossCount = 8
};

// Карточка босса: показывается перед боем (и при выборе отряда).
struct BossInfo {
    std::string name;
    std::string title;
    int hp = 0, atk = 0, def = 0, spd = 0;
    std::vector<std::string> abilities;
    std::vector<std::string> strengths;
    std::vector<std::string> weaknesses;
};

const BossInfo& bossInfo(int bossId);

}  // namespace dc
