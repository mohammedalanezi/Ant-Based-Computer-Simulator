#include "circuit/CircuitLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

// -----------------------------------------------------------------------------
static std::string toupper_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

Direction CircuitLoader::parseDirection(const std::string& token) {
    std::string t = toupper_str(token);
    if (t == "E")  return Direction::E;
    if (t == "NE") return Direction::NE;
    if (t == "NW") return Direction::NW;
    if (t == "W")  return Direction::W;
    if (t == "SW") return Direction::SW;
    if (t == "SE") return Direction::SE;
    throw std::runtime_error("Unknown direction: " + token);
}

// -----------------------------------------------------------------------------
void CircuitLoader::parseLine(const std::string& rawLine, Simulator& sim, std::vector<std::pair<HexCoord,Direction>>& initAnts) {
    // Strip comment
    std::string line = rawLine.substr(0, rawLine.find('#'));
    // Trim
    while (!line.empty() && std::isspace(line.front())) line.erase(line.begin());
    while (!line.empty() && std::isspace(line.back()))  line.pop_back();
    if (line.empty()) return;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    cmd = toupper_str(cmd);

    Grid& grid = sim.grid();
    SimConfig& cfg = sim.config();

    if (cmd == "PARAM") {
        std::string name; double val;
        iss >> name >> val;
        name = toupper_str(name);
        if      (name == "SPAWN_PROB"  || name == "SOURCE_SPAWN_PROBABILITY") cfg.sourceSpawnProbability = val;
        else if (name == "NONLINEARITY"|| name == "NONLINEARITY_EXPONENT")    cfg.nonlinearityExponent   = val;
        else if (name == "THRESHOLD"   || name == "PHEROMONE_THRESHOLD")      cfg.pheromoneThreshold     = val;
        else if (name == "EPSILON"     || name == "THRESHOLD_EPSILON")        cfg.thresholdEpsilon       = val;
        else if (name == "SECRETION"   || name == "SECRETION_AMOUNT")         cfg.secretionAmount        = val;
        else if (name == "DISSIPATION" || name == "DISSIPATION_RATE")         cfg.dissipationRate        = val;
        else if (name == "DIFFUSION"   || name == "DIFFUSION_RATE")           cfg.diffusionRate          = val;
        else if (name == "PROBE_WINDOW"|| name == "PROBE_WINDOW_LENGTH")      cfg.probeWindowLength      = (int)val;
        else std::cerr << "[CircuitLoader] Unknown PARAM: " << name << "\n";
    }
    else if (cmd == "PATH") {
        int q, r; iss >> q >> r;
        grid.setCell({q,r}, CellType::PATH);
    }
    else if (cmd == "WALL") {
        int q, r; iss >> q >> r;
        grid.setCell({q,r}, CellType::WALL);
    }
    else if (cmd == "SOURCE") {
        int q, r; std::string dirStr; double rate;
        iss >> q >> r >> dirStr;
        Direction dir       = parseDirection(dirStr);
        Cell& cell          = grid.getOrCreateCell({q,r});
        cell.type           = CellType::SOURCE;
        cell.colour         = abs(q + r) % MAX_COLOURS;
        cell.spawnDirection = dir;
        if (iss >> rate)
            cell.spawnProbability = rate;
    }
    else if (cmd == "SINK") {
        int q, r; iss >> q >> r;
        grid.setCell({q,r}, CellType::SINK);
    }
    else if (cmd == "PUMP") {
        int q, r; double rate;
        iss >> q >> r >> rate;
        Cell& cell = grid.getOrCreateCell({q,r});
        cell.type = CellType::PUMP;
        cell.pumpRate.assign(cfg.numPheromoneSpecies, 0.0f);
        cell.pumpRate[DEFAULT_PHEROMONE] = static_cast<float>(rate);
    }
    else if (cmd == "SWITCH") {
        int q, r; std::string dirStr, label; double rate;
        iss >> q >> r >> dirStr >> label;
        Direction dir = parseDirection(dirStr);
        Cell& cell = grid.getOrCreateCell({q,r});
        cell.type           = CellType::SWITCH;
        cell.spawnDirection = dir;
        if (iss >> rate)
            cell.spawnProbability = rate;
        cell.label          = label;
        cell.switchActive   = false;  // switches start closed (off)
        cell.colour         = abs(q + r) % MAX_COLOURS;
        sim.addSwitch(label, {q,r});
    }
    else if (cmd == "PROBE") {
        int q, r; std::string label;
        iss >> q >> r >> label;
        Cell& cell = grid.getOrCreateCell({q,r});
        cell.type  = CellType::PROBE;
        cell.label = label;
        sim.addProbe(label, {q,r});
    }
    else if (cmd == "BRIDGE") {
        int q, r; iss >> q >> r;
        grid.setCell({q,r}, CellType::BRIDGE);
    }
    else if (cmd == "WALL_BETWEEN") {
        int q1, r1, q2, r2;
        iss >> q1 >> r1 >> q2 >> r2;
        grid.addWallBetween({q1,r1}, {q2,r2});
    }
    else if (cmd == "INIT_ANT") {
        int q, r; std::string dirStr;
        iss >> q >> r >> dirStr;
        initAnts.push_back({{q,r}, parseDirection(dirStr)});
    }
    else if (cmd == "PHEROMONE") {
        int q, r; double amount;
        iss >> q >> r >> amount;
        Cell& cell = grid.getOrCreateCell({q,r});
        if (cell.type == CellType::VOID) cell.type = CellType::PATH;
        cell.pheromone[DEFAULT_PHEROMONE] = static_cast<float>(amount);
    }
    else {
        std::cerr << "[CircuitLoader] Unknown command: " << cmd << "\n";
    }
}

// -----------------------------------------------------------------------------
void CircuitLoader::loadFromString(const std::string& text, Simulator& sim) {
    std::vector<std::pair<HexCoord,Direction>> initAnts;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
        parseLine(line, sim, initAnts);
    // Place initial ants after grid is fully set up
    for (auto& [coord, dir] : initAnts)
        sim.spawnAnt(coord, dir);
}

void CircuitLoader::load(const std::string& filename, Simulator& sim) {
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("CircuitLoader: cannot open '" + filename + "'");

    std::vector<std::pair<HexCoord,Direction>> initAnts;
    std::string line;
    while (std::getline(file, line))
        parseLine(line, sim, initAnts);
    for (auto& [coord, dir] : initAnts)
        sim.spawnAnt(coord, dir);
}
