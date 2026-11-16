#pragma once
#include <string>
#include <vector>
#include "Rng.h"
#include "Types.h"

namespace dc {

// Карта комнаты: тип, сложность (1..3, у босса 4) и набор дверей.
struct Card {
    RoomType type = RoomType::Combat;
    int difficulty = 1;
    uint8_t doors = 0;
    int bossId = -1;  // для карты босса: какой именно босс
    void rotate() { doors = rotateDoors(doors, 1); }
};

const char* roomTypeName(RoomType t);
const char* roomTypeHint(RoomType t);
const char* roomTypeKey(RoomType t);   // ключ для картинок: rooms/<ключ>.png

// Колода: фиксированный состав, босс гарантированно выпадает в первых ходах.
class Deck {
public:
    Deck() = default;
    explicit Deck(Rng& rng, int bossId = -1, int bossIndexMin = 4, int bossIndexMax = 9);
    bool empty() const { return next_ >= cards_.size(); }
    int remaining() const { return static_cast<int>(cards_.size() - next_); }
    Card draw() { return cards_[next_++]; }

private:
    std::vector<Card> cards_;
    size_t next_ = 0;
};

}  // namespace dc
