#include "simulator/Simulator.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <list>
#include <iostream>

// -----------------------------------------------------------------------------
Simulator::Simulator(SimConfig config)
    : config_(std::move(config))
    , grid_(config_)
    , rng_(std::random_device{}())
{}

// -- Setup ---------------------------------------------------------------------
void Simulator::addProbe(const std::string& label, HexCoord coord) {
    ProbeRecord rec;
    rec.label = label;
    rec.coord = coord;
    probes_[label] = std::move(rec);
}

void Simulator::addSwitch(const std::string& label, HexCoord coord) {
    std::cout << "addSwitch: " << label << " -> (" << coord.q << "," << coord.r << ")\n";
    switches_[label] = coord;
}

AntID Simulator::spawnAnt(HexCoord coord, Direction dir, int colour) {
    Cell* cell = grid_.getCell(coord);
    if (!cell || !cell->isTraversable()) return INVALID_ANT;
    if (grid_.hasAnt(coord)) return INVALID_ANT;

    Ant ant;
    ant.id        = nextAntId_++;
    ant.location  = coord;
    ant.direction = dir;
    ant.colour    = colour;
    ants_.push_back(ant);
    grid_.placeAnt(coord, ant.id);
    return ant.id;
}

// -- Switch control ------------------------------------------------------------
void Simulator::setSwitchActive(const std::string& label, bool active) {
    auto it = switches_.find(label);
    if (it == switches_.end()) return;
    Cell* cell = grid_.getCell(it->second);
    std::cout << "setSwitchActive(" << label << ", " << active
        << ") -> cell at (" << it->second.q << "," << it->second.r
        << ") ptr=" << cell << "\n";
    if (cell) cell->switchActive = active;
}
bool Simulator::getSwitchActive(const std::string& label) const {
    auto it = switches_.find(label);
    if (it == switches_.end()) return false;
    const Cell* cell = grid_.getCell(it->second);
    return cell && cell->switchActive;
}

// -- Random helpers ------------------------------------------------------------
double Simulator::randUniform() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}
int Simulator::randInt(int n) {
    if (n <= 0) return 0;
    return std::uniform_int_distribution<int>(0, n-1)(rng_);
}

// -- Reset ---------------------------------------------------------------------
void Simulator::reset() {
    ants_.clear();
    claims_.clear();
    grid_.clearAnts();
    nextAntId_ = 1;
    timeStep_ = 0;
    // Clear pheromone and ant counts from all cells
    for (auto& [coord, cell] : grid_.cells()) {
        // const_cast is safe since we own the grid
        const_cast<Cell&>(cell).pheromone.assign(cell.pheromone.size(), 0.0f);
        const_cast<Cell&>(cell).secretionAccum.assign(cell.secretionAccum.size(), 0.0f);
    }
    for (auto& [label, rec] : probes_) {
        rec.antFlowHistory.clear();
        rec.pheromoneHistory.clear();
        rec.currentAntFlow   = 0.0f;
        rec.currentPheromone = 0.0f;
    }
}

// -- Main step -----------------------------------------------------------------
void Simulator::step() {
    if (preStep_) preStep_(*this);

    substepSpawn();
    substepChooseAndSecrete();
    substepCollisionHandle();
    substepRemoveSink();
    substepUpdatePheromone();  // uses secretions accumulated this step
    substepUpdateProbes();

    ++timeStep_;
    if (postStep_) postStep_(*this);
}

// -----------------------------------------------------------------------------
//  substepSpawn
//  Each SOURCE or active SWITCH spawns one ant per step with probability p.
// -----------------------------------------------------------------------------
void Simulator::substepSpawn() {
    for (auto& [coord, cell] : grid_.cells()) {
        if (!cell.isActive()) continue;
        if (randUniform() >= config_.sourceSpawnProbability) continue;
        if (randUniform() >= cell.spawnProbability) continue;
        if (grid_.hasAnt(coord)) continue;  // cell already occupied
        spawnAnt(coord, cell.spawnDirection, cell.colour);
    }
}

// -----------------------------------------------------------------------------
//  CHOOSEACTION - steps 1-4 for each ant.
//  Step 5 (actual movement) is deferred to COLLISIONHANDLING.
// -----------------------------------------------------------------------------
void Simulator::chooseAction(Ant& ant) {
    if (!ant.alive) return;

    // Step 1: identify reachable locations
    std::vector<HexCoord> R = grid_.reachableCells(ant.location, ant.direction);

    // If no reachable cells, ant stays (claimed = current location)
    if (R.empty()) {
        ant.claimed  = ant.location;
        ant.hasClaim = true;
        return;
    }

    // Step 2: sense pheromone at current location and reachable locations
    float Pc = grid_.getPheromone(ant.location);

    // Step 3: conditional secretion
    //   if P(LC) >= T + ε  AND  exists L in R s.t. P(L) <= T - ε
    double T   = config_.pheromoneThreshold;
    double eps = config_.thresholdEpsilon;
    if (Pc >= T + eps) {
        bool hasLow = false;
        for (HexCoord nb : R)
            if (grid_.getPheromone(nb) <= T - eps) { hasLow = true; break; }
        if (hasLow)
            grid_.addSecretion(ant.location, static_cast<float>(config_.secretionAmount));
    }

    // Step 4: choose LN with probability P(LN)^n / Σ P(L)^n
    double n = config_.nonlinearityExponent;
    std::vector<double> weights(R.size());
    double total = 0.0;
    for (size_t i = 0; i < R.size(); ++i) {
        double ph = grid_.getPheromone(R[i]);
        double w  = (ph > 0.0) ? std::pow(ph, n) : 1e-30;
        weights[i] = w;
        total      += w;
    }
    // If total is effectively 0, choose uniformly
    if (total <= 0.0) total = R.size();

    double r = randUniform() * total;
    size_t chosen = R.size() - 1;
    double accum = 0.0;
    for (size_t i = 0; i < R.size(); ++i) {
        accum += weights[i];
        if (r < accum) { chosen = i; break; }
    }

    ant.claimed  = R[chosen];
    ant.hasClaim = true;
}

