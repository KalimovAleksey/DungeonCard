#include "Party.h"
#include <algorithm>
#include "Util.h"

namespace dc {

const char* roleTitle(Role r) {
    switch (r) {
        case Role::Warrior:   return "Танк";
        case Role::Mage:      return "Маг-разрушитель";
        case Role::Cleric:    return "Лекарь";
        case Role::Rogue:     return "Быстрый убийца";
        case Role::Archer:    return "Стрелок";
        case Role::Paladin:   return "Защитник-целитель";
        case Role::Berserker: return "Яростный боец";
        case Role::Druid:     return "Лекарь группы";
        case Role::Bard:      return "Вдохновитель";
    }
    return "";
}

const char* roleAbility(Role r) {
    switch (r) {
        case Role::Warrior:   return "Провокация: враги чаще целятся в него. Надёжная опора отряда.";
        case Role::Mage:      return "Магия: игнорирует половину защиты врага. Пассивно: зелья лечат на 50% сильнее.";
        case Role::Cleric:    return "Исцеление: лечит самого раненого союзника (АТК+3). Пассивно: шанс воскрешения на привале +25% (до 50%).";
        case Role::Rogue:     return "Критический удар: 25% шанс удвоить урон. Обезвреживает ловушки.";
        case Role::Archer:    return "Залп: каждая атака бьёт двух врагов с 80% урона. Пассивно: +25% монет с врагов, пока жив.";
        case Role::Paladin:   return "Провокация: после атаки лечит раненого на 2. Пассивно: привал лечит на 40% вместо 20%.";
        case Role::Berserker: return "Ярость: +50% урона, когда здоровье ниже половины.";
        case Role::Druid:     return "Целебный дождь: если кто-то ранен, лечит всех союзников на 2.";
        case Role::Bard:      return "Вдохновение: усиливает союзника на половину своей АТК. Пассивно: скидка у торговца 15%.";
    }
    return "";
}

const char* itemKindName(ItemKind k) {
    switch (k) {
        case ItemKind::Weapon: return "Оружие";
        case ItemKind::Armor:  return "Броня";
        case ItemKind::Relic:  return "Реликвия";
        case ItemKind::Potion: return "Зелье";
    }
    return "";
}

// ------------------------------------------------------------------- Hero

std::optional<Item>& Hero::slot(ItemKind k) {
    return k == ItemKind::Weapon ? weapon : k == ItemKind::Armor ? armor : relic;
}
const std::optional<Item>& Hero::slot(ItemKind k) const {
    return k == ItemKind::Weapon ? weapon : k == ItemKind::Armor ? armor : relic;
}

void Hero::recalc() {
    int hpB = 0, atkB = 0, defB = 0, spdB = 0, crit = 0, vamp = 0;
    if (weapon) atkB += weapon->power;
    if (armor) {
        defB += armor->power;
        hpB += armor->tier * 3;
    }
    if (relic) {
        switch (relic->relic) {
            case RelicEffect::Vitality: hpB += relic->power; break;
            case RelicEffect::Haste:    spdB += relic->power; break;
            case RelicEffect::Keen:     crit += relic->power; break;
            case RelicEffect::Vampire:  vamp += relic->power; break;
        }
    }
    const int newMax = baseHp + hpB;
    if (alive()) {
        if (newMax > maxHp) hp += newMax - maxHp;  // бонус к максимуму сразу даёт и здоровье
        hp = std::max(1, std::min(hp, newMax));
    }
    maxHp = newMax;
    atk = baseAtk + buffAtk + buffAtkCombat + atkB;
    def = baseDef + buffDef + defB;
    spd = baseSpd + spdB;
    critPct = crit;
    lifestealPct = vamp;
}

Hero makeHero(const std::string& name, Role role, int hp, int atk, int def, int spd) {
    Hero h;
    h.name = name;
    h.role = role;
    h.baseHp = h.hp = h.maxHp = hp;
    h.baseAtk = h.atk = atk;
    h.baseDef = h.def = def;
    h.baseSpd = h.spd = spd;
    return h;
}

std::vector<Hero> heroRoster() {
    return {
        makeHero("Воин",    Role::Warrior,   40, 8, 3, 4),
        makeHero("Маг",     Role::Mage,      26, 11, 1, 5),
        makeHero("Жрица",   Role::Cleric,    30, 5, 2, 3),
        makeHero("Вор",     Role::Rogue,     28, 9, 2, 7),
        makeHero("Лучник",  Role::Archer,    28, 10, 1, 6),
        makeHero("Паладин", Role::Paladin,   38, 6, 4, 3),
        makeHero("Берсерк", Role::Berserker, 34, 10, 1, 5),
        makeHero("Друид",   Role::Druid,     27, 4, 1, 4),
        makeHero("Бард",    Role::Bard, 29, 6, 2, 4),
    };
}

// ------------------------------------------------------------------- loot

Item rollLoot(Rng& rng, int tier) {
    tier = std::max(1, std::min(4, tier));
    static const char* weapons[4] = {"Ржавый меч", "Стальной клинок", "Зачарованный клинок", "Клинок героя"};
    static const char* armors[4]  = {"Кожаная куртка", "Кольчуга", "Латы", "Драконья чешуя"};
    static const char* potions[4] = {"Малое зелье", "Зелье лечения", "Большое зелье", "Эликсир жизни"};
    static const char* relics[4]  = {"Амулет жизни", "Перо ветра", "Глаз сокола", "Клык вампира"};

    Item it;
    it.tier = tier;
    int r = rng.range(1, 100);
    if (r <= 32) {
        it.kind = ItemKind::Weapon;
        it.name = weapons[tier - 1];
        it.power = tier * 2 + rng.range(0, 1);
    } else if (r <= 60) {
        it.kind = ItemKind::Armor;
        it.name = armors[tier - 1];
        it.power = tier + rng.range(0, 1);
    } else if (r <= 82) {
        it.kind = ItemKind::Relic;
        int e = rng.range(0, 3);
        it.relic = static_cast<RelicEffect>(e);
        it.name = relics[e];
        switch (it.relic) {
            case RelicEffect::Vitality: it.power = 7 * tier; break;
            case RelicEffect::Haste:    it.power = tier; break;
            case RelicEffect::Keen:     it.power = 8 * tier; break;
            case RelicEffect::Vampire:  it.power = 7 * tier; break;
        }
    } else {
        it.kind = ItemKind::Potion;
        it.name = potions[tier - 1];
        it.power = 10 * tier;
    }
    return it;
}

std::string itemBonusText(const Item& it) {
    switch (it.kind) {
        case ItemKind::Weapon: return fmt("+%d АТК", it.power);
        case ItemKind::Armor:  return fmt("+%d ЗАЩ, +%d ЗДР", it.power, it.tier * 3);
        case ItemKind::Potion: return fmt("лечит %d", it.power);
        case ItemKind::Relic:
            switch (it.relic) {
                case RelicEffect::Vitality: return fmt("+%d макс. ЗДР", it.power);
                case RelicEffect::Haste:    return fmt("+%d СКР", it.power);
                case RelicEffect::Keen:     return fmt("+%d%% шанс крита", it.power);
                case RelicEffect::Vampire:  return fmt("вампиризм %d%%", it.power);
            }
    }
    return "";
}

// ----------------------------------------------------------------- магазин

int itemPrice(const Item& it) {
    static const int weapon[4] = {20, 48, 115, 260};
    static const int armor[4]  = {20, 48, 115, 260};
    static const int relic[4]  = {26, 58, 135, 300};
    static const int potion[4] = {12, 26, 55, 120};
    const int t = std::max(1, std::min(4, it.tier)) - 1;
    switch (it.kind) {
        case ItemKind::Weapon: return weapon[t];
        case ItemKind::Armor:  return armor[t];
        case ItemKind::Relic:  return relic[t];
        case ItemKind::Potion: return potion[t];
    }
    return 0;
}

std::vector<ShopOffer> makeShopStock(Rng& rng, int count) {
    std::vector<ShopOffer> out;
    for (int i = 0; i < count; ++i) {
        const int r = rng.range(1, 100);
        const int tier = r <= 52 ? 1 : r <= 92 ? 2 : 3;  // т3 - редкость (8%)
        Item it;
        for (int attempt = 0; attempt < 12; ++attempt) {
            it = rollLoot(rng, tier);
            bool dup = false;
            for (const auto& o : out)
                if (o.item.name == it.name && o.item.power == it.power) dup = true;
            if (!dup) break;
        }
        ShopOffer o;
        o.item = it;
        o.price = itemPrice(it);
        out.push_back(o);
    }
    return out;
}

// ------------------------------------------------------------------ Party

Party::Party() {
    auto roster = heroRoster();
    heroes.assign(roster.begin(), roster.begin() + 4);
}

Party::Party(std::vector<Hero> h) : heroes(std::move(h)) {}

bool Party::anyAlive() const {
    return std::any_of(heroes.begin(), heroes.end(), [](const Hero& h) { return h.alive(); });
}
int Party::aliveCount() const {
    return static_cast<int>(std::count_if(heroes.begin(), heroes.end(), [](const Hero& h) { return h.alive(); }));
}
int Party::totalHp() const {
    int s = 0;
    for (auto& h : heroes) s += std::max(0, h.hp);
    return s;
}
int Party::totalMaxHp() const {
    int s = 0;
    for (auto& h : heroes) s += h.maxHp;
    return s;
}
void Party::healPercent(int pct) {
    for (auto& h : heroes)
        if (h.alive()) h.hp = std::min(h.maxHp, h.hp + h.maxHp * pct / 100);
}
void Party::buff(int index, int atk, int def) {
    if (index < 0 || index >= static_cast<int>(heroes.size()) || !heroes[index].alive()) return;
    heroes[index].buffAtk += atk;
    heroes[index].buffDef += def;
    heroes[index].recalc();
}
void Party::clearCombatBuffs() {
    for (auto& h : heroes) {
        if (h.buffAtkCombat != 0) {
            h.buffAtkCombat = 0;
            h.recalc();
        }
    }
}
Hero* Party::findAlive(Role r) {
    for (auto& h : heroes)
        if (h.alive() && h.role == r) return &h;
    return nullptr;
}

int Party::potionCount() const {
    return static_cast<int>(std::count_if(bag.begin(), bag.end(), [](const Item& i) { return i.kind == ItemKind::Potion; }));
}

bool Party::takeBestPotion(int& power) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(bag.size()); ++i)
        if (bag[i].kind == ItemKind::Potion && (best < 0 || bag[i].power > bag[best].power)) best = i;
    if (best < 0) return false;
    power = bag[best].power;
    // --- ИЗМЕНЕНИЕ: Увеличиваем силу лечения, если есть Маг ---
    if (hasLivingMage()) {
        power = power * 150 / 100;
    }
    bag.erase(bag.begin() + best);
    return true;
}

