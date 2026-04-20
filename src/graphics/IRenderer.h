#pragma once
#include "simulator/Simulator.h"
#include <string>

// -----------------------------------------------------------------------------
//  IRenderer - abstract interface for rendering the ant simulator.
//
//  Concrete implementations (SFML, OpenGL, terminal …) inherit this.
//  Swapping 2D → 3D requires only a new IRenderer subclass.
// -----------------------------------------------------------------------------
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// Return false when the window/session should close.
    virtual bool isOpen() const = 0;

    /// Handle user input, resize events, etc.
    virtual void processEvents(Simulator& sim) = 0;

    /// Render the current simulator state.
    virtual void render(const Simulator& sim) = 0;

    /// Optional: display a status string in the title bar or HUD.
    virtual void setStatusText(const std::string& /*text*/) {}
};
