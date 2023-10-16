#include "topology.hpp"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>

static constexpr auto VERTICAL_HOP_CYCLE_COST = 1;
static constexpr auto HORIZONTAL_HOP_CYCLE_COST = 2;

static constexpr auto SKX_CPU_ROW_COUNT = 5;
static constexpr auto SKX_CPU_COL_COUNT = 6;
static constexpr auto SKX_CPU_IMC_COUNT = 2;

static constexpr auto IMC_ROW_INDEX = 1;
static constexpr std::pair<int, int> IMC0_INDEX = {IMC_ROW_INDEX, 0};
static constexpr std::pair<int, int> IMC1_INDEX = {IMC_ROW_INDEX, SKX_CPU_COL_COUNT - 1};

static constexpr auto DISABLED_TILE = 'X';
static const std::string IMC_TILE = "IMC";
static constexpr auto UNKNOWN_TILE = '?';

Topology::Topology(const std::map<int, int> &cha_core_map, std::uint32_t capid6) : cha_core_map_(cha_core_map) {
    // assert(cha_core_map_.size() == logical_core_count); // assuming that every core is co-located with a cha.
    tiles_ = {SKX_CPU_ROW_COUNT, {SKX_CPU_COL_COUNT, Tile()}};

    assert(!tiles_.empty());
    const auto tile_count = tiles_.size() * tiles_.front().size() - SKX_CPU_IMC_COUNT;
    const int reg_size = std::numeric_limits<std::uint32_t>::digits;
    std::bitset<reg_size> binary_form(capid6);

    // column major traversal for getting along easy with capid6 register representation.
    int IMC_OFFSET = 0;
    int cha = 0;
    for (int j = 0; j < tiles_.front().size(); ++j) {
        for (int i = 0; i < tiles_.size(); ++i) {
            const auto register_bit_index = j * (SKX_CPU_COL_COUNT - 1) + i - IMC_OFFSET;
            auto &tile = tiles_[i][j];

            auto tile_index_as_pair = std::make_pair(i, j);
            if (tile_index_as_pair == IMC0_INDEX || tile_index_as_pair == IMC1_INDEX) {
                tile.status = TileStatus::Imc;
                ++IMC_OFFSET;
                continue;
            }

            const auto register_bit = binary_form[register_bit_index];
            tile.status = static_cast<bool>(register_bit) ? TileStatus::Enabled : TileStatus::Disabled;
            tile.x = i;  // x is on vertical axis.
            tile.y = j;  // y is on horizontal axis.

            if (tile.status == TileStatus::Enabled) {
                tile.cha = cha++;
                tile.core = cha_core_map_[tile.cha];
            }
        }
    }

    // TODO: make sure disabled count is same in both halves. assert().
}

int Topology::getHopCost(int requesting_core, int forwarding_cha) const {
    int cost = 0;
    std::pair<int, int> requesting_core_tile{UNDEFINED, UNDEFINED};
    std::pair<int, int> forwarding_cha_tile{UNDEFINED, UNDEFINED};

    for (int i = 0; i < tiles_.size(); ++i) {
        for (int j = 0; j < tiles_.front().size(); ++j) {
            if (requesting_core_tile != std::make_pair(UNDEFINED, UNDEFINED) &&
                forwarding_cha_tile != std::make_pair(UNDEFINED, UNDEFINED)) {
                // std::cout << "Found both cha and core tiles. Breaking!" << std::endl;
                break;
            }

            if (tiles_[i][j].cha == forwarding_cha) {
                forwarding_cha_tile = {i, j};
            }
            if (tiles_[i][j].core == requesting_core) {
                requesting_core_tile = {i, j};
            }
        }
    }

    const int vertical_diff = std::abs(requesting_core_tile.first - forwarding_cha_tile.first);
    const int horizontal_diff = std::abs(requesting_core_tile.second - forwarding_cha_tile.second);

    cost = vertical_diff * VERTICAL_HOP_CYCLE_COST + horizontal_diff * HORIZONTAL_HOP_CYCLE_COST;
    // std::cout << "cha: " << forwarding_cha << ", core: " << requesting_core << ", vertical diff: " << vertical_diff
    // << ", hor diff: " << horizontal_diff <<
    // ", cost: " << cost << std::endl;

    return cost;
}

