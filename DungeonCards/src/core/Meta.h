#pragma once
#include <array>
#include <string>
#include <vector>
#include "Party.h"

namespace dc {

// «Наследие» - метапрогрессия: постоянные улучшения, которые покупаются за артефакты
// и действуют во всех следующих забегах.
enum Upgrade {
    UpVitality,    // +макс. ЗДР всем героям
    UpMight,       // +шанс крита всем героям
    UpArmor,       // меньше входящего урона
    UpPurse,       // стартовые монеты
    UpGreed,       // больше монет с врагов
    UpHaggler,     // скидка у торговца
    UpProvisions,  // стартовые предметы в рюкзаке
    UpBuilder,     // +комнаты при строительстве
    UpReserve,     // +сбросы карт
    UpCamp,        // сильнее лечение на привале
    UpLife,        // дешевле воскрешение за монеты
    kUpgradeCount
};

struct UpgradeInfo {
    std::string name;
    std::string desc;
    int maxLevel = 0;
    std::vector<int> costs;  // costs[i] - цена перехода с уровня i на i+1 (артефактов)
};

const UpgradeInfo& upgradeInfo(int upgrade);

constexpr int kBaseReviveCost = 50;     // воскрешение за монеты на привале
constexpr int kBaseRestHealPct = 40;    // лечение на привале

struct Meta {
    std::array<int, kUpgradeCount> level{};

    int hpBonus() const { return level[UpVitality]; }
    int critBonusPct() const { return 2 * level[UpMight]; }
    int damageReducePct() const { return level[UpArmor]; }
    int startCoins() const { return 15 * level[UpPurse]; }
    int coinBonusPct() const { return 10 * level[UpGreed]; }
    int shopDiscountPct() const { return 5 * level[UpHaggler]; }
    int provisions() const { return level[UpProvisions]; }   // стартовые зелья т1: 1 или 2
    int extraPlacements() const { return level[UpBuilder]; }
    int extraDiscards() const { return level[UpReserve]; }
    int restHealPct() const { return kBaseRestHealPct + 5 * level[UpCamp]; }
    int reviveCoinCost() const;

    bool maxed(int upgrade) const { return level[upgrade] >= upgradeInfo(upgrade).maxLevel; }
    int nextCost(int upgrade) const;   // 0, если уровень максимальный
    // Применить улучшения к только что созданному герою (вызывать один раз).
    void applyToHero(Hero& h) const;
};

// Краткое описание эффекта улучшения на заданном уровне («+4 ЗДР»).
std::string upgradeEffect(int upgrade, int level);

}  // namespace dc
