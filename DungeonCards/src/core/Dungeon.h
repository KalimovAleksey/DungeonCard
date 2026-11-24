#pragma once
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "Card.h"
#include "Rng.h"
#include "Types.h"

namespace dc {

struct Room {
    Card card;
    Vec2i pos;
    bool visited = false;
    bool eventUsed = false;
};

// Подземелье на сетке. Правило размещения («свободное, но с обязательной связью»):
// карту можно поставить в ЛЮБУЮ свободную клетку, если хотя бы одна её дверь стыкуется
// с дверью соседней комнаты, ДОСТИЖИМОЙ ОТ ВХОДА. Односторонние двери считаются стеной.
//
// Торговец ставится автоматически в начале игры у края карты и сначала не подключён:
// он становится частью подземелья, когда поставлена комната, стыкующаяся и с ним, и с сетью.
class Dungeon {
public:
    static constexpr int Width = 9;
    static constexpr int Height = 7;

    enum class PlaceResult { Ok, OutOfBounds, Occupied, NoConnection, ShopOnly };
    using Pred = std::function<bool(const Room&)>;

    Dungeon();

    Vec2i startPos() const { return {Width / 2, Height / 2}; }
    bool inBounds(Vec2i p) const { return p.x >= 0 && p.y >= 0 && p.x < Width && p.y < Height; }

    const Room* roomAt(Vec2i p) const;
    Room* roomAt(Vec2i p);
    const std::map<Vec2i, Room>& rooms() const { return rooms_; }

    PlaceResult check(const Card& c, Vec2i p) const;
    bool place(const Card& c, Vec2i p);

    // Есть ли проход из p в направлении d (двери совпали с обеих сторон).
    bool linked(Vec2i p, Dir d) const;

    // --- торговец ---
    Vec2i placeShop(Rng& rng);            // случайная клетка у края (двойное кольцо), чаще самый край
    bool placeShopAt(Vec2i p);            // поставить торговца в клетку p (для тестов)
    bool hasShop() const { return hasShop_; }
    Vec2i shopPos() const { return shopPos_; }
    bool shopConnected() const { const Room* r = hasShop_ ? roomAt(shopPos_) : nullptr; return r != nullptr && connected(shopPos_); }
    // Достижима ли комната от входа (по стыкующимся дверям).
    bool connected(Vec2i p) const { return reach_.count(p) > 0; }

    bool hasBoss() const { return hasBoss_; }
    Vec2i bossPos() const { return bossPos_; }

    // Кратчайший путь (без стартовой клетки) до ближайшей комнаты, подходящей под pred.
    std::vector<Vec2i> pathToNearest(Vec2i from, const Pred& target, const Pred& passable = nullptr) const;

private:
    void recomputeReach();

    std::map<Vec2i, Room> rooms_;
    std::set<Vec2i> reach_;   // комнаты, достижимые от входа
    bool hasBoss_ = false;
    Vec2i bossPos_{};
    bool hasShop_ = false;
    Vec2i shopPos_{};
};

}  // namespace dc