int Topology::getHopCost(int requesting_core, int forwarder_core, int coherence_cha) const {
    std::pair<int, int> requesting_core_tile{UNDEFINED, UNDEFINED};
    std::pair<int, int> coherence_cha_tile{UNDEFINED, UNDEFINED};
    std::pair<int, int> forwarder_core_tile{UNDEFINED, UNDEFINED};

    for (int i = 0; i < tiles_.size(); ++i) {
        for (int j = 0; j < tiles_.front().size(); ++j) {
            if (requesting_core_tile != std::make_pair(UNDEFINED, UNDEFINED) &&
                coherence_cha_tile != std::make_pair(UNDEFINED, UNDEFINED) &&
                forwarder_core_tile != std::make_pair(UNDEFINED, UNDEFINED)) {
                break;
            }

            if (tiles_[i][j].cha == coherence_cha) {
                coherence_cha_tile = {i, j};
            }
            if (tiles_[i][j].core == requesting_core) {
                requesting_core_tile = {i, j};
            }
            if (tiles_[i][j].core == forwarder_core) {
                forwarder_core_tile = {i, j};
            }
        }
    }

    // requestor to coherence tile.
    int rc_cost = 0;
    {
        const int vertical_diff = std::abs(requesting_core_tile.first - coherence_cha_tile.first);
        const int horizontal_diff = std::abs(requesting_core_tile.second - coherence_cha_tile.second);
        rc_cost = vertical_diff * VERTICAL_HOP_CYCLE_COST + horizontal_diff * HORIZONTAL_HOP_CYCLE_COST;
    }
    std::cout << "R - C cost: " << rc_cost << std::endl;

    // coherence to forwarder tile.
    int cf_cost = 0;
    {
        const int vertical_diff = std::abs(coherence_cha_tile.first - forwarder_core_tile.first);
        const int horizontal_diff = std::abs(coherence_cha_tile.second - forwarder_core_tile.second);
        cf_cost = vertical_diff * VERTICAL_HOP_CYCLE_COST + horizontal_diff * HORIZONTAL_HOP_CYCLE_COST;
    }
    std::cout << "C - F cost: " << cf_cost << std::endl;

    // forwarder to requestor tile.
    int fr_cost = 0;
    {
        const int vertical_diff = std::abs(forwarder_core_tile.first - requesting_core_tile.first);
        const int horizontal_diff = std::abs(forwarder_core_tile.second - requesting_core_tile.second);
        fr_cost = vertical_diff * VERTICAL_HOP_CYCLE_COST + horizontal_diff * HORIZONTAL_HOP_CYCLE_COST;
    }
    std::cout << "F - R cost: " << fr_cost << std::endl;

    const int cost = rc_cost + cf_cost + fr_cost;

    std::cout << "requesting core: " << requesting_core << ", x: " << requesting_core_tile.first
              << ", y: " << requesting_core_tile.second << std::endl;
    std::cout << "coherence cha: " << coherence_cha << ", x: " << coherence_cha_tile.first
              << ", y: " << coherence_cha_tile.second << std::endl;
    std::cout << "forwarder core: " << forwarder_core << ", x: " << forwarder_core_tile.first
              << ", y: " << forwarder_core_tile.second << std::endl;

    return cost;
}

void Topology::printTopology() const {
    assert(!tiles_.empty());

    for (int i = 0; i < tiles_.size(); ++i) {
        std::string text;
        std::ostringstream oss;

        for (int j = 0; j < tiles_.front().size(); ++j) {
            std::string text;
            static const int justify_len = 30;
            auto &tile = tiles_[i][j];

            const auto tile_status = tile.status;
            switch (tile_status) {
                case TileStatus::Enabled:
                    text += "CHA: " + std::to_string(tile.cha) + ", CORE: " +
                            std::to_string(
                                tile.core); /* + ", x: " + std::to_string(tile.x) + ", y: " + std::to_string(tile.y);*/
                    oss << std::setw(justify_len) << text;
                    break;
                case TileStatus::Disabled:
                    text += DISABLED_TILE;
                    oss << std::setw(justify_len) << text;
                    break;
                case TileStatus::Imc:
                    text += IMC_TILE;
                    oss << std::setw(justify_len) << text;
                    break;
                case TileStatus::Undefined:
                    text += UNKNOWN_TILE;
                    oss << std::setw(justify_len) << text;
                    break;
                default:
                    std::cerr << "this should have been unreachable.\n";
                    // SPDLOG_ERROR("This should have been unreachable.");
                    abort();
                    break;
            }
        }

        std::cout << oss.str() << std::endl;
        // SPDLOG_TRACE(oss.str());
    }
}

