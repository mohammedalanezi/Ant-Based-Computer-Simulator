#include "simulator/Simulator.h"
#include "circuit/CircuitLoader.h"
#include "graphics/SFMLRenderer.h"

#include <iostream>
#include <string>

// -----------------------------------------------------------------------------
//  Embedded fallback circuit: a single inverter (loaded if no file given)
//  Layout (flat-top hex, axial q,r):
//        Source(0,-2)  ---path---  Pump-B(5,-2)
//        Input(-1,0)   ---path---  Output(6,0)
//        Pump-A(0,2)   ---path---  Sink(5,2)
//
// -----------------------------------------------------------------------------
static const std::string EMBEDDED_INVERTER = R"(
# Ant-Based Inverter (Michael 2009) - verified topology
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

// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::cout << "Ant-Based Computer Simulator\n";
    std::cout << "Based on: Michael (2009) and Michael & Yiannakides (2012)\n\n";
    std::cout << "Controls:\n"
              << "  Space       - pause/resume\n"
              << "  -> (Right)  - single step (when paused)\n"
              << "  +/-         - increase/decrease speed\n"
              << "  S           - toggle pheromone overlay\n"
              << "  A           - toggle ant rendering\n"
              << "  R           - reset simulation\n"
              << "  Tab         - cycle probe graphs\n"
              << "  Scroll      - zoom in/out\n"
              << "  Drag LMB    - pan\n"
              << "  [/]         - rotate left and right\n"
              << "  0           - reset view\n\n";

    SimConfig config;
    Simulator sim(config);

    try {
        if (argc >= 2) {
            std::string filename(argv[1]);
            std::cout << "Loading circuit: " << filename << "\n";
            CircuitLoader::load(filename, sim);
        } else {
            std::cout << "No circuit file given. Loading built-in inverter.\n"
                      << "Usage: ant_simulator [circuit.circuit]\n"
                      << "See circuits/ directory for examples.\n\n";
            CircuitLoader::loadFromString(EMBEDDED_INVERTER, sim);
            // Open the Input switch so it starts with ants flowing
            sim.setSwitchActive("Input", true);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading circuit: " << e.what() << "\n";
        return 1;
    }

    SFMLRenderer renderer(1400, 800);
    renderer.setStatusText("Ant-Based Computer Simulator");

    // Auto-centre view on the circuit bounding box
    if (!sim.grid().cells().empty()) {
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto& [coord, cell] : sim.grid().cells()) {
            auto px = hexToPixel(coord, 20.f, {0,0});
            minX = std::min(minX, px.x); maxX = std::max(maxX, px.x);
            minY = std::min(minY, px.y); maxY = std::max(maxY, px.y);
        }
        // Centre will be set by default; renderer starts at reasonable position
        (void)minX; (void)maxX; (void)minY; (void)maxY;
    }

    // Main loop
    while (renderer.isOpen()) {
        renderer.processEvents(sim);
        renderer.render(sim);
    }

    return 0;
}
