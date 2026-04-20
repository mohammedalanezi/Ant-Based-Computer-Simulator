#include "simulator/Grid.h"
#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------------
Grid::Grid(const SimConfig& cfg) : cfg_(cfg) {}

// -- Cell management -----------------------------------------------------------
Cell* Grid::getCell(HexCoord c) {
    auto it = cells_.find(c);
    return it != cells_.end() ? &it->second : nullptr;
}
const Cell* Grid::getCell(HexCoord c) const {
    auto it = cells_.find(c);
    return it != cells_.end() ? &it->second : nullptr;
}
Cell& Grid::getOrCreateCell(HexCoord c) {
    auto [it, inserted] = cells_.emplace(c, Cell{});
    if (inserted) it->second.init(cfg_.numPheromoneSpecies);
    return it->second;
}
void Grid::setCell(HexCoord c, CellType type) {
    Cell& cell = getOrCreateCell(c);
    cell.type = type;
}
bool Grid::hasCell(HexCoord c) const {
    auto it = cells_.find(c);
    return it != cells_.end() && it->second.type != CellType::VOID;
}

// -- Inter-cell walls ----------------------------------------------------------
void Grid::addWallBetween(HexCoord a, HexCoord b) {
    interCellWalls_.insert(WallKey(a, b));
}
bool Grid::hasWallBetween(HexCoord a, HexCoord b) const {
    return interCellWalls_.count(WallKey(a, b)) > 0;
}

// -- Ant presence --------------------------------------------------------------
void Grid::placeAnt(HexCoord c, AntID id) {
    antPresence_[c] = id;
    if (Cell* cell = getCell(c)) cell->antCount++;
}
void Grid::removeAnt(HexCoord c, AntID id) {
    auto it = antPresence_.find(c);
    if (it != antPresence_.end() && it->second == id) {
        antPresence_.erase(it);
        if (Cell* cell = getCell(c)) {
            cell->antCount = std::max(0, cell->antCount - 1);
        }
    }
}
bool Grid::hasAnt(HexCoord c) const {
    return antPresence_.count(c) > 0;
}
AntID Grid::antAt(HexCoord c) const {
    auto it = antPresence_.find(c);
    return it != antPresence_.end() ? it->second : INVALID_ANT;
}

void Grid::clearAnts() {
    antPresence_.clear();
    for (auto& [coord, cell] : cells_) {
        cell.antCount    = 0;
        cell.antFlowAvg  = 0.0f;
        cell.antFlowWindow = 0;
    }
}

// -- Traversability ------------------------------------------------------------
std::vector<HexCoord> Grid::traversableNeighbours(HexCoord c) const {
    std::vector<HexCoord> result;
    result.reserve(6);
    for (int d = 0; d < 6; ++d) {
        HexCoord nb{ c.q + DIR_DQ[d], c.r + DIR_DR[d] };
        if (hasWallBetween(c, nb)) continue;
        const Cell* cell = getCell(nb);
        if (cell && isTravellable(cell->type))
            result.push_back(nb);
    }
    return result;
}

std::vector<HexCoord> Grid::reachableCells(HexCoord c, Direction d) const {
    const Cell* cell = getCell(c);
    if (!cell || !isTravellable(cell->type))
        return {};

    // Special case: BRIDGE cells allow movement only straight ahead.
    if (cell->type == CellType::BRIDGE) {
        HexCoord nb = c.neighbor(d);
        if (hasWallBetween(c, nb)) return {};
        const Cell* nbCell = getCell(nb);
        if (nbCell && isTravellable(nbCell->type))
            return {nb};
        return {};
    }
    
    std::vector<HexCoord> result;
    result.reserve(3);
    auto arc = HexCoord::frontArc(d);   // {left, straight, right}
    for (Direction fd : arc) {
        HexCoord nb = c.neighbor(fd);
        if (hasWallBetween(c, nb)) continue;
        const Cell* cell = getCell(nb);
        if (cell && isTravellable(cell->type))
            result.push_back(nb);
    }
    return result;
}

// -- Pheromone helpers ---------------------------------------------------------
float Grid::getPheromone(HexCoord c, PheromoneID pid) const {
    const Cell* cell = getCell(c);
    return cell ? cell->getPheromone(pid) : 0.0f;
}
void Grid::addSecretion(HexCoord c, float amount, PheromoneID pid) {
    if (Cell* cell = getCell(c)) cell->addSecretion(amount, pid);
}

float Grid::diffusionAverage(HexCoord c, PheromoneID pid) const {
    const Cell* cc = getCell(c);
    if (!cc || !allowsPheromone(cc->type)) return 0.0f;

    float sum = cc->getPheromone(pid);
    int   cnt = 1;
    for (int d = 0; d < 6; ++d) {
        HexCoord nb{ c.q + DIR_DQ[d], c.r + DIR_DR[d] };
        if (hasWallBetween(c, nb)) continue;
        const Cell* nc = getCell(nb);
        if (nc && allowsPheromone(nc->type)) {
            sum += nc->getPheromone(pid);
            ++cnt;
        }
    }
    return sum / static_cast<float>(cnt);
}

// -- Pheromone step ------------------------------------------------------------
// Implements: P^t(L) = (1−d)·[(1−f)·P^{t-1}(L) + f·W^{t-1}(L)] + s^{t-1}(L) + p(L)
//
// We must read ALL old values before writing any new value, so we snapshot
// diffusion averages first.
void Grid::stepPheromone() {
    const float d = static_cast<float>(cfg_.dissipationRate);
    const float f = static_cast<float>(cfg_.diffusionRate);

    int ns = cfg_.numPheromoneSpecies;

    // Step 1: snapshot diffusion averages (W) for every pheromone-capable cell
    struct SnapEntry { HexCoord coord; std::vector<float> W; };
    std::vector<SnapEntry> snap;
    snap.reserve(cells_.size());
    for (auto& [coord, cell] : cells_) {
        if (!allowsPheromone(cell.type)) continue;
        SnapEntry e;
        e.coord = coord;
        e.W.resize(ns);
        for (int pid = 0; pid < ns; ++pid)
            e.W[pid] = diffusionAverage(coord, pid);
        snap.push_back(std::move(e));
    }

    // Step 2: apply update formula
    for (auto& e : snap) {
        Cell* cell = getCell(e.coord);
        if (!cell) continue;
        for (int pid = 0; pid < ns; ++pid) {
            float P   = cell->pheromone[pid];
            float W   = e.W[pid];
            float s   = cell->secretionAccum[pid];
            float p   = cell->pumpRate[pid];
            float Pnew = (1.0f - d) * ((1.0f - f) * P + f * W) + s + p;
            if (Pnew < 0.0f) Pnew = 0.0f;
            cell->pheromone[pid] = Pnew;
        }
        cell->clearSecretions();
    }
}