Tile Topology::getHotspotTile(const std::map<int, int> &cha_count_map)  // this might result in a disabled tile.
{
    assert(!cha_count_map.empty());

    std::vector<std::vector<int>> cha_counts(SKX_CPU_ROW_COUNT, std::vector<int>(SKX_CPU_COL_COUNT, 0));  // weights.
    assert(!cha_counts.empty());
    assert(cha_counts.size() == SKX_CPU_ROW_COUNT);
    assert(cha_counts.front().size() == SKX_CPU_COL_COUNT);

    // first, populate "cha_counts" 2d vector.
    for (const auto &[cha, count] : cha_count_map) {
        const auto tile = getTile(cha);
        std::cout << "found tile for cha: " << cha << ". x: " << tile.x << ", y: " << tile.y << std::endl;
        // SPDLOG_TRACE("Found tile for cha {}. x: {}, y: {}", cha, tile.x, tile.y);

        cha_counts[tile.x][tile.y] = count;
    }

    for (int i = 0; i < cha_counts.size(); ++i) {
        for (int j = 0; j < cha_counts.front().size(); ++j) {
            std::cout << "cha access counts. tile x: " << i << ", y: " << j << " --> count: " << cha_counts[i][j]
                      << std::endl;
            // SPDLOG_TRACE("cha access counts tile x: {}, y: {} --> count: {}", i, j, cha_counts[i][j]);
        }
    }

    Tile hotspot_tile;
    double total_cha_count = 0;
    for (int i = 0; i < cha_counts.size(); ++i) {
        for (int j = 0; j < cha_counts.front().size(); ++j) {
            const auto cha_count = cha_counts[i][j];

            hotspot_tile.x += (cha_count * i);
            hotspot_tile.y += (cha_count * j);

            total_cha_count += cha_count;
        }
    }

    // SPDLOG_TRACE("SUM hotspot_tile.x: {}, SUM hotspot_tile.Y: {}, total_cha_count: {}", hotspot_tile.x,
    // hotspot_tile.y,
    //              total_cha_count);

    hotspot_tile.x = std::round(hotspot_tile.x / total_cha_count);
    hotspot_tile.y =
        std::round(hotspot_tile.y / total_cha_count);  // here I am losing worthy precision info. think about this.
    hotspot_tile.cha = getTile(hotspot_tile.x, hotspot_tile.y).cha;

    // SPDLOG_TRACE("Hotspot tile x: {} y: {}, cha: {}", hotspot_tile.x, hotspot_tile.y, hotspot_tile.cha);

    return hotspot_tile;
}

Tile Topology::getTile(int cha) {
    // place some assertion here? cha-range should be enforced.

    for (int i = 0; i < tiles_.size(); ++i) {
        for (int j = 0; j < tiles_.front().size(); ++j) {
            const auto tile_ = tiles_[i][j];
            // fprintf(stderr, "tile_.cha: %d, cha: %d\n", tile_.cha, cha);
            if (tile_.cha == cha) {
                // fprintf(stderr, "print here 1, tiles_.size(): %d, tiles_.front().size(): %d, tile_.cha: %d\n",
                // tiles_.size(), tiles_.front().size(), tile_.cha);
                return tile_;
                break;
            }
        }
    }
    // fprintf(stderr, "print here 2, tiles_.size(): %d, tiles_.front().size(): %d\n", tiles_.size(),
    // tiles_.front().size());
    return {};
}

Tile Topology::getTileByCore(int core) {
    // place some assertion here? cha-range should be enforced.

    for (int i = 0; i < tiles_.size(); ++i) {
        for (int j = 0; j < tiles_.front().size(); ++j) {
            const auto tile_ = tiles_[i][j];
            // fprintf(stderr, "tile_.cha: %d, cha: %d\n", tile_.cha, cha);
            if (tile_.core == core) {
                // fprintf(stderr, "print here 1, tiles_.size(): %d, tiles_.front().size(): %d, tile_.cha: %d\n",
                // tiles_.size(), tiles_.front().size(), tile_.cha);
                return tile_;
                break;
            }
        }
    }
    // fprintf(stderr, "print here 2, tiles_.size(): %d, tiles_.front().size(): %d\n", tiles_.size(),
    // tiles_.front().size());
    return {};
}