void Simulator::substepChooseAndSecrete() {
    claims_.clear();
    for (size_t i = 0; i < ants_.size(); ++i) {
        Ant& ant = ants_[i];
        if (!ant.alive) continue;
        chooseAction(ant);
        if (ant.hasClaim)
            claims_[ant.claimed].push_back(i);
    }
}

// -----------------------------------------------------------------------------
//  COLLISIONHANDLING - Figure 4 of Michael & Yiannakides (2012).
//
//  List E holds locations that are currently EMPTY (in new state SN) AND
//  claimed by at least one ant.  We process E until empty, moving one ant
//  at a time into its claimed (empty) cell and freeing the source cell.
//
//  Theorem 1 guarantees: termination, safety (no two ants same cell),
//  liveness (every claimed empty cell eventually occupied).
// -----------------------------------------------------------------------------
void Simulator::substepCollisionHandle() {
    // Build a set of cells occupied in the NEW state (starts = current state)
    // We track "occupied in SN" via a bool in a hash-map for O(1) check.
    std::unordered_map<HexCoord, bool, HexCoordHash> occupiedSN;
    for (const Ant& a : ants_)
        if (a.alive) occupiedSN[a.location] = true;

    // E = locations that are empty in SN AND claimed
    // Use a list so we can efficiently remove from middle
    std::list<HexCoord> E;
    for (auto& [loc, idxs] : claims_) {
        if (!idxs.empty() && occupiedSN.find(loc) == occupiedSN.end())
            E.push_back(loc);
    }

    // Also build index: AntID -> index in ants_
    std::unordered_map<HexCoord, size_t, HexCoordHash> locToIdx;
    for (size_t i = 0; i < ants_.size(); ++i)
        if (ants_[i].alive) locToIdx[ants_[i].location] = i;

    while (!E.empty()) {
        HexCoord LN = E.front();
        E.pop_front();

        auto& claimants = claims_[LN];
        if (claimants.empty()) continue;

        // Choose one ant randomly from those claiming LN
        int pick = randInt(static_cast<int>(claimants.size()));
        size_t antIdx = claimants[pick];
        Ant& ant = ants_[antIdx];

        HexCoord LC = ant.location;

        // Move ant: LC -> LN in SN
        grid_.removeAnt(LC, ant.id);
        grid_.placeAnt(LN, ant.id);

        // Update ant's direction (defined by vector LC->LN)
        auto newDir = LC.dirTo(LN);
        if (newDir.has_value()) ant.direction = *newDir;
        ant.location = LN;
        locToIdx[LN] = antIdx;
        occupiedSN[LN] = true;
        occupiedSN.erase(LC);
        locToIdx.erase(LC);

        // Remove LN from all claims lists (it is now occupied)
        claims_.erase(LN);

        // If LC is now claimed by other ants, add LC to E
        auto sit = claims_.find(LC);
        if (sit != claims_.end() && !sit->second.empty())
            E.push_back(LC);
    }
}

// -----------------------------------------------------------------------------
//  substepRemoveSink - ants that are on a SINK cell are removed.
// -----------------------------------------------------------------------------
void Simulator::substepRemoveSink() {
    for (Ant& ant : ants_) {
        if (!ant.alive) continue;
        const Cell* cell = grid_.getCell(ant.location);
        if (cell && cell->type == CellType::SINK) {
            grid_.removeAnt(ant.location, ant.id);
            ant.alive = false;
        }
    }
    // Compact dead ants periodically
    if (ants_.size() > 500) {
        ants_.erase(std::remove_if(ants_.begin(), ants_.end(),
            [](const Ant& a){ return !a.alive; }), ants_.end());
    }
}

// -----------------------------------------------------------------------------
//  substepUpdatePheromone - delegate to Grid.
// -----------------------------------------------------------------------------
void Simulator::substepUpdatePheromone() {
    grid_.stepPheromone();
}

// -----------------------------------------------------------------------------
//  substepUpdateProbes - record running averages in each probe cell.
// -----------------------------------------------------------------------------
void Simulator::substepUpdateProbes() {
    int maxHist = config_.probeHistoryLength;
    for (auto& [label, rec] : probes_) {
        const Cell* cell = grid_.getCell(rec.coord);
        float antFlow   = cell ? (cell->antCount > 0 ? 1.0f : 0.0f) : 0.0f;
        float pheromone = cell ? cell->getPheromone(DEFAULT_PHEROMONE) : 0.0f;

        // Running average (exponential moving average with 1/window weight)
        float alpha = 1.0f / std::max(config_.probeWindowLength, 1);
        rec.currentAntFlow   = rec.currentAntFlow * (1.0f - alpha) + antFlow * alpha;
        rec.currentPheromone = pheromone;

        rec.antFlowHistory.push_back(rec.currentAntFlow);
        rec.pheromoneHistory.push_back(rec.currentPheromone);
        if ((int)rec.antFlowHistory.size()   > maxHist) rec.antFlowHistory.pop_front();
        if ((int)rec.pheromoneHistory.size() > maxHist) rec.pheromoneHistory.pop_front();
    }
}
