#include "simulator/Simulator.h"
#include "circuit/CircuitLoader.h"
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>

static const std::string BUILTIN_INVERTER = R"(
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

int main(int argc, char* argv[]) {
    std::string circuitFile;
    int maxSteps = 800;
    std::string outFile;

    if (argc >= 2) circuitFile = argv[1];
    if (argc >= 3) maxSteps    = std::stoi(argv[2]);
    if (argc >= 4) outFile     = argv[3];

    SimConfig cfg;
    Simulator sim(cfg);

    try {
        if (!circuitFile.empty())
            CircuitLoader::load(circuitFile, sim);
        else {
            CircuitLoader::loadFromString(BUILTIN_INVERTER, sim);
            sim.setSwitchActive("Input", false);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::vector<std::string> probeLabels;
    for (const auto& [lbl, _] : sim.probes())
        probeLabels.push_back(lbl);

    std::ofstream fileOut;
    std::ostream* out = &std::cout;
    if (!outFile.empty()) {
        fileOut.open(outFile);
        if (!fileOut.is_open()) { std::cerr << "Cannot open: " << outFile << "\n"; return 1; }
        out = &fileOut;
        std::cerr << "Writing to: " << outFile << "\n";
    }

    *out << "step";
    for (const auto& lbl : probeLabels)
        *out << "," << lbl << "_flow," << lbl << "_pheromone";
    *out << "\n";

    for (int t = 0; t < maxSteps; ++t) {
        if (t == 200) sim.setSwitchActive("Input", true);
        if (t == 500) sim.setSwitchActive("Input", false);

        sim.step();

        if (t % 5 == 0) {
            *out << t;
            for (const auto& lbl : probeLabels) {
                const ProbeRecord* rec = sim.probe(lbl);
                if (rec)
                    *out << std::fixed << std::setprecision(4)
                         << "," << rec->currentAntFlow << "," << rec->currentPheromone;
                else
                    *out << ",0,0";
            }
            *out << "\n";
        }
        if (t % 100 == 0)
            std::cerr << "  step " << t << "/" << maxSteps
                      << "  ants=" << sim.antCount() << "\n";
    }
    std::cerr << "Done. Final ants: " << sim.antCount() << "\n";
    return 0;
}
