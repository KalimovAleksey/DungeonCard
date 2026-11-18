#include "Assets.h"

namespace dc {

const char* roleKey(Role r) {
    switch (r) {
        case Role::Warrior:   return "warrior";
        case Role::Mage:      return "mage";
        case Role::Cleric:    return "cleric";
        case Role::Rogue:     return "rogue";
        case Role::Archer:    return "archer";
        case Role::Paladin:   return "paladin";
        case Role::Berserker: return "berserker";
        case Role::Druid:     return "druid";
        case Role::Bard:      return "bard";
    }
    return "hero";
}

const char* bossKey(int id) {
    switch (id) {
        case BossSlime:    return "slime";
        case BossDevourer: return "devourer";
        case BossKnight:   return "knight";
        case BossArchmage: return "archmage";
        case BossDruid:    return "druid";
        case BossTroll:    return "troll";
        case BossNecro:    return "necromancer";
        case BossDragon:   return "dragon";
    }
    return "boss";
}

std::string heroArt(Role r) { return std::string("heroes/") + roleKey(r) + ".png"; }
std::string bossArt(int id) { return std::string("bosses/") + bossKey(id) + ".png"; }
std::string enemyArt(const std::string& key) { return "enemies/" + key + ".png"; }
std::string roomArt(RoomType t) { return std::string("rooms/") + roomTypeKey(t) + ".png"; }

std::string itemArt(const Item& it) {
    const std::string tier = std::to_string(std::max(1, std::min(4, it.tier)));
    switch (it.kind) {
        case ItemKind::Weapon: return "items/weapon_" + tier + ".png";
        case ItemKind::Armor:  return "items/armor_" + tier + ".png";
        case ItemKind::Potion: return "items/potion_" + tier + ".png";
        case ItemKind::Relic:
            switch (it.relic) {
                case RelicEffect::Vitality: return "items/relic_vitality.png";
                case RelicEffect::Haste:    return "items/relic_haste.png";
                case RelicEffect::Keen:     return "items/relic_keen.png";
                case RelicEffect::Vampire:  return "items/relic_vampire.png";
            }
    }
    return "items/unknown.png";
}

std::vector<AssetSpec> assetManifest() {
    std::vector<AssetSpec> m;
    auto add = [&](const std::string& path, int w, int h, const char* group, const std::string& desc, bool opt = false) {
        m.push_back({path, w, h, group, desc, opt});
    };

    // герои: бой, выбор отряда, привал, инвентарь
    const Role roles[8] = {Role::Warrior, Role::Mage, Role::Cleric, Role::Rogue,
                           Role::Archer, Role::Paladin, Role::Berserker, Role::Druid};
    const char* roleNames[8] = {"Воин", "Маг", "Жрица", "Вор", "Лучник", "Паладин", "Берсерк", "Друид"};
    for (int i = 0; i < 8; ++i)
        add(heroArt(roles[i]), 128, 128, "Герои", std::string("Герой: ") + roleNames[i] + ". Лицом вправо (к врагам)");

    // боссы
    for (int b = 0; b < kBossCount; ++b)
        add(bossArt(b), 256, 256, "Боссы", std::string("Босс: ") + bossInfo(b).name + ". Лицом влево");
    add("bosses/slime_medium.png", 192, 192, "Боссы", "Слизень-Делитель, 1-е деление (если нет файла - slime.png)", true);
    add("bosses/slime_small.png", 128, 128, "Боссы", "Слизень-Делитель, 2-е деление (если нет файла - slime.png)", true);

    // обычные враги
    const char* enemyKeys[] = {"goblin", "rat", "skeleton", "orc", "ghoul", "bandit", "ogre", "death_knight", "demon"};
    const char* enemyNames[] = {"Гоблин", "Крыса", "Скелет", "Орк", "Гуль", "Бандит", "Огр", "Рыцарь смерти", "Демон"};
    for (int i = 0; i < 9; ++i)
        add(enemyArt(enemyKeys[i]), 128, 128, "Враги", std::string("Враг: ") + enemyNames[i] + ". Лицом влево");
    add(enemyArt("skeleton_minion"), 128, 128, "Враги", "Скелет, призванный некромантом. Лицом влево");

    // предметы
    const char* tierNames[4] = {"ур.1", "ур.2", "ур.3", "ур.4"};
    for (int t = 1; t <= 4; ++t) {
        add("items/weapon_" + std::to_string(t) + ".png", 128, 128, "Предметы", std::string("Оружие, ") + tierNames[t - 1]);
        add("items/armor_" + std::to_string(t) + ".png", 128, 128, "Предметы", std::string("Броня, ") + tierNames[t - 1]);
        add("items/potion_" + std::to_string(t) + ".png", 128, 128, "Предметы", std::string("Зелье, ") + tierNames[t - 1]);
    }
    add("items/relic_vitality.png", 128, 128, "Предметы", "Реликвия: Амулет жизни (+макс. ЗДР)");
    add("items/relic_haste.png", 128, 128, "Предметы", "Реликвия: Перо ветра (+СКР)");
    add("items/relic_keen.png", 128, 128, "Предметы", "Реликвия: Глаз сокола (+крит)");
    add("items/relic_vampire.png", 128, 128, "Предметы", "Реликвия: Клык вампира (вампиризм)");

    // комнаты (плитка на карте подземелья)
    const RoomType rooms[7] = {RoomType::Start, RoomType::Combat, RoomType::Treasure, RoomType::Trap,
                               RoomType::Rest, RoomType::Boss, RoomType::Shop};
    for (RoomType t : rooms)
        add(roomArt(t), 128, 128, "Комнаты", std::string("Плитка комнаты: ") + roomTypeName(t) + ". На карте занимает 60x60 внутри клетки; подпись и звёзды рисуются поверх по центру");

    // интерфейс
    add("ui/coin.png", 64, 64, "Интерфейс", "Монета (панель, лавка, всплывающие числа)");
    add("ui/artifact.png", 64, 64, "Интерфейс", "Артефакт - постоянная валюта (меню, достижения, Наследие, привал)");
    add("ui/slot.png", 128, 128, "Интерфейс", "Фон ячейки инвентаря и лавки (квадрат)");
    add("ui/slot_weapon.png", 128, 128, "Интерфейс", "Пустой слот оружия у героя");
    add("ui/slot_armor.png", 128, 128, "Интерфейс", "Пустой слот брони у героя");
    add("ui/slot_relic.png", 128, 128, "Интерфейс", "Пустой слот реликвии у героя");

    // фоны (необязательные)
    add("backgrounds/menu.png", 1920, 1080, "Фоны", "Фон главного меню и настроек (16:9)", true);
    add("backgrounds/game.png", 1920, 1080, "Фоны", "Фон игровых экранов (16:9)", true);
    add("backgrounds/arena.png", 1080, 840, "Фоны", "Фон арены боя (9:7, рисуется 720x560)", true);
    return m;
}

}  // namespace dc
