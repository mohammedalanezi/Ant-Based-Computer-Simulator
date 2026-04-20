#include "simulator/Simulator.h"
#include "circuit/CircuitLoader.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <sstream>

// -----------------------------------------------------------------------------
//  Minimal test framework
// -----------------------------------------------------------------------------
static int g_passed = 0, g_failed = 0;

#define ASSERT(cond, msg)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr << "FAIL [" << __func__ << ":" << __LINE__        \
                      << "] " << (msg) << "\n";                         \
            ++g_failed;                                                 \
        } else {                                                        \
            ++g_passed;                                                 \
        }                                                               \
    } while(0)

#define ASSERT_NEAR(a, b, tol, msg) ASSERT(std::fabs((a)-(b)) < (tol), msg)

// -----------------------------------------------------------------------------
//  Test: HexCoord direction arithmetic
// -----------------------------------------------------------------------------
void test_hexcoord() {
    HexCoord origin{0,0};
    HexCoord east = origin.neighbor(Direction::E);
    ASSERT(east.q == 1 && east.r == 0, "East neighbor q=1,r=0");

    auto dir = origin.dirTo(east);
    ASSERT(dir.has_value() && *dir == Direction::E, "dirTo E");

    HexCoord ne = origin.neighbor(Direction::NE);
    ASSERT(ne.q == 1 && ne.r == -1, "NE neighbor");

    // Front arc of East: frontArc returns {dirCCW(d), d, dirCW(d)}
    // dirCW increments index: E(0)->NE(1); dirCCW: E(0)->SE(5)
    // So frontArc(E) = {SE, E, NE}
    auto arc = HexCoord::frontArc(Direction::E);
    ASSERT(arc[0] == Direction::SE, "arc[0] = CCW(E) = SE");
    ASSERT(arc[1] == Direction::E,  "arc[1] = E");
    ASSERT(arc[2] == Direction::NE, "arc[2] = CW(E) = NE");

    std::cout << "  test_hexcoord: OK\n";
}

// -----------------------------------------------------------------------------
//  Test: Grid cell placement and retrieval
// -----------------------------------------------------------------------------
void test_grid_basic() {
    SimConfig cfg;
    Grid grid(cfg);

    grid.setCell({0,0}, CellType::PATH);
    grid.setCell({1,0}, CellType::SOURCE);
    grid.setCell({2,0}, CellType::SINK);

    ASSERT(grid.hasCell({0,0}), "PATH cell exists");
    ASSERT(grid.getCell({0,0})->type == CellType::PATH, "PATH type");
    ASSERT(grid.getCell({1,0})->type == CellType::SOURCE, "SOURCE type");
    ASSERT(grid.getCell({2,0})->type == CellType::SINK,   "SINK type");
    ASSERT(!grid.hasCell({99,99}), "non-existent cell");

    // Ant placement
    grid.placeAnt({0,0}, 42);
    ASSERT(grid.hasAnt({0,0}), "ant placed");
    ASSERT(grid.antAt({0,0}) == 42, "antAt returns correct ID");
    grid.removeAnt({0,0}, 42);
    ASSERT(!grid.hasAnt({0,0}), "ant removed");

    std::cout << "  test_grid_basic: OK\n";
}

// -----------------------------------------------------------------------------
//  Test: Inter-cell walls block reachability
// -----------------------------------------------------------------------------
void test_intercell_walls() {
    SimConfig cfg;
    Grid grid(cfg);

    grid.setCell({0,0}, CellType::PATH);
    grid.setCell({1,0}, CellType::PATH);  // E neighbor

    // Without wall: (1,0) should be reachable from (0,0) facing E
    auto reach = grid.reachableCells({0,0}, Direction::E);
    ASSERT(!reach.empty(), "E neighbor reachable without wall");

    // Add wall
    grid.addWallBetween({0,0}, {1,0});
    reach = grid.reachableCells({0,0}, Direction::E);
    bool found = false;
    for (auto& c : reach) if (c == HexCoord{1,0}) { found = true; break; }
    ASSERT(!found, "E neighbor blocked by inter-cell wall");

    std::cout << "  test_intercell_walls: OK\n";
}

