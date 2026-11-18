#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Rng.h"

namespace dc {

enum class Role { Warrior, Mage, Cleric, Rogue, Archer, Paladin, Berserker, Druid };

inline bool isHealer(Role r) { return r == Role::Cleric || r == Role::Druid; }
inline bool isTank(Role r) { return r == Role::Warrior || r == Role::Paladin; }
const char* roleTitle(Role r);    // короткое описание роли
const char* roleAbility(Role r);  // описание умения

enum class ItemKind { Weapon, Armor, Relic, Potion };
enum class RelicEffect { Vitality, Haste, Keen, Vampire };

struct Item {
    std::string name;
    ItemKind kind = ItemKind::Weapon;
    int tier = 1;     // 1..4
    int power = 0;    // основная величина (АТК / ЗАЩ / лечение / сила реликвии)
    RelicEffect relic = RelicEffect::Vitality;
};

// Добыча по уровню 1..4 (чем выше сложность комнаты, тем сильнее предмет).
Item rollLoot(Rng& rng, int tier);
Item makePotion(int tier);                   // зелье заданного уровня (без случайности)
std::string itemBonusText(const Item& it);   // короткое описание эффекта
const char* itemKindName(ItemKind k);

struct Hero {
    std::string name;
    Role role = Role::Warrior;
    // текущие значения (с учётом снаряжения и баффов)
    int hp = 1, maxHp = 1, atk = 1, def = 0, spd = 1;
    int critPct = 0;       // шанс крита (реликвии + Наследие)
    int lifestealPct = 0;  // вампиризм от реликвий
    // базовые значения
    int baseHp = 1, baseAtk = 1, baseDef = 0, baseSpd = 1;
    int metaCritPct = 0;   // шанс крита от «Наследия»
    int buffAtk = 0, buffDef = 0;  // баффы с привала
    bool away = false;             // проглочен боссом: не участвует в бою
    std::optional<Item> weapon, armor, relic;

    bool alive() const { return hp > 0; }
    bool inFight() const { return hp > 0 && !away; }
    void recalc();                       // пересчёт характеристик из базовых + снаряжение
    std::optional<Item>& slot(ItemKind k);
    const std::optional<Item>& slot(ItemKind k) const;
};

Hero makeHero(const std::string& name, Role role, int hp, int atk, int def, int spd);

// Все доступные герои (для экрана выбора отряда).
std::vector<Hero> heroRoster();

// Лот у торговца.
struct ShopOffer {
    Item item;
    int price = 0;
    bool sold = false;
};
int itemPrice(const Item& it);                              // цена в монетах
std::vector<ShopOffer> makeShopStock(Rng& rng, int count, int discountPct = 0);  // т1 часто, т2 нередко, т3 редко

class Party {
public:
    Party();                                // отряд по умолчанию: первые четыре героя
    explicit Party(std::vector<Hero> heroes);

    bool anyAlive() const;
    int aliveCount() const;
    int totalHp() const;
    int totalMaxHp() const;
    void healPercent(int pct);
    void buff(int index, int atk, int def);
    Hero* findAlive(Role r);

    // --- рюкзак ---
    int potionCount() const;
    bool takeBestPotion(int& power);                 // для автобоя: достаёт лучшее зелье
    bool equipFromBag(int bagIndex, int heroIndex);  // надеть (старый предмет возвращается в рюкзак)
    bool unequip(int heroIndex, ItemKind slot);      // снять в рюкзак
    bool usePotion(int bagIndex, int heroIndex);     // выпить зелье вне боя

    std::vector<Hero> heroes;
    std::vector<Item> bag;
    int coins = 0;  // монеты (падают с врагов, тратятся у торговца)
};

}  // namespace dc
