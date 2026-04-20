#pragma once
#include "graphics/IRenderer.h"
#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
//  SFMLRenderer - renders the ant simulator using SFML 2.5+.
//
//  Layout:
//    Left panel  : hex grid (main simulation view)
//    Right panel : probe graphs + control buttons
//
//  Controls (keyboard):
//    Space      - pause / resume
//    Right      - step once (while paused)
//    +/-        - increase/decrease simulation speed
//    0          - reset view
//    Scroll     - zoom in/out
//    Drag LMB   - pan
//    Click hex  - select cell (shows info in status bar)
//    S          - toggle display: show pheromone overlay on/off
//    A          - toggle ant rendering on/off
//    Tab        - cycle probe displayed in graph
//    [/]        - rotate circuit by 30 degrees
// -----------------------------------------------------------------------------
class SFMLRenderer : public IRenderer {
public:
    explicit SFMLRenderer(unsigned int winW = 1400, unsigned int winH = 800);
    ~SFMLRenderer() override = default;

    bool isOpen()                              const override;
    void processEvents(Simulator& sim)               override;
    void render(const Simulator& sim)                override;
    void setStatusText(const std::string& text)      override;

private:
    // -- Drawing helpers ---------------------------------------------------
    void drawGrid       (const Simulator& sim);
    void drawAnts       (const Simulator& sim);
    void drawUI         (const Simulator& sim);
    void drawProbeGraph (const Simulator& sim);
    void drawMiniMap    (const Simulator& sim);
    void drawWallsBetween(const Simulator& sim);
    void drawStatusBar  (const Simulator& sim);

    sf::Color antColor(const Ant& ant) const;
    sf::Color antOutlineColor(const Ant& ant) const;
    sf::Color cellColor(const Cell& cell, float maxPheromone) const;
    sf::Color pheromoneColor(float ph, float maxPh) const;
    void      drawHex(sf::RenderTarget& target, sf::Vector2f centre, float size, sf::Color fill, sf::Color outline);
    void rotateWindow(bool reset);

    // -- World <-> screen transforms -----------------------------------------
    sf::Vector2f hexToScreen(HexCoord h) const;
    HexCoord     screenToHex(sf::Vector2f p) const;

    // -- State -------------------------------------------------------------
    sf::RenderWindow window_;
    sf::Font         font_;
    bool             fontLoaded_ = false;

    float   hexSize_    = 20.0f;
    sf::Vector2f viewOrigin_ = {400.f, 400.f};  // pixel offset for (0,0) hex
    bool    paused_           = false;
    bool    showPheromone_    = true;
    bool    showAnts_         = true;
    int     simSpeed_         = 1;   // steps per render frame
    float   viewRotation_     = 0.f; // degrees CW; snaps to multiples of 30

    std::string statusText_;
    std::string selectedProbeLabel_;

    // Pan state
    bool         panning_    = false;
    sf::Vector2f panStart_   = {};
    sf::Vector2f originAtPanStart_ = {};

    // Right panel bounds
    sf::FloatRect uiPanel_;

    // Button state
    struct Button {
        sf::FloatRect rect;
        std::string   label;
        std::function<void(Simulator&)> action;
    };
    std::vector<Button> buttons_;

    void buildButtons();
    void handleButtonClick(sf::Vector2f pos, Simulator& sim);
};
