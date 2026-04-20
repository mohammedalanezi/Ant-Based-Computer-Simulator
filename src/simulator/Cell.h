#pragma once
#include "core/Types.h"
#include <vector>
#include <string>

// -----------------------------------------------------------------------------
//  Cell - one hexagonal grid cell.
//
//  Pheromone storage is a vector<float> with one entry per pheromone species,
//  making multi-species extensions trivial.
// -----------------------------------------------------------------------------
struct Cell {
    CellType type = CellType::VOID;

    // Optional label used by SWITCH, PROBE, and OUTPUT cells
    std::string label;

    // -- Pheromone ---------------------------------------------------------
    std::vector<float> pheromone;       // current concentration per species
    std::vector<float> secretionAccum;  // secretions accumulated this step

    // -- SOURCE / SWITCH specifics -----------------------------------------
    Direction spawnDirection = Direction::E;  // direction new ants face
    bool      switchActive   = true;          // only relevant for SWITCH
    int       colour         = 0;             // colour of new ants
    double    spawnProbability = 1.0f;        // chance for a new ant to spawn

    // -- PUMP specifics ----------------------------------------------------
    // Index 0 corresponds to DEFAULT_PHEROMONE (species 0).
    std::vector<float> pumpRate;   // pheromone added per step per species

    // -- PROBE / statistics ------------------------------------------------
    int   antCount      = 0;    // ants present right now
    float antFlowAvg    = 0.0f; // running average of ant-flow [0,1]
    int   antFlowWindow = 0;    // samples counted in current window

    // ---------------------------------------------------------------------
    //  Helpers
    // ---------------------------------------------------------------------
    void init(int numSpecies) {
        pheromone.assign(numSpecies, 0.0f);
        secretionAccum.assign(numSpecies, 0.0f);
        pumpRate.assign(numSpecies, 0.0f);
    }

    float getPheromone(PheromoneID pid = DEFAULT_PHEROMONE) const {
        return (pid < (int)pheromone.size()) ? pheromone[pid] : 0.0f;
    }
    void addSecretion(float amount, PheromoneID pid = DEFAULT_PHEROMONE) {
        if (pid < (int)secretionAccum.size())
            secretionAccum[pid] += amount;
    }
    void clearSecretions() {
        for (auto& s : secretionAccum) s = 0.0f;
    }

    bool isTraversable() const { return isTravellable(type); }
    bool isActive() const {
        if (type == CellType::SOURCE) return true;
        if (type == CellType::SWITCH) return switchActive;
        return false;
    }
};