// -----------------------------------------------------------------------------
//  Test: Pheromone dissipation converges to p/d (Theorem 2 bound)
// -----------------------------------------------------------------------------
void test_pheromone_dissipation() {
    SimConfig cfg;
    cfg.diffusionRate   = 0.0;   // no diffusion → simpler formula
    cfg.dissipationRate = 0.10;
    Simulator sim(cfg);

    // Single isolated pump cell
    sim.grid().setCell({0,0}, CellType::PUMP);
    sim.grid().getOrCreateCell({0,0}).pumpRate[0] = 1.0f;

    // Run 500 steps
    for (int i = 0; i < 500; ++i) sim.step();

    float ph = sim.grid().getPheromone({0,0});
    float expected = 1.0f / 0.10f;  // p/d = 10.0
    ASSERT_NEAR(ph, expected, 0.5f, "Pheromone steady-state ≈ p/d");

    // Also verify Theorem 2: ph ≤ (s + pmax)/d
    float bound = (float)(cfg.secretionAmount + 1.0) / (float)cfg.dissipationRate;
    ASSERT(ph <= bound + 0.01f, "Pheromone within Theorem 2 bound");

    std::cout << "  test_pheromone_dissipation: OK (ph=" << ph
              << ", expected≈" << expected << ")\n";
}

// -----------------------------------------------------------------------------
//  Test: Pheromone diffusion - average spreads outward
// -----------------------------------------------------------------------------
void test_pheromone_diffusion() {
    SimConfig cfg;
    cfg.diffusionRate   = 0.50;  // fast diffusion
    cfg.dissipationRate = 0.00;  // no dissipation
    Grid grid(cfg);

    // Line of 5 PATH cells
    for (int q = 0; q < 5; ++q) grid.setCell({q,0}, CellType::PATH);

    // Give centre cell a large initial pheromone
    grid.getOrCreateCell({2,0}).pheromone[0] = 100.f;

    // Step a few times and check it spread
    for (int i = 0; i < 10; ++i) grid.stepPheromone();

    float left   = grid.getPheromone({0,0});
    float centre = grid.getPheromone({2,0});
    float right  = grid.getPheromone({4,0});

    ASSERT(left   > 0.0f, "Pheromone diffused to left end");
    ASSERT(right  > 0.0f, "Pheromone diffused to right end");
    ASSERT(centre > left, "Centre still higher than edges (decaying gradient)");

    std::cout << "  test_pheromone_diffusion: OK (left=" << left
              << " centre=" << centre << " right=" << right << ")\n";
}

// -----------------------------------------------------------------------------
//  Test: Theorem 1 - Termination, Safety, Liveness of collision handling
//
//  Place many ants on one cell merging into another - verify none disappear
//  and no cell ends up doubly occupied.
// -----------------------------------------------------------------------------
void test_collision_handling() {
    SimConfig cfg;
    cfg.sourceSpawnProbability = 0.0;  // disable auto-spawn
    Simulator sim(cfg);

    // Create a Y-merge: two SOURCE paths → one PATH
    // Upper arm: (-1,-1), lower arm: (-1,1), merge: (0,0), exit: (1,0)
    sim.grid().setCell({-1,-1}, CellType::PATH);
    sim.grid().setCell({-1, 1}, CellType::PATH);
    sim.grid().setCell({ 0, 0}, CellType::PATH);
    sim.grid().setCell({ 1, 0}, CellType::PATH);
    sim.grid().setCell({ 2, 0}, CellType::SINK);

    // Manually place ants
    int placed = 0;
    for (HexCoord c : std::vector<HexCoord>{{-1,-1},{-1,1}}) {
        AntID id = sim.spawnAnt(c, Direction::SE);
        if (id != INVALID_ANT) ++placed;
    }
    ASSERT(placed == 2, "2 ants placed");

    // Run 20 steps and verify no ant doubled up
    for (int t = 0; t < 20; ++t) {
        sim.step();
        std::unordered_map<HexCoord, int, HexCoordHash> occupancy;
        for (const Ant& a : sim.ants()) {
            if (!a.alive) continue;
            occupancy[a.location]++;
            ASSERT(occupancy[a.location] <= 1, "No two ants on same cell (step " + std::to_string(t) + ")");
        }
    }

    std::cout << "  test_collision_handling: OK\n";
}

// -----------------------------------------------------------------------------
//  Test: Source spawns ants, Sink removes them
// -----------------------------------------------------------------------------
void test_source_sink() {
    SimConfig cfg;
    cfg.sourceSpawnProbability = 1.0;  // always spawn
    Simulator sim(cfg);

    // SOURCE → PATH → PATH → SINK
    sim.grid().setCell({0,0}, CellType::SOURCE);
    sim.grid().getOrCreateCell({0,0}).spawnDirection = Direction::E;
    sim.grid().setCell({1,0}, CellType::PATH);
    sim.grid().setCell({2,0}, CellType::PATH);
    sim.grid().setCell({3,0}, CellType::SINK);

    // After enough steps ants should reach sink and be removed
    for (int i = 0; i < 10; ++i) sim.step();

    // Verify sink has no ants (they're removed on entry)
    ASSERT(!sim.grid().hasAnt({3,0}), "Sink cell empty after ants arrive");

    std::cout << "  test_source_sink: OK (total ants=" << sim.antCount() << ")\n";
}