Tile Topology::getTile(int x, int y) {
    for (int i = 0; i < tiles_.size(); ++i) {
        for (int j = 0; j < tiles_.front().size(); ++j) {
            const auto tile_ = tiles_[i][j];

            if (tile_.x == x && tile_.y == y) {
                return tile_;
                break;
            }
        }
    }

    return {};
}

Tile Topology::getClosestTilewithThreshold(
    const Tile &tile,
    const std::vector<Tile>
        &ignored_tiles)  // keep in mind that vertical hops are less costly and x signifies vertical axis.
{
    const auto src_x = tile.x;
    const auto src_y = tile.y;

    // SPDLOG_TRACE("Trying to find the closest available tile to tile x: {}, y: {}", tile.x, tile.y);

    Tile res;

    // SPDLOG_TRACE("src_x: {}", src_x);
    // SPDLOG_TRACE("src_y: {}", src_y);
    // SPDLOG_TRACE("cha: {}", tile.cha);
    // SPDLOG_TRACE("ignored tiles size: {}", ignored_tiles.size());
    for (const auto &ignored_tile : ignored_tiles) {
        // SPDLOG_TRACE("Ignored tile x: {}, y: {}, cha: {}", ignored_tile.x, ignored_tile.y, ignored_tile.cha);
    }

    const auto candidate_tile = getTile(src_x, src_y);
    const auto ignored_tiles_it =
        std::find_if(ignored_tiles.begin(), ignored_tiles.end(), [&candidate_tile](const Tile &ignored_tile) {
            return ignored_tile.x == candidate_tile.x && ignored_tile.y == candidate_tile.y;
        });

    if (ignored_tiles_it != ignored_tiles.end()) {
        // SPDLOG_TRACE("Ignored tile!");
    }

    else {
        if (candidate_tile.cha != UNDEFINED) {
            res = candidate_tile;
            // SPDLOG_TRACE("early return res tile. x: {}, y: {}, cha: {}", res.x, res.y, res.cha);
            return res;  // early return without walking.
        }
    }

    const std::vector<std::pair<int, int>> dirs{{1, 0},  {-1, 0}, {0, 1},  {0, -1}, {2, 0},
                                                {-2, 0}, {1, 1},  {1, -1}, {-1, 1}, {-1, -1}};

    std::vector<std::vector<int>> visited(SKX_CPU_ROW_COUNT, std::vector<int>(SKX_CPU_COL_COUNT, 0));
    visited[src_x][src_y] = 1;

    for (const auto &dir : dirs) {
        const auto next_x = src_x + dir.first;
        const auto next_y = src_y + dir.second;

        if (next_x >= 0 && next_x < SKX_CPU_ROW_COUNT && next_y >= 0 && next_y < SKX_CPU_COL_COUNT &&
            !visited[next_x][next_y]) {
            visited[next_x][next_y] = 1;
            // q.push({next_x, next_y});
            // SPDLOG_TRACE("Pushing into queue. next_x: {}, next_y: {}", next_x, next_y);

            const auto candidate_tile = getTile(next_x, next_y);
            const auto ignored_tiles_it = std::find_if(
                ignored_tiles.begin(), ignored_tiles.end(),
                [&candidate_tile](const Tile &ignored_tile) {  // fprintf(stderr, "it is an ignored tile\n");
                    return ignored_tile.x == candidate_tile.x && ignored_tile.y == candidate_tile.y;
                });

            if (ignored_tiles_it != ignored_tiles.end()) {
                // SPDLOG_TRACE("Ignored tile!");
            } else {
                if (candidate_tile.cha != UNDEFINED) {
                    res = candidate_tile;
                    // SPDLOG_TRACE("return res tile. x: {}, y: {}, cha: {}", res.x, res.y, res.cha);
                    // fprintf(stderr, "return res tile in getClosestTile 1. x: %d, y: %d, cha: %d\n", res.x, res.y,
                    // res.cha);
                    return res;  // early return without walking.
                }
            }
        }
    }
    // SPDLOG_TRACE("BROKE OUT.");
    // SPDLOG_INFO("BROKE OUT.");
    // fprintf(stderr, "return res tile in getClosestTile 2. x: %d, y: %d, cha: %d\n", res.x, res.y, res.cha);
    return res;
}

