//  test_main.cpp - self-contained unit tests (no external framework).
//  Run:  ./ant_tests
//  Exit 0 = all pass, nonzero = failures.

#include "simulator/Simulator.h"
#include "circuit/CircuitLoader.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <sstream>

// -- Minimal test harness ------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define TEST(name) void name()
#define RUN(name)  do { std::cerr << "  " #name " ... "; \
    try { name(); std::cerr << "PASS\n"; ++g_pass; } \
    catch(const std::exception& e){ std::cerr << "FAIL: " << e.what() << "\n"; ++g_fail; } \
    catch(...){ std::cerr << "FAIL (unknown)\n"; ++g_fail; } } while(0)
#define ASSERT(cond) do { if(!(cond)) throw std::runtime_error("assert failed: " #cond \
    " at line " + std::to_string(__LINE__)); } while(0)
#define ASSERT_NEAR(a,b,eps) do { if(std::abs((double)(a)-(double)(b))>(eps)) \
    throw std::runtime_error(std::string("assert_near failed: |") + \
    std::to_string(a) + " - " + std::to_string(b) + "| > " + std::to_string(eps)); } while(0)

// -----------------------------------------------------------------------------
TEST(test_hex_direction) {
    HexCoord origin{0,0};
    for (int d = 0; d < 6; ++d) {
        Direction dir = dirFromInt(d);
        HexCoord nb = origin.neighbor(dir);
        auto back = nb.dirTo(origin);
        ASSERT(back.has_value());
        ASSERT(dirToInt(*back) == dirToInt(dirOpp(dir)));
    }
}

TEST(test_hex_front_arc) {
    // For direction E (0), front arc should be NW(2), E(0), SE(5)? No:
    // frontArc(E) = {CCW(E), E, CW(E)} = {NE(1), E(0), SE(5)}
    // Actually: CCW of E(0) = 5=SE, CW of E(0) = 1=NE? Let's verify:
    // dirCCW(E=0) = dirFromInt(-1) = dirFromInt(5) = SE
    // dirCW(E=0)  = dirFromInt(1)  = NE
    // So arc = {SE, E, NE}
    auto arc = HexCoord::frontArc(Direction::E);
    ASSERT(arc.size() == 3);
    // All three are distinct
    ASSERT(arc[0] != arc[1] && arc[1] != arc[2] && arc[0] != arc[2]);
}

TEST(test_grid_set_get) {
    SimConfig cfg;
    Grid grid(cfg);
    grid.setCell({0,0}, CellType::PATH);
    grid.setCell({1,0}, CellType::SINK);
    ASSERT(grid.hasCell({0,0}));
    ASSERT(grid.hasCell({1,0}));
    ASSERT(!grid.hasCell({99,99}));
    ASSERT(grid.getCell({0,0})->type == CellType::PATH);
    ASSERT(grid.getCell({1,0})->type == CellType::SINK);
}

TEST(test_inter_cell_wall) {
    SimConfig cfg;
    Grid grid(cfg);
    grid.setCell({0,0}, CellType::PATH);
    grid.setCell({1,0}, CellType::PATH);
    ASSERT(!grid.hasWallBetween({0,0},{1,0}));
    grid.addWallBetween({0,0},{1,0});
    ASSERT(grid.hasWallBetween({0,0},{1,0}));
    ASSERT(grid.hasWallBetween({1,0},{0,0}));  // symmetry
}

TEST(test_pheromone_dissipation) {
    // With f=0 (no diffusion) and d=0.1: P(t) = (1-0.1)^t * P(0)
    SimConfig cfg;
    cfg.diffusionRate   = 0.0;
    cfg.dissipationRate = 0.1;
    cfg.numPheromoneSpecies = 1;
    Grid grid(cfg);
    Cell& c = grid.getOrCreateCell({0,0});
    c.type = CellType::PATH;
    c.pheromone[0] = 10.0f;

    // No pumps, no secretion, no neighbours
    for (int i = 0; i < 10; ++i) grid.stepPheromone();

    float expected = 10.0f * std::pow(0.9f, 10);
    ASSERT_NEAR(c.pheromone[0], expected, 0.1f);
}

TEST(test_pheromone_pump) {
    // Pump adds p per step. With d>0 it converges to p/d (Theorem 2).
    SimConfig cfg;
    cfg.diffusionRate   = 0.0;
    cfg.dissipationRate = 0.1;
    cfg.numPheromoneSpecies = 1;
    Grid grid(cfg);
    Cell& c = grid.getOrCreateCell({0,0});
    c.type = CellType::PUMP;
    c.pumpRate[0] = 1.0f;

    for (int i = 0; i < 200; ++i) grid.stepPheromone();

    // Steady state: p/d = 1.0/0.1 = 10
    ASSERT_NEAR(c.pheromone[0], 10.0f, 0.5f);
}

