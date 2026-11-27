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
        case Role::Seidmard:  return "Разрушитель брони";
        case Role::Monk:      return "Стойкий отшельник";
    }
    return "";
}

const char* roleAbility(Role r) {
    switch (r) {
        case Role::Warrior:   return "Провокация: враги чаще целятся в него. Надёжная опора отряда.";
        case Role::Cleric:    return "Исцеление: на своём ходу лечит самого раненого союзника (АТК+3).";
        case Role::Rogue:     return "Критический удар: 25% шанс удвоить урон. Всегда обезвреживает ловушки: урона нет.";
        case Role::Archer:    return "Залп: бьёт двух врагов с 80% урона, а единственную цель - одним выстрелом со 100% урона.";
        case Role::Paladin:   return "Провокация и благословение: после атаки лечит самого раненого на 2.";
        case Role::Berserker: return "Ярость: доп. удар ниже половины ЗДР; аура: вампиризм 10% всей группе.";
        case Role::Druid:     return "Цепное лечение: 6/5/4/3+половина ЗАЩ всем союзникам после атаки.";
        case Role::Bard:      return "Вдохновение: усиливает сильнейшего союзника в бою; даёт скидку у торговца.";
        case Role::Mage:      return "Магия: игнорирует половину ЗАЩ; каждый 3-й ход после атаки - арканный взрыв по всем врагам (60% АТК).";
        case Role::Seidmard:  return "Руна: -2 ЗАЩ самому защищённому врагу (до -6) за удар; затем магия 50% АТК. Кузница: -20%.";
        case Role::Monk:      return "Не носит оружие и броню, но тренировка даёт +4 АТК/+2 ЗАЩ. Стойка меняет роль в бою.";
    }
    return "";
}

const char* stanceName(Stance s) {
    switch (s) {
        case Stance::Wind:      return "Ветер";
        case Stance::Lightning: return "Молния";
        case Stance::Earth:     return "Земля";
    }
    return "";
}
const char* stanceShort(Stance s) {
    switch (s) {
        case Stance::Wind:      return "Ветер";
        case Stance::Lightning: return "Молния";
        case Stance::Earth:     return "Земля";
    }
    return "";
}
const char* stanceDesc(Stance s) {
    switch (s) {
        case Stance::Wind:      return "35% отражение: урона нет, враг получает столько же. Перо ветра усиливает шанс.";
        case Stance::Lightning: return "130% АТК по 1-2 целям, магия игнорирует половину ЗАЩ.";
        case Stance::Earth:     return "+4 ЗАЩ, щит союзнику и 20% урона себе (до 35% ЗДР за раунд). Танк.";
    }
    return "";
}

std::vector<HeroVariant> heroVariants(Role r) {
    if (r != Role::Monk) return {};
    std::vector<HeroVariant> out;
    for (int i = 0; i < 3; ++i) {
        const Stance s = static_cast<Stance>(i);
        out.push_back({i, stanceShort(s), stanceDesc(s)});
    }
    return out;
}

int heroVariant(const Hero& h) {
    return h.role == Role::Monk ? static_cast<int>(h.stance) : 0;
}

void setHeroVariant(Hero& h, int id) {
    if (h.role != Role::Monk || id < 0 || id >= 3) return;
    h.stance = static_cast<Stance>(id);
    h.recalc();  // Земля даёт +4 ЗАЩ сразу, без ожидания следующего пересчёта
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
    // Стойка Земли компенсирует отсутствие брони постоянной +4 ЗАЩ.
    if (role == Role::Monk && stance == Stance::Earth) defB += 4;
    const int newMax = baseHp + hpB;
    if (alive()) {
        // Текущее здоровье следует за максимальным в обе стороны: надевание предмета
        // прибавляет и текущее, снятие - возвращает оба значения. Иначе снятие
        // бесплатно лечило бы героя (макс. упал, текущее осталось).
        hp += newMax - maxHp;
        hp = std::max(1, std::min(hp, newMax));
    }
    maxHp = newMax;
    // Отрицательные свойства уникального оружия (события): складываем с базой/баффами.
    const int penAtk = weapon ? weapon->eventPenaltyAtk : 0;
    const int penDef = armor ? armor->eventPenaltyDef : 0;
    atk = baseAtk + buffAtk + buffAtkCombat + atkB + penAtk;
    def = baseDef + buffDef + defB + penDef;
    spd = baseSpd + spdB;
    critPct = crit + metaCritPct;
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
        makeHero("Маг",     Role::Mage,      26, 10, 1, 5),  // АТК снижен с 11: появился арканный взрыв
        makeHero("Жрица",   Role::Cleric,    30, 5, 2, 3),
        makeHero("Вор",     Role::Rogue,     28, 9, 2, 7),
        makeHero("Лучник",  Role::Archer,    28, 10, 1, 6),
        makeHero("Паладин", Role::Paladin,   38, 6, 4, 3),
        makeHero("Берсерк", Role::Berserker, 40, 11, 2, 5),
        makeHero("Друид",   Role::Druid,     27, 4, 1, 4),
        makeHero("Бард",    Role::Bard,      29, 6, 2, 4),
        makeHero("Сейдмард", Role::Seidmard,  29, 10, 2, 5),  // стартовые значения после балансировки
        // База уже включает «виртуальное снаряжение» 2 тира: +3 АТК, +3 ЗАЩ, +6 ЗДР.
        // Слоты оружия/брони заблокированы, реликвия идёт сверху.
        makeHero("Монах",    Role::Monk,      40, 10, 5, 5),
    };
}