// -----------------------------------------------------------------------------
//  Test: Circuit loader parses all cell types
// -----------------------------------------------------------------------------
void test_circuit_loader() {
    static const std::string circuit = R"(
PARAM spawn_prob   0.85
PARAM nonlinearity 25.0
PATH    0  0
WALL    1  0
SOURCE  2  0  E
SINK    3  0
PUMP    4  0  0.5
SWITCH  5  0  E  MySwitch
PROBE   6  0  MyProbe
BRIDGE  7  0
WALL_BETWEEN 0 0  1 0
PHEROMONE 4 0 3.0
)";
    SimConfig cfg;
    Simulator sim(cfg);
    CircuitLoader::loadFromString(circuit, sim);

    const Grid& g = sim.grid();
    ASSERT(g.getCell({0,0})->type == CellType::PATH,   "PATH parsed");
    ASSERT(g.getCell({1,0})->type == CellType::WALL,   "WALL parsed");
    ASSERT(g.getCell({2,0})->type == CellType::SOURCE, "SOURCE parsed");
    ASSERT(g.getCell({3,0})->type == CellType::SINK,   "SINK parsed");
    ASSERT(g.getCell({4,0})->type == CellType::PUMP,   "PUMP parsed");
    ASSERT(g.getCell({5,0})->type == CellType::SWITCH, "SWITCH parsed");
    ASSERT(g.getCell({6,0})->type == CellType::PROBE,  "PROBE parsed");
    ASSERT(g.getCell({7,0})->type == CellType::BRIDGE, "BRIDGE parsed");
    ASSERT(g.hasWallBetween({0,0},{1,0}), "WALL_BETWEEN parsed");
    ASSERT_NEAR(g.getPheromone({4,0}), 3.0f, 0.01f, "PHEROMONE parsed");

    ASSERT_NEAR(sim.config().sourceSpawnProbability, 0.85, 0.001, "PARAM spawn_prob");
    ASSERT_NEAR(sim.config().nonlinearityExponent,  25.0, 0.001, "PARAM nonlinearity");

    ASSERT(sim.probe("MyProbe") != nullptr, "Probe registered");
    ASSERT(sim.switches().count("MySwitch") > 0, "Switch registered");

    ASSERT(g.getCell({4,0})->pumpRate[0] == 0.5f, "Pump rate set");
    ASSERT(g.getCell({2,0})->spawnDirection == Direction::E, "Source direction");

    std::cout << "  test_circuit_loader: OK\n";
}

// -----------------------------------------------------------------------------
//  Test: Probe records running averages
// -----------------------------------------------------------------------------
void test_probe_recording() {
    SimConfig cfg;
    cfg.sourceSpawnProbability = 1.0;
    cfg.probeWindowLength = 5;
    Simulator sim(cfg);

    sim.grid().setCell({0,0}, CellType::SOURCE);
    sim.grid().getOrCreateCell({0,0}).spawnDirection = Direction::E;
    sim.grid().setCell({1,0}, CellType::PROBE);
    sim.grid().setCell({2,0}, CellType::SINK);
    sim.addProbe("P1", {1,0});

    for (int i = 0; i < 20; ++i) sim.step();

    const ProbeRecord* rec = sim.probe("P1");
    ASSERT(rec != nullptr, "Probe exists");
    ASSERT(!rec->antFlowHistory.empty(), "Probe has history");
    ASSERT(rec->antFlowHistory.size() <= (size_t)cfg.probeHistoryLength,
           "Probe history bounded");

    std::cout << "  test_probe_recording: OK (history size="
              << rec->antFlowHistory.size() << ")\n";
}

