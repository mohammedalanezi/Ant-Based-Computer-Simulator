#pragma once
#include <vector>
#include <string>

// -----------------------------------------------------------------------------
//  SimConfig - all tunable parameters for the ant/pheromone model.
//
//  Defaults match the empirical values from Michael (2009), Table 1.
//  Each field has a brief note explaining its role.
// -----------------------------------------------------------------------------
struct SimConfig {
    // -- Ant behaviour --------------------------------------------------------
    /// Probability that a SOURCE cell spawns an ant each time-step.
    double sourceSpawnProbability = 0.95;

    /// Exponent n in the probabilistic move function P(L)^n / Σ P(L)^n.
    /// Higher n → more deterministic (ants strongly prefer highest pheromone).
    double nonlinearityExponent = 30.0;

    /// Pheromone threshold T.  An ant secretes when:
    ///   P(LC) >= T + epsilon  AND  exists reachable L s.t. P(L) <= T - epsilon
    double pheromoneThreshold = 6.0;

    /// Sensing accuracy margin ε (epsilon).
    double thresholdEpsilon = 0.1;

    /// Fixed amount of pheromone secreted by an ant when the condition is met.
    double secretionAmount = 12.0;

    // -- Pheromone dynamics ---------------------------------------------------
    /// Fraction of pheromone that dissipates into the environment each step.
    double dissipationRate = 0.10;

    /// Fraction by which pheromone diffuses to adjacent cells each step.
    double diffusionRate = 0.10;

    // -- Probe / display ------------------------------------------------------
    /// Running-average window length (time steps) for probe ant-flow.
    int    probeWindowLength = 30;

    /// Maximum number of history points stored per probe (for graphing).
    int    probeHistoryLength = 500;

    // -- Extensibility hooks --------------------------------------------------
    /// Number of distinct pheromone species (currently always 1; reserved for
    /// future multi-pheromone extensions).
    int    numPheromoneSpecies = 1;
};
