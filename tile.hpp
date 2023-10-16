#pragma once

inline static constexpr auto UNDEFINED = -1;

enum TileStatus { Enabled, Disabled, Imc, Undefined };

struct Tile {
    int x = UNDEFINED;  // horizontal.
    int y = UNDEFINED;  // vertical.
    int core = UNDEFINED;
    int cha = UNDEFINED;
    TileStatus status = TileStatus::Undefined;

    bool operator<(const Tile& other) const { return (x < other.x) || (x == other.x && y < other.y); }
};