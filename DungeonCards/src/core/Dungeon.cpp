#include "Dungeon.h"
#include <algorithm>
#include <queue>

namespace dc {

Dungeon::Dungeon() {
    Room r;
    r.card.type = RoomType::Start;
    r.card.difficulty = 0;
    r.card.doors = 15;
    r.pos = startPos();
    r.visited = true;
    rooms_[r.pos] = r;
    recomputeReach();
}

void Dungeon::recomputeReach() {
    reach_.clear();
    std::queue<Vec2i> q;
    q.push(startPos());
    reach_.insert(startPos());
    while (!q.empty()) {
        Vec2i cur = q.front();
        q.pop();
        for (int i = 0; i < kDirCount; ++i) {
            Dir d = static_cast<Dir>(i);
            if (!linked(cur, d)) continue;
            Vec2i n = cur + dirDelta(d);
            if (reach_.insert(n).second) q.push(n);
        }
    }
}

bool Dungeon::placeShopAt(Vec2i p) {
    if (!inBounds(p) || rooms_.count(p) || hasShop_) return false;
    Room r;
    r.card.type = RoomType::Shop;
    r.card.difficulty = 0;
    r.card.doors = 0;
    for (int i = 0; i < kDirCount; ++i) {  // двери ко всем соседним клеткам внутри карты
        Dir d = static_cast<Dir>(i);
        if (inBounds(p + dirDelta(d))) r.card.doors |= doorBit(d);
    }
    r.pos = p;
    rooms_[p] = r;
    hasShop_ = true;
    shopPos_ = p;
    recomputeReach();
    return true;
}

Vec2i Dungeon::placeShop(Rng& rng) {
    std::vector<Vec2i> cells;
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            const int edge = std::min(std::min(x, Width - 1 - x), std::min(y, Height - 1 - y));
            if (edge > 1 || rooms_.count({x, y})) continue;
            for (int k = 0; k < (edge == 0 ? 3 : 1); ++k) cells.push_back({x, y});  // самый край втрое вероятнее
        }
    Vec2i p = cells[rng.range(0, static_cast<int>(cells.size()) - 1)];
    placeShopAt(p);
    return p;
}

const Room* Dungeon::roomAt(Vec2i p) const {
    auto it = rooms_.find(p);
    return it == rooms_.end() ? nullptr : &it->second;
}
Room* Dungeon::roomAt(Vec2i p) {
    auto it = rooms_.find(p);
    return it == rooms_.end() ? nullptr : &it->second;
}

Dungeon::PlaceResult Dungeon::check(const Card& c, Vec2i p) const {
    if (!inBounds(p)) return PlaceResult::OutOfBounds;
    if (rooms_.count(p)) return PlaceResult::Occupied;
    bool onlyUnconnected = false;
    for (int i = 0; i < kDirCount; ++i) {
        Dir d = static_cast<Dir>(i);
        if (!hasDoor(c.doors, d)) continue;
        const Room* n = roomAt(p + dirDelta(d));
        if (!n || !hasDoor(n->card.doors, opposite(d))) continue;
        if (connected(n->pos)) return PlaceResult::Ok;
        onlyUnconnected = true;  // дверь стыкуется, но комната ещё не подключена к входу (торговец)
    }
    return onlyUnconnected ? PlaceResult::ShopOnly : PlaceResult::NoConnection;
}

bool Dungeon::place(const Card& c, Vec2i p) {
    if (check(c, p) != PlaceResult::Ok) return false;
    Room r;
    r.card = c;
    r.pos = p;
    rooms_[p] = r;
    if (c.type == RoomType::Boss) {
        hasBoss_ = true;
        bossPos_ = p;
    }
    recomputeReach();
    return true;
}

bool Dungeon::linked(Vec2i p, Dir d) const {
    const Room* a = roomAt(p);
    const Room* b = roomAt(p + dirDelta(d));
    return a && b && hasDoor(a->card.doors, d) && hasDoor(b->card.doors, opposite(d));
}

std::vector<Vec2i> Dungeon::pathToNearest(Vec2i from, const Pred& target, const Pred& passable) const {
    std::map<Vec2i, Vec2i> parent;
    std::queue<Vec2i> q;
    q.push(from);
    parent[from] = from;
    while (!q.empty()) {
        Vec2i cur = q.front();
        q.pop();
        if (cur != from && target(rooms_.at(cur))) {
            std::vector<Vec2i> path;
            for (Vec2i p = cur; p != from; p = parent[p]) path.push_back(p);
            std::reverse(path.begin(), path.end());
            return path;
        }
        // Через непроходимую комнату (например, ещё живого босса) дальше не идём.
        if (cur != from && passable && !passable(rooms_.at(cur))) continue;
        for (int i = 0; i < kDirCount; ++i) {
            Dir d = static_cast<Dir>(i);
            if (!linked(cur, d)) continue;
            Vec2i n = cur + dirDelta(d);
            if (!parent.count(n)) {
                parent[n] = cur;
                q.push(n);
            }
        }
    }
    return {};
}

}  // namespace dc
