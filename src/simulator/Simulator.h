#pragma once
#include "Grid.h"
#include "Ant.h"
#include "SimConfig.h"
#include <vector>
#include <unordered_map>
#include <deque>
#include <string>
#include <functional>
#include <random>
#include <memory>

// -----------------------------------------------------------------------------
//  ProbeRecord - running measurement stored per probe cell.
// -----------------------------------------------------------------------------
struct ProbeRecord {
    std::string  label;
    HexCoord     coord;
    std::deque<float> antFlowHistory;   // running-avg ant flow per step
    std::deque<float> pheromoneHistory; // pheromone concentration per step
    float currentAntFlow    = 0.0f;
    float currentPheromone  = 0.0f;
};

// -----------------------------------------------------------------------------
//  Simulator - orchestrates the full simulation loop.
//
//  The algorithm faithfully implements:
//    • CHOOSEACTION  (Michael 2009, Figure 1 / Michael & Yiannakides 2012, Fig 1)
//    • COLLISIONHANDLING (Michael & Yiannakides 2012, Figure 4)
//    • Pheromone update equation
//
//  Extensibility:
//    • step() and substep methods are virtual-friendly
//    • Hooks (pre/post step callbacks) provided for future GUI or logging
// -----------------------------------------------------------------------------
class Simulator {
public:
    explicit Simulator(SimConfig config = {});

    // -- Setup -------------------------------------------------------------
    Grid& grid() { return grid_; }
    const Grid& grid() const { return grid_; }
    SimConfig& config() { return config_; }

    /// Register a named probe at coord (cell must already be PATH/PROBE type).
    void addProbe(const std::string& label, HexCoord coord);

    /// Register a named switch (maps label -> cell coord).
    void addSwitch(const std::string& label, HexCoord coord);

    /// Manually place an ant (e.g. for initialisation).
    AntID spawnAnt(HexCoord coord, Direction dir, int colour = 0);

    // -- Simulation control ------------------------------------------------
    void step();
    void reset();

    uint64_t timeStep() const { return timeStep_; }

    // -- State accessors (for renderer) ------------------------------------
    const std::vector<Ant>& ants() const { return ants_; }
    size_t antCount() const {
        size_t n = 0;
        for (const auto& a : ants_) if (a.alive) ++n;
        return n;
    }

    const std::unordered_map<std::string, ProbeRecord>& probes() const {
        return probes_;
    }
    ProbeRecord* probe(const std::string& label) {
        auto it = probes_.find(label);
        return it != probes_.end() ? &it->second : nullptr;
    }
    const ProbeRecord* probe(const std::string& label) const {
        auto it = probes_.find(label);
        return it != probes_.end() ? &it->second : nullptr;
    }

    // -- Switch control ----------------------------------------------------
    void setSwitchActive(const std::string& label, bool active);
    bool getSwitchActive(const std::string& label) const;
    const std::unordered_map<std::string, HexCoord>& switches() const {
        return switches_;
    }

    // -- Step callbacks (extension points) --------------------------------
    using StepCallback = std::function<void(Simulator&)>;
    void setPreStepCallback (StepCallback cb) { preStep_  = std::move(cb); }
    void setPostStepCallback(StepCallback cb) { postStep_ = std::move(cb); }

private:
    // -- Core substeps -----------------------------------------------------
    void substepSpawn();
    void substepChooseAndSecrete();   // CHOOSEACTION steps 1-4 for all ants
    void substepCollisionHandle();    // COLLISIONHANDLING (Figure 4)
    void substepRemoveSink();
    void substepUpdatePheromone();
    void substepUpdateProbes();

    // CHOOSEACTION for a single ant (fills ant.claimed, may secrete)
    void chooseAction(Ant& ant);

    // Random helpers
    double randUniform();   // [0,1)
    int    randInt(int n);  // [0, n)

    SimConfig config_;
    Grid      grid_;

    std::vector<Ant> ants_;
    AntID            nextAntId_ = 1;
    uint64_t         timeStep_  = 0;

    std::unordered_map<std::string, ProbeRecord> probes_;
    std::unordered_map<std::string, HexCoord>    switches_;

    // Multi-ant collision: C(L) = set of ant indices claiming L
    std::unordered_map<HexCoord, std::vector<size_t>, HexCoordHash> claims_;

    StepCallback preStep_;
    StepCallback postStep_;

    std::mt19937 rng_;
};
