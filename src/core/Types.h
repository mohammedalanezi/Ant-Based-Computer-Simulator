#pragma once
#include <cstdint>
#include <string>

// -----------------------------------------------------------------------------
//  Direction - flat-top hexagonal grid, 6 compass directions (CW from East)
//
//  Neighbor axial offsets (q, r):
//    E  (+1, 0)   NE (+1,-1)   NW ( 0,-1)
//    W  (-1, 0)   SW (-1,+1)   SE ( 0,+1)
//
//  Reachable from direction D: (D-1)%6, D, (D+1)%6
// -----------------------------------------------------------------------------
enum class Direction : int8_t {
    E  = 0,
    NE = 1,
    NW = 2,
    W  = 3,
    SW = 4,
    SE = 5,
    NONE = -1
};

inline constexpr int NUM_DIRECTIONS = 6;
inline constexpr int MAX_COLOURS    = 4;

// Axial offsets for each direction (flat-top hex)
inline constexpr int DIR_DQ[6] = { +1, +1,  0, -1, -1,  0 };
inline constexpr int DIR_DR[6] = {  0, -1, -1,  0, +1, +1 };

inline Direction dirFromInt(int d) {
    return static_cast<Direction>(((d % 6) + 6) % 6);
}
inline int dirToInt(Direction d) { return static_cast<int>(d); }
inline Direction dirCW(Direction d)  { return dirFromInt(dirToInt(d) + 1); }
inline Direction dirCCW(Direction d) { return dirFromInt(dirToInt(d) - 1); }
inline Direction dirOpp(Direction d) { return dirFromInt(dirToInt(d) + 3); }

// -----------------------------------------------------------------------------
//  Cell types
// -----------------------------------------------------------------------------
enum class CellType : uint8_t {
    VOID    = 0,   // does not exist; border of the world
    WALL    = 1,   // solid; ants cannot enter; pheromone does not diffuse through
    PATH    = 2,   // normal traversable path
    SOURCE  = 3,   // spawns ants each tick (with probability p_spawn)
    SINK    = 4,   // removes ants that enter
    PUMP    = 5,   // releases pheromone at a fixed rate
    SWITCH  = 6,   // controllable source (can be toggled on/off)
    PROBE   = 7,   // like PATH but records ant-flow running average
    BRIDGE  = 8,   // special: allows two crossing paths (uses inter-cell walls)
};

inline bool isTravellable(CellType t) {
    return t == CellType::PATH   || t == CellType::SOURCE ||
           t == CellType::SINK   || t == CellType::PUMP   ||
           t == CellType::SWITCH || t == CellType::PROBE  ||
           t == CellType::BRIDGE;
}

inline bool allowsPheromone(CellType t) {
    return t != CellType::VOID && t != CellType::WALL;
}

// -----------------------------------------------------------------------------
//  Identifiers
// -----------------------------------------------------------------------------
using AntID       = uint64_t;
using PheromoneID = int;   // extensible: multiple pheromone species

static constexpr AntID INVALID_ANT = 0;
static constexpr PheromoneID DEFAULT_PHEROMONE = 0;