TEST(test_pheromone_upper_bound) {
    // Theorem 2: P <= (s + pmax) / d
    SimConfig cfg;
    cfg.dissipationRate = 0.10;
    cfg.secretionAmount = 12.0;
    cfg.numPheromoneSpecies = 1;
    Grid grid(cfg);
    for (int q = -3; q <= 3; ++q)
        for (int r = -3; r <= 3; ++r) {
            Cell& c = grid.getOrCreateCell({q,r});
            c.type = CellType::PUMP;
            c.pumpRate[0] = 1.0f;  // pmax = 1
        }
    for (int i = 0; i < 500; ++i) grid.stepPheromone();

    float pmax  = 1.0f;
    float s     = static_cast<float>(cfg.secretionAmount);
    float bound = (s + pmax) / static_cast<float>(cfg.dissipationRate);

    for (auto& [coord, cell] : grid.cells())
        ASSERT(cell.pheromone[0] <= bound + 1.0f); // small tolerance
}

TEST(test_ant_spawn_and_move) {
    SimConfig cfg;
    cfg.sourceSpawnProbability = 1.0;  // always spawn
    cfg.nonlinearityExponent   = 1.0;  // uniform random movement
    cfg.numPheromoneSpecies    = 1;
    Simulator sim(cfg);

    // Simple 3-cell line: SOURCE → PATH → SINK
    Grid& g = sim.grid();
    Cell& src = g.getOrCreateCell({0,0});
    src.type = CellType::SOURCE;
    src.spawnDirection = Direction::E;

    Cell& mid = g.getOrCreateCell({1,0});
    mid.type = CellType::PATH;

    Cell& snk = g.getOrCreateCell({2,0});
    snk.type = CellType::SINK;

    // Run enough steps for ant to traverse and get removed
    for (int i = 0; i < 10; ++i) sim.step();

    // Ants should have been removed at the sink; living count bounded
    int alive = 0;
    for (const auto& a : sim.ants()) if (a.alive) ++alive;
    ASSERT(alive <= 3);  // at most one per cell (3 cells)
}

TEST(test_collision_safety) {
    // Multiple ants converging on the same cell must not stack
    SimConfig cfg;
    cfg.sourceSpawnProbability = 1.0;
    cfg.nonlinearityExponent   = 0.1;  // near-uniform
    cfg.numPheromoneSpecies    = 1;
    Simulator sim(cfg);

    Grid& g = sim.grid();
    // Two sources pointing at the same merge cell
    auto mkSrc = [&](HexCoord c, Direction d) {
        Cell& cell = g.getOrCreateCell(c);
        cell.type = CellType::SOURCE;
        cell.spawnDirection = d;
    };
    mkSrc({0,0}, Direction::E);
    mkSrc({0,-1}, Direction::SE);

    Cell& merge = g.getOrCreateCell({1,0});
    merge.type = CellType::PATH;

    Cell& snk = g.getOrCreateCell({2,0});
    snk.type = CellType::SINK;

    for (int t = 0; t < 50; ++t) {
        sim.step();
        // Safety invariant: no two ants on the same cell
        std::unordered_map<HexCoord,int,HexCoordHash> counts;
        for (const auto& a : sim.ants())
            if (a.alive) counts[a.location]++;
        for (auto& [coord, cnt] : counts)
            ASSERT(cnt == 1);
    }
}

TEST(test_probe_records) {
    SimConfig cfg;
    cfg.sourceSpawnProbability = 1.0;
    cfg.nonlinearityExponent   = 30.0;
    cfg.numPheromoneSpecies    = 1;
    Simulator sim(cfg);

    Grid& g = sim.grid();
    Cell& src = g.getOrCreateCell({0,0});
    src.type = CellType::SOURCE;
    src.spawnDirection = Direction::E;

    Cell& probe = g.getOrCreateCell({1,0});
    probe.type = CellType::PROBE;

    Cell& snk = g.getOrCreateCell({2,0});
    snk.type = CellType::SINK;

    sim.addProbe("P1", {1,0});

    for (int i = 0; i < 50; ++i) sim.step();

    const ProbeRecord* rec = sim.probe("P1");
    ASSERT(rec != nullptr);
    ASSERT(!rec->antFlowHistory.empty());
    ASSERT(rec->currentAntFlow >= 0.0f && rec->currentAntFlow <= 1.0f);
}

