#pragma once
#include "Cell.h"
#include "core/HexCoord.h"
#include "simulator/SimConfig.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <set>

// -----------------------------------------------------------------------------
//  WallKey - unordered key for a pair of adjacent cells (order-independent).
// -----------------------------------------------------------------------------
struct WallKey {
    HexCoord a, b;
    WallKey(HexCoord x, HexCoord y) {
        if (y < x) std::swap(x, y);
        a = x; b = y;
    }
    bool operator==(const WallKey& o) const { return a==o.a && b==o.b; }
};
struct WallKeyHash {
    size_t operator()(const WallKey& w) const noexcept {
        HexCoordHash h;
        return h(w.a) ^ (h(w.b) * 2654435761ULL);
    }
};

// -----------------------------------------------------------------------------
//  Grid - the world: a sparse map of HexCoords → Cells, plus pheromone state
//  and inter-cell walls.
//
//  Design notes for extensibility:
//    • Pheromone is stored inside Cell::pheromone (vector, one per species).
//    • Inter-cell walls are stored externally so cells stay simple.
//    • The ant-presence map (coord → AntID) lives here for O(1) lookup.
// -----------------------------------------------------------------------------
class Grid {
public:
    using CellMap = std::unordered_map<HexCoord, Cell, HexCoordHash>;

    explicit Grid(const SimConfig& cfg);

    // -- Cell management ---------------------------------------------------
    Cell*       getCell(HexCoord c);
    const Cell* getCell(HexCoord c) const;
    Cell&       getOrCreateCell(HexCoord c);
    void        setCell(HexCoord c, CellType type);
    bool        hasCell(HexCoord c) const;

    const CellMap& cells() const { return cells_; }

    // -- Inter-cell walls --------------------------------------------------
    /// Prevent ants from moving between cells a and b (and pheromone diffusing).
    void addWallBetween(HexCoord a, HexCoord b);
    bool hasWallBetween(HexCoord a, HexCoord b) const;

    // -- Ant presence -----------------------------------------------------
    void     placeAnt   (HexCoord c, AntID id);
    void     removeAnt  (HexCoord c, AntID id);
    bool     hasAnt     (HexCoord c) const;
    AntID    antAt      (HexCoord c) const; // INVALID_ANT if none
    void     clearAnts  ();   /// Wipe all ant-presence data

    // -- Traversability & reachability -------------------------------------
    /// All traversable, wall-free neighbours of c.
    std::vector<HexCoord> traversableNeighbours(HexCoord c) const;

    /// The (up to 3) cells reachable by an ant at c facing d.
    /// Applies: direction arc filter + cell traversability + inter-cell walls.
    std::vector<HexCoord> reachableCells(HexCoord c, Direction d) const;

    // -- Pheromone helpers -------------------------------------------------
    float getPheromone(HexCoord c, PheromoneID pid = 0) const;
    void  addSecretion(HexCoord c, float amount, PheromoneID pid = 0);

    /// Diffusion average: average of pheromone at c and its non-wall neighbours.
    float diffusionAverage(HexCoord c, PheromoneID pid = 0) const;

    /// Advance pheromone one time-step (call once per simulation step).
    void stepPheromone();

    int numSpecies() const { return cfg_.numPheromoneSpecies; }

private:
    SimConfig cfg_;
    CellMap   cells_;
    std::unordered_set<WallKey, WallKeyHash> interCellWalls_;
    std::unordered_map<HexCoord, AntID, HexCoordHash> antPresence_;
};