// ------------------------------------------------------------------- loot

Item rollLootOfKind(Rng& rng, ItemKind kind, int tier) {
    tier = std::max(1, std::min(4, tier));
    static const char* weapons[4] = {"Ржавый меч", "Стальной клинок", "Зачарованный клинок", "Клинок героя"};
    static const char* armors[4]  = {"Кожаная куртка", "Кольчуга", "Латы", "Драконья чешуя"};
    static const char* potions[4] = {"Малое зелье", "Зелье лечения", "Большое зелье", "Эликсир жизни"};
    static const char* relics[4]  = {"Амулет жизни", "Перо ветра", "Глаз сокола", "Клык вампира"};

    Item it;
    it.tier = tier;
    it.kind = kind;
    if (kind == ItemKind::Weapon) {
        it.name = weapons[tier - 1];
        it.power = tier * 2 + rng.range(0, 1);
    } else if (kind == ItemKind::Armor) {
        it.name = armors[tier - 1];
        it.power = tier + rng.range(0, 1);
    } else if (kind == ItemKind::Relic) {
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
        it.name = potions[tier - 1];
        it.power = 10 * tier;
    }
    return it;
}

Item rollLoot(Rng& rng, int tier) {
    tier = std::max(1, std::min(4, tier));
    int r = rng.range(1, 100);
    ItemKind kind = (r <= 32) ? ItemKind::Weapon
                  : (r <= 60) ? ItemKind::Armor
                  : (r <= 82) ? ItemKind::Relic
                              : ItemKind::Potion;
    return rollLootOfKind(rng, kind, tier);
}

Item makePotion(int tier) {
    static const char* potions[4] = {"Малое зелье", "Зелье лечения", "Большое зелье", "Эликсир жизни"};
    tier = std::max(1, std::min(4, tier));
    Item it;
    it.kind = ItemKind::Potion;
    it.tier = tier;
    it.name = potions[tier - 1];
    it.power = 10 * tier;
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


Item upgradeItem(const Item& it) {
    static const char* weapons[4] = {"Ржавый меч", "Стальной клинок", "Зачарованный клинок", "Клинок героя"};
    static const char* armors[4]  = {"Кожаная куртка", "Кольчуга", "Латы", "Драконья чешуя"};
    static const char* relics[4]  = {"Амулет жизни", "Перо ветра", "Глаз сокола", "Клык вампира"};
    Item up = it; up.tier = std::min(4, it.tier + 1);
    switch (up.kind) {
      case ItemKind::Weapon: up.name = weapons[up.tier-1]; up.power = up.tier*2+1; break;
      case ItemKind::Armor: up.name = armors[up.tier-1]; up.power = up.tier+1; break;
      case ItemKind::Relic: up.name = relics[static_cast<int>(it.relic)];
        switch(it.relic) { case RelicEffect::Vitality: up.power=7*up.tier; break; case RelicEffect::Haste: up.power=up.tier; break; case RelicEffect::Keen: up.power=8*up.tier; break; case RelicEffect::Vampire: up.power=7*up.tier; break; } break;
      case ItemKind::Potion: up.power=10*up.tier; break;
    } return up;
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

int itemSellPrice(const Item& it) {
    // Торговец выкупает вещь за половину стоимости - продажа никогда не
    // окупает покупку полностью, но позволяет вернуть часть монет.
    return std::max(1, itemPrice(it) / 2);
}

std::vector<ShopOffer> makeShopStock(Rng& rng, int count, int discountPct) {
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
        o.price = std::max(1, itemPrice(it) * (100 - std::max(0, std::min(90, discountPct))) / 100);
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
void Party::clearCombatBuffs() { for (auto& h : heroes) { h.buffAtkCombat = 0; h.recalc(); } }
void Party::buff(int index, int atk, int def) {
    if (index < 0 || index >= static_cast<int>(heroes.size()) || !heroes[index].alive()) return;
    heroes[index].buffAtk += atk;
    heroes[index].buffDef += def;
    heroes[index].recalc();
}
Hero* Party::findAlive(Role r) {
    for (auto& h : heroes) if (h.alive() && h.role == r) return &h;
    return nullptr;
}
const Hero* Party::findAlive(Role r) const {
    for (const auto& h : heroes) if (h.alive() && h.role == r) return &h;
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
    bag.erase(bag.begin() + best);
    return true;
}

bool Party::equipFromBag(int bagIndex, int heroIndex) {
    if (bagIndex < 0 || bagIndex >= static_cast<int>(bag.size())) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(heroes.size())) return false;
    Hero& h = heroes[heroIndex];
    Item it = bag[bagIndex];
    if (it.kind == ItemKind::Potion || !h.alive()) return false;
    // Монах сознательно отказывается от оружия и брони: его база уже их компенсирует.
    if (h.role == Role::Monk && (it.kind == ItemKind::Weapon || it.kind == ItemKind::Armor)) return false;
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
    h.hp = std::min(h.maxHp, h.hp + bag[bagIndex].power);
    bag.erase(bag.begin() + bagIndex);
    return true;
}

}  // namespace dc