Tile Topology::getClosestTile(const Tile &tile,
                              const std::vector<Tile> &ignored_tiles)  // keep in mind that vertical hops are less
                                                                       // costly and x signifies vertical axis.
{
    const auto src_x = tile.x;
    const auto src_y = tile.y;

    // SPDLOG_TRACE("Trying to find the closest available tile to tile x: {}, y: {}", tile.x, tile.y);

    Tile res;

    // SPDLOG_TRACE("src_x: {}", src_x);
    // SPDLOG_TRACE("src_y: {}", src_y);
    // SPDLOG_TRACE("cha: {}", tile.cha);
    // SPDLOG_TRACE("ignored tiles size: {}", ignored_tiles.size());
    for (const auto &ignored_tile : ignored_tiles) {
        // SPDLOG_TRACE("Ignored tile x: {}, y: {}, cha: {}", ignored_tile.x, ignored_tile.y, ignored_tile.cha);
    }

    const auto candidate_tile = getTile(src_x, src_y);
    const auto ignored_tiles_it =
        std::find_if(ignored_tiles.begin(), ignored_tiles.end(), [&candidate_tile](const Tile &ignored_tile) {
            return ignored_tile.x == candidate_tile.x && ignored_tile.y == candidate_tile.y;
        });

    if (ignored_tiles_it != ignored_tiles.end()) {
        // SPDLOG_TRACE("Ignored tile!");
    } else {
        if (candidate_tile.cha != UNDEFINED) {
            res = candidate_tile;
            // SPDLOG_TRACE("early return res tile. x: {}, y: {}, cha: {}", res.x, res.y, res.cha);
            return res;  // early return without walking.
        }
    }

    const std::vector<std::pair<int, int>> dirs{{1, 0},  {-1, 0}, {0, 1},  {0, -1}, {2, 0},
                                                {-2, 0}, {1, 1},  {1, -1}, {-1, 1}, {-1, -1}};

    std::vector<std::vector<int>> visited(SKX_CPU_ROW_COUNT, std::vector<int>(SKX_CPU_COL_COUNT, 0));
    visited[src_x][src_y] = 1;

    std::queue<std::pair<int, int>> q;
    q.push({tile.x, tile.y});

    int iteration_upper_bound = 28;
    while (iteration_upper_bound-- > 0) {
        const auto size = q.size();

        for (int i = 0; i < size; ++i) {
            const auto cur = q.front();
            q.pop();

            for (const auto &dir : dirs) {
                const auto next_x = cur.first + dir.first;
                const auto next_y = cur.second + dir.second;

                if (next_x >= 0 && next_x < SKX_CPU_ROW_COUNT && next_y >= 0 && next_y < SKX_CPU_COL_COUNT &&
                    !visited[next_x][next_y]) {
                    visited[next_x][next_y] = 1;
                    q.push({next_x, next_y});
                    // SPDLOG_TRACE("Pushing into queue. next_x: {}, next_y: {}", next_x, next_y);

                    const auto candidate_tile = getTile(next_x, next_y);
                    const auto ignored_tiles_it = std::find_if(
                        ignored_tiles.begin(), ignored_tiles.end(), [&candidate_tile](const Tile &ignored_tile) {
                            // fprintf(stderr, "it is an ignored tile\n");
                            return ignored_tile.x == candidate_tile.x && ignored_tile.y == candidate_tile.y;
                        });

                    if (ignored_tiles_it != ignored_tiles.end()) {
                        // SPDLOG_TRACE("Ignored tile!");
                    } else {
                        if (candidate_tile.cha != UNDEFINED) {
                            res = candidate_tile;
                            // SPDLOG_TRACE("return res tile. x: {}, y: {}, cha: {}", res.x, res.y, res.cha);
                            // fprintf(stderr, "return res tile in getClosestTile 1. x: %d, y: %d, cha: %d\n", res.x,
                            // res.y, res.cha);
                            return res;  // early return without walking.
                        }
                    }
                }
            }
        }
    }

    // SPDLOG_TRACE("BROKE OUT.");
    // fprintf(stderr, "return res tile in getClosestTile 2. x: %d, y: %d, cha: %d\n", res.x, res.y, res.cha);
    return res;
}