TEST(test_circuit_loader_string) {
    const std::string circuit = R"(
PARAM spawn_prob 0.95
PARAM nonlinearity 30
SOURCE 0 0 E
PATH   1 0
PROBE  2 0 MyProbe
SINK   3 0
)";
    Simulator sim;
    CircuitLoader::loadFromString(circuit, sim);

    ASSERT(sim.grid().hasCell({0,0}));
    ASSERT(sim.grid().getCell({0,0})->type == CellType::SOURCE);
    ASSERT(sim.grid().hasCell({3,0}));
    ASSERT(sim.grid().getCell({3,0})->type == CellType::SINK);
    ASSERT(sim.probe("MyProbe") != nullptr);
}

TEST(test_switch_toggle) {
    Simulator sim;
    Grid& g = sim.grid();
    Cell& sw = g.getOrCreateCell({0,0});
    sw.type = CellType::SWITCH;
    sw.spawnDirection = Direction::E;
    sw.switchActive = false;
    sw.label = "SW1";
    sim.addSwitch("SW1", {0,0});

    g.setCell({1,0}, CellType::SINK);

    ASSERT(!sim.getSwitchActive("SW1"));
    sim.setSwitchActive("SW1", true);
    ASSERT(sim.getSwitchActive("SW1"));

    for (int i = 0; i < 5; ++i) sim.step();
    // With switch active, ants should spawn
    int alive = 0;
    for (const auto& a : sim.ants()) if (a.alive) ++alive;
    // At least attempted spawn (sink may eat them; just check no crash)
    ASSERT(sim.timeStep() == 5);
}

TEST(test_inverter_logic) {
    // Verified inverter topology (Michael 2009 Figure 2).
    // Source-path ants compare P(Pump-B) vs P(Sink-B).
    // Pump-A secretion raises P(Sink-B) when Input=ON, diverting ants from Output.
    const std::string inv = R"(
PARAM spawn_prob    0.95
PARAM nonlinearity  30.0
PARAM threshold     6.0
PARAM epsilon       0.1
PARAM secretion     12.0
PARAM dissipation   0.10
PARAM diffusion     0.10
PARAM probe_window  30

SOURCE -4  0  E
PATH   -3  0
PATH   -2  0
PATH   -1  0
PATH    0  0
PUMP    1  0  0.2
PATH    2  0
PROBE   3  0  Output
SINK    4  0
SINK    0  1
SWITCH -4  1  E  Input
PATH   -3  1
PATH   -2  1
PUMP   -1  1  1.0
WALL_BETWEEN -1 1   0 0
WALL_BETWEEN -1 1  -1 0
WALL_BETWEEN -2 1  -2 0
WALL_BETWEEN -2 1  -1 0
WALL_BETWEEN -3 1  -3 0
WALL_BETWEEN -3 1  -2 0
WALL_BETWEEN -4 1  -4 0
WALL_BETWEEN  1  0  0 1
)";
    Simulator sim;
    CircuitLoader::loadFromString(inv, sim);
    sim.setSwitchActive("Input", false);

    // Run 300 steps with Input=0; Output should be in logic-1 range
    for (int i = 0; i < 300; ++i) sim.step();
    float outputHigh = sim.probe("Output")->currentAntFlow;

    // Enable Input, run 400 more steps; Output should drop to ~0
    sim.setSwitchActive("Input", true);
    for (int i = 0; i < 400; ++i) sim.step();
    float outputLow = sim.probe("Output")->currentAntFlow;

    std::cerr << "\n    [inverter] output(Input=0)=" << outputHigh
              << "  output(Input=1)=" << outputLow << "\n    ";

    // Inverter property (Michael 2009 noise regions: x≈0.05, y≈0.22)
    ASSERT(outputHigh > 0.3f);   // should be well above logic-0 threshold
    ASSERT(outputLow  < 0.1f);   // should be near zero
    ASSERT(outputHigh > outputLow);
}

// -----------------------------------------------------------------------------
int main() {
    std::cerr << "=== Ant-Based Computer Simulator - Unit Tests ===\n";
    RUN(test_hex_direction);
    RUN(test_hex_front_arc);
    RUN(test_grid_set_get);
    RUN(test_inter_cell_wall);
    RUN(test_pheromone_dissipation);
    RUN(test_pheromone_pump);
    RUN(test_pheromone_upper_bound);
    RUN(test_ant_spawn_and_move);
    RUN(test_collision_safety);
    RUN(test_probe_records);
    RUN(test_circuit_loader_string);
    RUN(test_switch_toggle);
    RUN(test_inverter_logic);

    std::cerr << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
