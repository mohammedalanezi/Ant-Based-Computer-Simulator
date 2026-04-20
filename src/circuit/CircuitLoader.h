#pragma once
#include "simulator/Simulator.h"
#include <string>
#include <stdexcept>

// -----------------------------------------------------------------------------
//  CircuitLoader - parses a .circuit text file and populates a Simulator.
//
//  File format (lines processed top-to-bottom; # = comment):
//
//  PARAM  <name>  <value>        - override SimConfig fields
//  PATH   <q> <r>                - traversable path cell
//  WALL   <q> <r>                - solid wall cell
//  SOURCE <q> <r> <dir>*         - ant source; dir = E NE NW W SW SE
//  SINK   <q> <r>                - ant sink
//  PUMP   <q> <r> <rate>         - pheromone pump (species 0)
//  SWITCH <q> <r> <dir> <label>* - controllable source
//  PROBE  <q> <r> <label>        - measurement probe
//  BRIDGE <q> <r>                - bridge cell
//  WALL_BETWEEN <q1> <r1> <q2> <r2>  - inter-cell wall (prevents crossing)
//  INIT_ANT <q> <r> <dir>        - place an ant at startup
//  PHEROMONE <q> <r> <amount>    - set initial pheromone (species 0)
//
//  Direction tokens: E NE NW W SW SE  (case-insensitive)
//  SOURCE/SWITCH *:  <prob>      - probablity of an ant spawning
// -----------------------------------------------------------------------------

class CircuitLoader {
public:
    /// Load circuit from file into simulator.  Throws std::runtime_error on error.
    static void load(const std::string& filename, Simulator& sim);

    /// Load from a string (for embedding small circuits in code).
    static void loadFromString(const std::string& text, Simulator& sim);

private:
    static void parseLine(const std::string& line, Simulator& sim,
                          std::vector<std::pair<HexCoord,Direction>>& initAnts);
    static Direction parseDirection(const std::string& token);
};