bool Party::equipFromBag(int bagIndex, int heroIndex) {
    if (bagIndex < 0 || bagIndex >= static_cast<int>(bag.size())) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(heroes.size())) return false;
    Hero& h = heroes[heroIndex];
    Item it = bag[bagIndex];
    if (it.kind == ItemKind::Potion || !h.alive()) return false;
    bag.erase(bag.begin() + bagIndex);
    std::optional<Item>& s = h.slot(it.kind);
    if (s) bag.push_back(*s);  // старый предмет возвращается в рюкзак
    s = it;
    h.recalc();
    return true;
}

bool Party::unequip(int heroIndex, ItemKind kind) {
    if (heroIndex < 0 || heroIndex >= static_cast<int>(heroes.size()) || kind == ItemKind::Potion) return false;
    Hero& h = heroes[heroIndex];
    std::optional<Item>& s = h.slot(kind);
    if (!s) return false;
    bag.push_back(*s);
    s.reset();
    h.recalc();
    return true;
}

bool Party::usePotion(int bagIndex, int heroIndex) {
    if (bagIndex < 0 || bagIndex >= static_cast<int>(bag.size())) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(heroes.size())) return false;
    Hero& h = heroes[heroIndex];
    if (bag[bagIndex].kind != ItemKind::Potion || !h.alive() || h.hp >= h.maxHp) return false;
    // --- ИЗМЕНЕНИЕ: Увеличиваем силу лечения, если есть Маг ---
    int healPower = bag[bagIndex].power;
    if (hasLivingMage()) {
        healPower = healPower * 150 / 100; // +50%
    }
    h.hp = std::min(h.maxHp, h.hp + bag[bagIndex].power);
    bag.erase(bag.begin() + bagIndex);
    return true;
}
bool Party::hasLivingMage() const {
    for (const auto& h : heroes) {
        if (h.alive() && h.role == Role::Mage) return true;
    }
    return false;
}
}  // namespace dc
