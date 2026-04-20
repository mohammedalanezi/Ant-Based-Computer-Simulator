#pragma once
#include "core/Types.h"
#include "core/HexCoord.h"

// -----------------------------------------------------------------------------
//  Ant - a single agent in the simulation.
//
//  During collision resolution (COLLISIONHANDLING) the ant first "claims" a
//  target cell; actual movement happens only when the target is confirmed empty.
// -----------------------------------------------------------------------------
struct Ant {
    AntID     id        = INVALID_ANT;
    HexCoord  location  = {};
    Direction direction = Direction::E;
    bool      alive     = true;

    // -- Collision-resolution scratch fields (valid only during one step) ---
    HexCoord  claimed   = {};   // the cell this ant wants to move to
    bool      hasClaim  = false;

    // -- Extensibility -----------------------------------------------------
    // Future: ant type, memory state, carried cargo, species tag, etc.
    // int  species = 0;
    int colour = 0; // for tracking ant sources
};
