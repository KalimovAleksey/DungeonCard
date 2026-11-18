#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Rng.h"

namespace dc {

enum class Role { Warrior, Mage, Cleric, Rogue, Archer, Paladin, Berserker, Druid, Bard };

inline bool isHealer(Role r) { return r == Role::Cleric || r == Role::Druid; }
inline bool isTank(Role r) { return r == Role::Warrior || r == Role::Paladin; }
const char* roleTitle(Role r);
const char* roleAbility(Role r);

enum class ItemKind { Weapon, Armor, Relic, Potion };
enum class RelicEffect { Vitality, Haste, Keen, Vampire };

struct Item {
    std::string name;
    ItemKind kind = ItemKind::Weapon;
    int tier = 1;
    int power = 0;
    RelicEffect relic = RelicEffect::Vitality;
};

Item rollLoot(Rng& rng, int tier);
std::string itemBonusText(const Item& it);
const char* itemKindName(ItemKind k);
Item upgradeItem(const Item& it);

struct Hero {
    std::string name;
    Role role = Role::Warrior;
    int hp = 1, maxHp = 1, atk = 1, def = 0, spd = 1;
    int critPct = 0;
    int lifestealPct = 0;
    int baseHp = 1, baseAtk = 1, baseDef = 0, baseSpd = 1;
    int buffAtk = 0, buffDef = 0;
    int buffAtkCombat = 0;
    bool away = false;
    std::optional<Item> weapon, armor, relic;

    bool alive() const { return hp > 0; }
    bool inFight() const { return hp > 0 && !away; }
    void recalc();
    std::optional<Item>& slot(ItemKind k);
    const std::optional<Item>& slot(ItemKind k) const;
};

Hero makeHero(const std::string& name, Role role, int hp, int atk, int def, int spd);
std::vector<Hero> heroRoster();

struct ShopOffer {
    Item item;
    int price = 0;
    bool sold = false;
};
int itemPrice(const Item& it);
std::vector<ShopOffer> makeShopStock(Rng& rng, int count);

class Party {
public:
    Party();
    explicit Party(std::vector<Hero> heroes);

    bool anyAlive() const;
    bool hasLivingMage() const;
    int aliveCount() const;
    int totalHp() const;
    int totalMaxHp() const;
    void healPercent(int pct);
    void buff(int index, int atk, int def);
    void clearCombatBuffs();          // <-- ДОЛЖНО БЫТЬ ЗДЕСЬ
    Hero* findAlive(Role r);

    int potionCount() const;
    bool takeBestPotion(int& power);
    bool equipFromBag(int bagIndex, int heroIndex);
    bool unequip(int heroIndex, ItemKind slot);
    bool usePotion(int bagIndex, int heroIndex);

    std::vector<Hero> heroes;
    std::vector<Item> bag;
    int coins = 0;
};

}  // namespace dc