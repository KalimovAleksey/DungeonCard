#include "Meta.h"
#include <algorithm>
#include "Util.h"

namespace dc {

const UpgradeInfo& upgradeInfo(int u) {
    static const UpgradeInfo infos[kUpgradeCount] = {
        {"Закалка", "Все герои получают немного больше максимального здоровья.", 2, {8, 14}},
        {"Меткость", "У всех героев растёт шанс критического удара (удвоенный урон).", 4, {6, 9, 12, 15}},
        {"Закалённые доспехи", "Весь отряд получает меньше урона от врагов.", 5, {6, 8, 10, 12, 15}},
        {"Походный кошелёк", "Отряд начинает поход с монетами.", 5, {1, 2, 3, 4, 5}},
        {"Охотник за трофеями", "С врагов выпадает больше монет.", 5, {2, 2, 3, 3, 4}},
        {"Торговые связи", "Торговец продаёт дешевле.", 4, {2, 3, 4, 5}},
        {"Дар предков", "В начале похода в рюкзаке лежат целебные зелья.", 2, {6, 10}},
        {"Мастер-строитель", "Можно поставить на одну комнату больше.", 1, {15}},
        {"Запасная колода", "Больше сбросов карт при строительстве.", 3, {2, 3, 4}},
        {"Уютный лагерь", "Привал лечит сильнее.", 3, {3, 4, 5}},
        {"Школа жизни", "Воскрешение за монеты дешевле.", 3, {2, 3, 4}},
    };
    return infos[std::max(0, std::min(kUpgradeCount - 1, u))];
}

int Meta::reviveCoinCost() const { return std::max(10, kBaseReviveCost - 8 * level[UpLife]); }

int Meta::nextCost(int u) const {
    const UpgradeInfo& info = upgradeInfo(u);
    const int l = level[u];
    if (l >= info.maxLevel || l >= static_cast<int>(info.costs.size())) return 0;
    return info.costs[l];
}

void Meta::applyToHero(Hero& h) const {
    h.baseHp += hpBonus();
    h.metaCritPct += critBonusPct();
    h.recalc();
    h.hp = h.maxHp;
}

std::string upgradeEffect(int u, int l) {
    if (l <= 0) return "нет";
    switch (u) {
        case UpVitality:   return fmt("+%d ЗДР", l);
        case UpMight:      return fmt("+%d%% крит", 2 * l);
        case UpArmor:      return fmt("-%d%% урона", l);
        case UpPurse:      return fmt("+%d монет", 15 * l);
        case UpGreed:      return fmt("+%d%% монет", 10 * l);
        case UpHaggler:    return fmt("-%d%% цен", 5 * l);
        case UpProvisions: return l == 1 ? "1 зелье" : "2 зелья";
        case UpBuilder:    return fmt("+%d комн.", l);
        case UpReserve:    return fmt("+%d сброс", l);
        case UpCamp:       return fmt("лечение %d%%", kBaseRestHealPct + 5 * l);
        case UpLife:       return fmt("%d монет", std::max(10, kBaseReviveCost - 8 * l));
    }
    return "";
}

}  // namespace dc