// -----------------------------------------------------------------------------
//  Test: Inverter inverts (integration test)
//
//  Correct topology (flat-top axial hex):
//
//   Source(-3,0)→(-2,0)→(-1,0)→CHOICE(0,0)→E→PUMP-B(1,0)→Output(2,0)→SINK(3,0)
//                                           ↘SE↘
//                                            SINK(0,1)   [Source ants' alt exit]
//
//   Input→(-2,4)→(-1,4)→PUMP-A(0,4)→(1,4)→SINK(2,4)
//
//   CHOICE(0,0) facing E: front arc = {SE=(0,1), E=(1,0), NE=(1,-1)}
//     - E neighbor (1,0) gets pheromone from Pump-B(1,0) [adjacent]
//     - SE neighbor (0,1) gets pheromone from Pump-A via diffusion
//
//   With n=30, small pheromone differences become near-deterministic.
//   Input OFF: Pump-B (adjacent, rate=0.2) > Pump-A diffusion at distance 3
//              → ants go E → Output HIGH
//   Input ON:  secretion at Pump-A (12 units, rate=1.0) raises Sink pheromone
//              far above Pump-B → ants go SE → Output LOW
// -----------------------------------------------------------------------------
void test_inverter_integration() {
    static const std::string INV = R"(
PARAM spawn_prob    0.95
PARAM nonlinearity  30.0
PARAM threshold     6.0
PARAM epsilon       0.1
PARAM secretion     12.0
PARAM dissipation   0.10
PARAM diffusion     0.10
PARAM probe_window  30

# -- Source path ------------------------------------------------------------
SOURCE -3  0  E
PATH   -2  0
PATH   -1  0
# Choice point (0,0) facing E: front arc = {SE=(0,1), E=(1,0), NE=(1,-1)}
PATH    0  0

# -- E arm: Output path via Pump-B -----------------------------------------
# Pump-B adjacent to choice → strong, constant pheromone at E neighbor
PUMP    1  0  0.2
PROBE   2  0  Output
SINK    3  0

# -- SE arm: Sink for Source ants (chosen when Pump-A pheromone rises) -----
SINK    0  1

# -- Pheromone bridge: allows Pump-A pheromone to diffuse up to Sink(0,1) --
# These cells carry NO ants (no Source, not reachable by Input ants who face E)
PATH    0  2
PATH    0  3

# -- Input path ------------------------------------------------------------
# Pump-A(0,4): Input ants pass through and secrete (P≈10 >= T+eps=6.1)
# Secretion (12 units) diffuses: (0,4)→(0,3)→(0,2)→Sink(0,1)
# Input ants face E at (0,4) → front arc {NE=(1,3),E=(1,4),SE=(0,5)}: cannot go NW
SWITCH -3  4  E  Input
PATH   -2  4
PATH   -1  4
PUMP    0  4  1.0
PATH    1  4
SINK    2  4
)";

    SimConfig cfg;
    Simulator sim(cfg);
    CircuitLoader::loadFromString(INV, sim);
    sim.setSwitchActive("Input", false);  // Input OFF

    // Warm up: Input OFF → expect Output HIGH
    for (int i = 0; i < 500; ++i) sim.step();
    float outputHigh = sim.probe("Output")->currentAntFlow;

    // Turn Input ON → expect Output LOW (after propagation delay)
    sim.setSwitchActive("Input", true);
    for (int i = 0; i < 500; ++i) sim.step();
    float outputLow = sim.probe("Output")->currentAntFlow;

    std::cout << "  test_inverter_integration:\n"
              << "    Input OFF → Output flow = " << outputHigh << "\n"
              << "    Input ON  → Output flow = " << outputLow  << "\n";

    // With n=30 non-linearity, the inversion should be clear
    ASSERT(outputHigh > outputLow,
           "Inverter: output higher when input off than when input on");
    ASSERT(outputHigh > 0.3f, "Inverter output HIGH when input OFF");
    ASSERT(outputLow  < 0.7f, "Inverter output LOW  when input ON");
}

// -----------------------------------------------------------------------------
//  Test: Pheromone upper bound (Theorem 2) with secretion
// -----------------------------------------------------------------------------
void test_theorem2_bound() {
    SimConfig cfg;
    cfg.dissipationRate = 0.10;
    cfg.secretionAmount = 12.0;
    float pmax = 2.0f;
    float bound = (float)(cfg.secretionAmount + pmax) / (float)cfg.dissipationRate;

    Simulator sim(cfg);
    // Pump at max rate
    sim.grid().setCell({0,0}, CellType::PUMP);
    sim.grid().getOrCreateCell({0,0}).pumpRate[0] = pmax;
    // Surrounding path cells (allow diffusion)
    for (int d = 0; d < 6; ++d) {
        HexCoord nb{ DIR_DQ[d], DIR_DR[d] };
        sim.grid().setCell(nb, CellType::PATH);
    }

    for (int i = 0; i < 1000; ++i) sim.step();

    float maxObserved = 0.f;
    for (const auto& [c, cell] : sim.grid().cells())
        if (!cell.pheromone.empty())
            maxObserved = std::max(maxObserved, cell.pheromone[0]);

    ASSERT(maxObserved <= bound + 0.1f,
           "Theorem 2: max pheromone ≤ (s+pmax)/d");
    std::cout << "  test_theorem2_bound: OK (max=" << maxObserved
              << " bound=" << bound << ")\n";
}

// -----------------------------------------------------------------------------
//  main
// -----------------------------------------------------------------------------
int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "  Ant-Based Computer Simulator - Unit Tests\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    test_hexcoord();
    test_grid_basic();
    test_intercell_walls();
    test_pheromone_dissipation();
    test_pheromone_diffusion();
    test_collision_handling();
    test_source_sink();
    test_circuit_loader();
    test_probe_recording();
    test_theorem2_bound();
    test_inverter_integration();  // slow - runs 800 sim steps

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  Results: " << g_passed << " passed, "
              << g_failed << " failed\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    return g_failed > 0 ? 1 : 0;
}
