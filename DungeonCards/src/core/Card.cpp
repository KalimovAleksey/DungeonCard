#include "Card.h"
#include "Boss.h"

namespace dc {

const char* roomTypeName(RoomType t) {
    switch (t) {
        case RoomType::Start:    return "Вход";
        case RoomType::Combat:   return "Бой";
        case RoomType::Treasure: return "Клад";
        case RoomType::Trap:     return "Ловушка";
        case RoomType::Rest:     return "Отдых";
        case RoomType::Boss:     return "БОСС";
        case RoomType::Shop:     return "Торговец";
    }
    return "?";
}

const char* roomTypeHint(RoomType t) {
    switch (t) {
        case RoomType::Start:    return "Отсюда начинается поход";
        case RoomType::Combat:   return "Враги; добыча зависит от сложности";
        case RoomType::Treasure: return "Охрана (со 2 ур.) и добыча";
        case RoomType::Trap:     return "Урон двум разным героям; Вор снижает его";
        case RoomType::Rest:     return "Привал: лечение всех или бафф двух героев";
        case RoomType::Boss:     return "Финал: победа в забеге";
        case RoomType::Shop:     return "Лавка у края карты: подключи её";
    }
    return "";
}

const char* roomTypeKey(RoomType t) {
    switch (t) {
        case RoomType::Start:    return "start";
        case RoomType::Combat:   return "combat";
        case RoomType::Treasure: return "treasure";
        case RoomType::Trap:     return "trap";
        case RoomType::Rest:     return "rest";
        case RoomType::Boss:     return "boss";
        case RoomType::Shop:     return "shop";
    }
    return "room";
}

static uint8_t randomDoors(Rng& rng) {
    const uint8_t N = doorBit(North), E = doorBit(East), S = doorBit(South);
    int r = rng.range(1, 100);
    uint8_t m;
    if (r <= 20)      m = N;                                   // тупик
    else if (r <= 40) m = static_cast<uint8_t>(N | S);         // коридор
    else if (r <= 65) m = static_cast<uint8_t>(N | E);         // поворот
    else if (r <= 90) m = static_cast<uint8_t>(N | E | S);     // тройник
    else              m = 15;                                  // перекрёсток
    return rotateDoors(m, rng.range(0, 3));
}

Deck::Deck(Rng& rng, int bossId, int bossMin, int bossMax) {
    std::vector<Card> pool;
    auto add = [&](RoomType t, int n, int dmin, int dmax) {
        for (int i = 0; i < n; ++i) {
            Card c;
            c.type = t;
            c.difficulty = rng.range(dmin, dmax);
            c.doors = randomDoors(rng);
            pool.push_back(c);
        }
    };
    add(RoomType::Combat, 11, 1, 3);
    add(RoomType::Treasure, 4, 1, 3);
    add(RoomType::Trap, 4, 1, 3);
    add(RoomType::Rest, 4, 1, 1);
    rng.shuffle(pool);

    Card boss;
    boss.type = RoomType::Boss;
    boss.difficulty = 4;
    boss.bossId = bossId >= 0 ? bossId : rng.range(0, kBossCount - 1);
    boss.doors = randomDoors(rng);
    pool.insert(pool.begin() + rng.range(bossMin, bossMax), boss);
    cards_ = std::move(pool);
}

}  // namespace dc
