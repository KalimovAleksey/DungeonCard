#pragma once
#include <cstdint>

namespace dc {

struct Vec2i {
    int x = 0, y = 0;
    bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2i& o) const { return !(*this == o); }
    bool operator<(const Vec2i& o) const { return y != o.y ? y < o.y : x < o.x; }
    Vec2i operator+(const Vec2i& o) const { return {x + o.x, y + o.y}; }
};

enum Dir : int { North = 0, East = 1, South = 2, West = 3 };
constexpr int kDirCount = 4;

constexpr uint8_t doorBit(Dir d) { return static_cast<uint8_t>(1u << d); }
inline bool hasDoor(uint8_t mask, Dir d) { return (mask & doorBit(d)) != 0; }
inline Dir opposite(Dir d) { return static_cast<Dir>((d + 2) % 4); }

inline Vec2i dirDelta(Dir d) {
    switch (d) {
        case North: return {0, -1};
        case East:  return {1, 0};
        case South: return {0, 1};
        default:    return {-1, 0};
    }
}

// Поворот набора дверей по часовой стрелке (N -> E -> S -> W -> N).
inline uint8_t rotateDoors(uint8_t mask, int quarterTurns) {
    int q = ((quarterTurns % 4) + 4) % 4;
    uint8_t out = 0;
    for (int i = 0; i < kDirCount; ++i)
        if (mask & (1u << i)) out |= static_cast<uint8_t>(1u << ((i + q) % 4));
    return out;
}

enum class RoomType { Start, Combat, Treasure, Trap, Rest, Boss, Shop, Forge, Event };

}  // namespace dc
