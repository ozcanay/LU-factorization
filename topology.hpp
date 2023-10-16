#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "tile.hpp"

// static constexpr std::uint32_t CAPID6 = 0x06bd5757; // bull hft4
static constexpr std::uint32_t CAPID6 = 0xffffffff;  // koc cascade

// koc cascade
static const std::map<int, int> cha_core_map{{0, 0},   {1, 28},  {2, 16},  {3, 44},  {4, 4},   {5, 32},  {6, 20},
                                             {7, 48},  {8, 8},   {9, 36},  {10, 24}, {11, 52}, {12, 12}, {13, 40},
                                             {14, 26}, {15, 54}, {16, 10}, {17, 38}, {18, 22}, {19, 50}, {20, 6},
                                             {21, 34}, {22, 18}, {23, 46}, {24, 2},  {25, 30}, {26, 14}, {27, 42}};

class Topology {
   public:
    explicit Topology(const std::map<int, int>& cha_core_map, std::uint32_t capid6);
    int getHopCost(int requesting_core, int forwarding_cha) const;
    int getHopCost(int requesting_core, int forwarder_core, int coherence_cha) const;
    void printTopology() const;
    Tile getHotspotTile(const std::map<int, int>& cha_count_map);
    Tile getClosestTile(const Tile& tile, const std::vector<Tile>& ignored_tiles = {});
    Tile getClosestTilewithThreshold(const Tile& tile, const std::vector<Tile>& ignored_tiles = {});
    Tile getTile(int cha);
    Tile getTile(int x, int y);
    Tile getTileByCore(int core);

   private:
    std::map<int, int> cha_core_map_;
    std::vector<std::vector<Tile>> tiles_;

    // Tile getTile(int cha);
    // Tile getTile(int x, int y);
};