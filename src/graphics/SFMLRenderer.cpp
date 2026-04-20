#include "graphics/SFMLRenderer.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <climits>
#include <format>
#include <iostream>

static const float PI = 3.14159265f;

// -----------------------------------------------------------------------------
SFMLRenderer::SFMLRenderer(unsigned int winW, unsigned int winH) : window_(sf::VideoMode(winW, winH), "Ant-Based Computer Simulator", sf::Style::Default)
{
    window_.setFramerateLimit(120);

    // Try to load a system font; fall back gracefully
    const std::vector<std::string> fontPaths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/arial.ttf",
        "arial.ttf"
    };
    for (const auto& fp : fontPaths) {
        if (font_.loadFromFile(fp)) { fontLoaded_ = true; break; }
    }

    uiPanel_ = sf::FloatRect(static_cast<float>(winW) - 320.f, 0.f, 320.f, static_cast<float>(winH));
    buildButtons();
}

// -- IRenderer interface -------------------------------------------------------
bool SFMLRenderer::isOpen() const { return window_.isOpen(); }

void SFMLRenderer::setStatusText(const std::string& text) { statusText_ = text; }

// -----------------------------------------------------------------------------
//  Event handling
// -----------------------------------------------------------------------------
void SFMLRenderer::processEvents(Simulator& sim) {
    sf::Event evt;
    while (window_.pollEvent(evt)) {
        if (evt.type == sf::Event::Closed)
            window_.close();

        // -- Keyboard --
        if (evt.type == sf::Event::KeyPressed) {
            switch (evt.key.code) {
                case sf::Keyboard::Space:
                    paused_ = !paused_;
                    break;
                case sf::Keyboard::Right:
                    if (paused_) sim.step();
                    break;
                case sf::Keyboard::Add:
                case sf::Keyboard::Equal:
                    simSpeed_ = std::min(simSpeed_ * 2, 256);
                    break;
                case sf::Keyboard::Subtract:
                case sf::Keyboard::Dash:
                    simSpeed_ = std::max(simSpeed_ / 2, 1);
                    break;
                case sf::Keyboard::Num0:
                case sf::Keyboard::Numpad0:
                    viewOrigin_ = {(uiPanel_.left) / 2.f,
                                   window_.getSize().y / 2.f};
                    hexSize_ = 20.f;
                    break;
                case sf::Keyboard::S:
                    showPheromone_ = !showPheromone_;
                    break;
                case sf::Keyboard::A:
                    showAnts_ = !showAnts_;
                    break;
                case sf::Keyboard::F: {
                    // Auto-fit: centre view on circuit bounding box
                    const Grid& g = sim.grid();
                    if (g.cells().empty()) break;
                    float minPx= 1e9f,maxPx=-1e9f,minPy=1e9f,maxPy=-1e9f;
                    for (const auto& [c,cell] : g.cells()) {
                        if (cell.type==CellType::VOID) continue;
                        auto p = hexToPixel(c, 1.f, {0,0});
                        minPx=std::min(minPx,p.x); maxPx=std::max(maxPx,p.x);
                        minPy=std::min(minPy,p.y); maxPy=std::max(maxPy,p.y);
                    }
                    float cw = maxPx - minPx + 4.f;
                    float ch = maxPy - minPy + 4.f;
                    float fitW = uiPanel_.left * 0.9f;
                    float fitH = static_cast<float>(window_.getSize().y) * 0.9f;
                    hexSize_ = std::clamp(std::min(fitW/cw, fitH/ch) * 20.f,
                                          4.f, 120.f);
                    viewOrigin_ = sf::Vector2f(
                        uiPanel_.left/2.f - (minPx + cw/2.f) * hexSize_/20.f,
                        window_.getSize().y/2.f - (minPy + ch/2.f) * hexSize_/20.f);
                    break;
                }
                case sf::Keyboard::Tab: {
                    // Tab now cycles which probe is highlighted in UI list
                    auto& ps = sim.probes();
                    if (!ps.empty()) {
                        bool found = false;
                        for (auto it = ps.begin(); it != ps.end(); ++it) {
                            if (it->first == selectedProbeLabel_) {
                                auto next = std::next(it);
                                selectedProbeLabel_ = (next != ps.end())
                                    ? next->first : ps.begin()->first;
                                found = true; break;
                            }
                        }
                        if (!found && !ps.empty())
                            selectedProbeLabel_ = ps.begin()->first;
                    }
                    break;
                }
                case sf::Keyboard::R:
                    sim.reset();
                    break;
                case sf::Keyboard::LBracket:   // [ to rotate CCW
                    viewRotation_ = std::fmod(viewRotation_ - 30.f + 360.f, 360.f);
                    break;
                case sf::Keyboard::RBracket:   // ] to rotate CW
                    viewRotation_ = std::fmod(viewRotation_ + 30.f, 360.f);
                    break;
                default: break;
            }
        }

        // -- Mouse scroll -> zoom --
        if (evt.type == sf::Event::MouseWheelScrolled) {
            float factor = (evt.mouseWheelScroll.delta > 0) ? 1.15f : 1.f/1.15f;
            sf::Vector2f mPos(evt.mouseWheelScroll.x, evt.mouseWheelScroll.y);
            // Zoom towards cursor
            viewOrigin_ = mPos + (viewOrigin_ - mPos) * factor;
            hexSize_ = std::clamp(hexSize_ * factor, 4.f, 120.f);
        }

        // -- Mouse drag -> pan --
        if (evt.type == sf::Event::MouseButtonPressed &&
            evt.mouseButton.button == sf::Mouse::Left) {
            sf::Vector2f mPos(evt.mouseButton.x, evt.mouseButton.y);
            // Check if in UI panel first
            if (!uiPanel_.contains(mPos)) {
                panning_           = true;
                panStart_          = mPos;
                originAtPanStart_  = viewOrigin_;
            } else {
                handleButtonClick(mPos, sim);
            }
        }
        if (evt.type == sf::Event::MouseButtonReleased &&
            evt.mouseButton.button == sf::Mouse::Left)
            panning_ = false;

        if (evt.type == sf::Event::MouseMoved && panning_) {
            sf::Vector2f mPos(evt.mouseMove.x, evt.mouseMove.y);
            viewOrigin_ = originAtPanStart_ + (mPos - panStart_);
        }
    }

    // Run simulation steps
    if (!paused_)
        for (int i = 0; i < simSpeed_; ++i)
            sim.step();
}

// -----------------------------------------------------------------------------
//  Render
// -----------------------------------------------------------------------------
void SFMLRenderer::rotateWindow(bool reset) {
    if(reset)
        window_.setView(window_.getDefaultView());
    else
    {
        float winW = static_cast<float>(window_.getSize().x);
        float winH = static_cast<float>(window_.getSize().y);
        float gridW = uiPanel_.left;   // width of the non-UI area

        // A view whose world-coords match screen pixels, but is clipped to the grid area and rotated around its centre.
        sf::View gridView(sf::FloatRect(0.f, 0.f, gridW, winH));
        gridView.setViewport(sf::FloatRect(0.f, 0.f, gridW / winW, 1.f));
        gridView.setRotation(viewRotation_); 
        window_.setView(gridView);
    }
}

void SFMLRenderer::render(const Simulator& sim) {
    window_.clear(sf::Color(20, 20, 20));
    // Grid area drawn with rotated view
    rotateWindow(false);
    drawGrid(sim);
    drawWallsBetween(sim);
    if (showAnts_) drawAnts(sim);
    // Default/unrotated view
    rotateWindow(true);
    drawUI(sim);
    drawProbeGraph(sim);
    drawMiniMap(sim);
    drawStatusBar(sim);

    window_.display();
}

// -----------------------------------------------------------------------------
//  World ↔ screen
// -----------------------------------------------------------------------------
sf::Vector2f SFMLRenderer::hexToScreen(HexCoord h) const {
    PixelPos p = hexToPixel(h, hexSize_, {viewOrigin_.x, viewOrigin_.y});
    return {p.x, p.y};
}
HexCoord SFMLRenderer::screenToHex(sf::Vector2f p) const {
    return pixelToHex({p.x, p.y}, hexSize_, {viewOrigin_.x, viewOrigin_.y});
}

// -----------------------------------------------------------------------------
//  Colour helpers
// -----------------------------------------------------------------------------
sf::Color SFMLRenderer::pheromoneColor(float ph, float maxPh) const {
    if (maxPh <= 0.f) return sf::Color::Transparent;
    float t = std::min(ph / maxPh, 1.f);
    // Low -> teal, high -> deep-blue
    return sf::Color(
        0, static_cast<uint8_t>(200 * (1.f - t)), static_cast<uint8_t>(255),
        static_cast<uint8_t>(55 + 200 * t)
    );
}

sf::Color SFMLRenderer::antColor(const Ant& ant) const {
    switch (ant.colour % MAX_COLOURS) {
        case 0: return sf::Color(220, 30, 30);
        case 1: return sf::Color(30, 220, 30);
        case 2: return sf::Color(30, 30, 220);
        case 3: return sf::Color(220, 30, 220);

        default: return sf::Color(220, 30, 30);
    }
}

sf::Color SFMLRenderer::antOutlineColor(const Ant& ant) const {
    switch (ant.colour % MAX_COLOURS) {
        case 0: return sf::Color(255, 180, 180);
        case 1: return sf::Color(180, 255, 180);
        case 2: return sf::Color(180, 180, 255);
        case 3: return sf::Color(255, 180, 255);

        default: return sf::Color(255, 180, 180);
    }
}

sf::Color SFMLRenderer::cellColor(const Cell& cell, float /*maxPheromone*/) const {
    switch (cell.type) {
        case CellType::WALL:   return sf::Color(50, 50, 55);
        case CellType::SOURCE: return sf::Color(60, 160, 60);
        case CellType::SINK:   return sf::Color(180, 50, 50);
        case CellType::PUMP:   return sf::Color(200, 130, 30);
        case CellType::SWITCH: return cell.switchActive
                                      ? sf::Color(80, 200, 80)
                                      : sf::Color(100, 100, 110);
        case CellType::PROBE:  return sf::Color(80, 80, 200);
        case CellType::BRIDGE: return sf::Color(80, 160, 200);
        case CellType::PATH:
        default:               return sf::Color(160, 160, 165);
    }
}

// -----------------------------------------------------------------------------
//  drawHex - draw one flat-top hexagon
// -----------------------------------------------------------------------------
void SFMLRenderer::drawHex(sf::RenderTarget& target, sf::Vector2f centre,
                            float size, sf::Color fill, sf::Color outline) {
    sf::ConvexShape hex(6);
    for (int i = 0; i < 6; ++i) {
        float angle = PI / 180.f * 60.f * i;
        hex.setPoint(i, {centre.x + size * std::cos(angle),
                         centre.y + size * std::sin(angle)});
    }
    hex.setFillColor(fill);
    hex.setOutlineColor(outline);
    hex.setOutlineThickness(std::max(0.5f, hexSize_ * 0.04f));
    target.draw(hex);
}

// -----------------------------------------------------------------------------
//  drawGrid
// -----------------------------------------------------------------------------
void SFMLRenderer::drawGrid(const Simulator& sim) {
    const Grid& grid = sim.grid();

    // Find max pheromone for normalisation
    float maxPh = 0.01f;
    if (showPheromone_) {
        for (const auto& [coord, cell] : grid.cells())
            if (!cell.pheromone.empty() && isTravellable(cell.type))
                maxPh = std::max(maxPh, cell.pheromone[0]);
    }

    // Visible area (exclude UI panel)
    float visW = uiPanel_.left;
    float visH = static_cast<float>(window_.getSize().y) - 20.f; // minus status bar
    sf::FloatRect viewBounds(-hexSize_ * 2.f, -hexSize_ * 2.f,
                              visW + hexSize_ * 4.f, visH + hexSize_ * 4.f);

    for (const auto& [coord, cell] : grid.cells()) {
        if (cell.type == CellType::VOID) continue;
        sf::Vector2f centre = hexToScreen(coord);

        // Tight culling
        if (centre.x < viewBounds.left || centre.x > viewBounds.left + viewBounds.width ||
            centre.y < viewBounds.top  || centre.y > viewBounds.top  + viewBounds.height)
            continue;

        sf::Color base = cellColor(cell, maxPh);

        // Pheromone heat-map overlay on traversable cells
        if (showPheromone_ && !cell.pheromone.empty() && isTravellable(cell.type)) {
            float ph = cell.pheromone[0];
            sf::Color phCol = pheromoneColor(ph, maxPh);
            float a = phCol.a / 255.f;
            base.r = static_cast<uint8_t>(base.r * (1.f-a) + phCol.r * a);
            base.g = static_cast<uint8_t>(base.g * (1.f-a) + phCol.g * a);
            base.b = static_cast<uint8_t>(base.b * (1.f-a) + phCol.b * a);
        }

        sf::Color outline(30, 30, 35);
        drawHex(window_, centre, hexSize_ * 0.94f, base, outline);

        if (!fontLoaded_ || hexSize_ < 14.f) continue;

        // Cell label or type icon
        const char* icon = nullptr;
        if (cell.type == CellType::SOURCE || cell.type == CellType::SWITCH)
            icon = (cell.type==CellType::SWITCH && !cell.switchActive) ? "x" : "+";
        else if (cell.type == CellType::SINK)   icon = "-";
        else if (cell.type == CellType::PUMP)   icon = "P";

        const std::string& txt = cell.label.empty() ? (icon ? std::string(icon) : std::string()) : cell.label.substr(0, 5);

        std::string formatted_value = "";
        if (cell.pumpRate[0] > 0)
            formatted_value = std::format(" {:.2f}", cell.pumpRate[0]);

        if (!txt.empty()) {
            sf::Text t(txt + formatted_value, font_, static_cast<unsigned>(std::max(8.f, hexSize_ * 0.45f)));
            t.setFillColor(sf::Color::White);
            auto lb = t.getLocalBounds();
            t.setPosition(centre.x - lb.width/2.f - lb.left,
                          centre.y - lb.height/2.f - lb.top);
            window_.draw(t);
        }
    }

    // Pheromone scale bar (bottom-left), we cannot move this out of this function without repeating redundant work every frame
    if (showPheromone_ && fontLoaded_) {
        rotateWindow(true); 
        float bx = 10.f, by = visH - 28.f, bw = 120.f, bh = 12.f;
        for (int i = 0; i < (int)bw; ++i) {
            float t = i / bw;
            sf::Color c = pheromoneColor(t * maxPh, maxPh);
            c.a = 255;
            sf::RectangleShape seg(sf::Vector2f(1.f, bh));
            seg.setPosition(bx + i, by);
            seg.setFillColor(c);
            window_.draw(seg);
        }
        sf::Text lo("0", font_, 10); lo.setFillColor(sf::Color::White);
        lo.setPosition(bx, by + bh + 2);
        window_.draw(lo);
        std::ostringstream oss; oss << std::fixed << std::setprecision(1) << maxPh;
        sf::Text hi(oss.str(), font_, 10); hi.setFillColor(sf::Color::White);
        hi.setPosition(bx + bw - 20.f, by + bh + 2);
        window_.draw(hi);
        sf::Text label("ph", font_, 10); label.setFillColor(sf::Color(180,180,180));
        label.setPosition(bx + bw/2.f - 5.f, by + bh + 2);
        window_.draw(label);
        rotateWindow(false);
    }
}

// -----------------------------------------------------------------------------
//  drawAnts
// -----------------------------------------------------------------------------
void SFMLRenderer::drawAnts(const Simulator& sim) {
    float r = std::max(2.f, hexSize_ * 0.30f);
    float arrowLen = r * 1.5f;

    sf::CircleShape dot(r);
    dot.setOrigin(r, r);
    dot.setOutlineThickness(std::max(0.5f, r * 0.12f));

    float visW = uiPanel_.left;
    float visH = static_cast<float>(window_.getSize().y);

    for (const Ant& ant : sim.ants()) {
        if (!ant.alive) continue;
        dot.setFillColor(antColor(ant));
        dot.setOutlineColor(antOutlineColor(ant));
        sf::Vector2f pos = hexToScreen(ant.location);
        if (pos.x < -r || pos.x > visW + r || pos.y < -r || pos.y > visH + r)
            continue;

        dot.setPosition(pos);
        window_.draw(dot);

        // Direction arrow (only when zoomed in enough)
        if (hexSize_ >= 12.f && ant.direction != Direction::NONE) {
            int di = dirToInt(ant.direction);
            float angle = static_cast<float>(di) * 60.f * PI / 180.f;
            sf::Vector2f tip(pos.x + arrowLen * std::cos(angle), pos.y - arrowLen * std::sin(angle)); // +Y is down
            sf::Vertex line[2] = {
                sf::Vertex(pos, sf::Color(255, 200, 200)),
                sf::Vertex(tip, sf::Color(255, 100, 100))
            };
            window_.draw(line, 2, sf::Lines);
        }
    }
}

// -----------------------------------------------------------------------------
//  UI panel helpers
// -----------------------------------------------------------------------------
void SFMLRenderer::buildButtons() {
    // Buttons will be created dynamically in drawUI using uiPanel_ dimensions
    buttons_.clear();
}

void SFMLRenderer::handleButtonClick(sf::Vector2f pos, Simulator& sim) {
    for (auto& btn : buttons_) {
        if (btn.rect.contains(pos) && btn.action) {
            btn.action(sim);
            return;
        }
    }
}

// -----------------------------------------------------------------------------
//  drawUI - right-side panel
// -----------------------------------------------------------------------------
void SFMLRenderer::drawUI(const Simulator& sim) {
    // Panel background
    sf::RectangleShape bg(sf::Vector2f(uiPanel_.width, uiPanel_.height));
    bg.setPosition(uiPanel_.left, uiPanel_.top);
    bg.setFillColor(sf::Color(30, 30, 40));
    window_.draw(bg);

    if (!fontLoaded_) return;

    float x = uiPanel_.left + 10.f;
    float y = uiPanel_.top  + 10.f;
    auto label = [&](const std::string& txt, unsigned sz = 14,
                     sf::Color col = sf::Color::White) {
        sf::Text t(txt, font_, sz);
        t.setFillColor(col);
        t.setPosition(x, y);
        window_.draw(t);
        y += sz + 4.f;
    };
    auto hline = [&]() {
        sf::RectangleShape ln(sf::Vector2f(uiPanel_.width - 20.f, 1.f));
        ln.setPosition(x, y);
        ln.setFillColor(sf::Color(80,80,100));
        window_.draw(ln);
        y += 6.f;
    };
    auto button = [&](const std::string& txt, std::function<void(Simulator&)> action) {
        sf::FloatRect rect(x, y, uiPanel_.width - 20.f, 24.f);
        sf::RectangleShape bg2(sf::Vector2f(rect.width, rect.height));
        bg2.setPosition(rect.left, rect.top);
        bg2.setFillColor(sf::Color(60,60,90));
        bg2.setOutlineColor(sf::Color(120,120,160));
        bg2.setOutlineThickness(1.f);
        window_.draw(bg2);

        sf::Text t(txt, font_, 12);
        t.setFillColor(sf::Color::White);
        auto lb = t.getLocalBounds();
        t.setPosition(rect.left + (rect.width - lb.width)/2.f,
                      rect.top  + (rect.height- lb.height)/2.f - lb.top - 2.f);
        window_.draw(t);

        // Rebuild clickable list each frame (cheap)
        buttons_.push_back({rect, txt, std::move(action)});
        y += 28.f;
    };

    buttons_.clear();  // rebuild each frame

    label("ANT-BASED COMPUTER SIM", 15, sf::Color(200,200,255));
    hline();

    // Time step
    label("Step: " + std::to_string(sim.timeStep()), 13, sf::Color(180,220,180));
    label("Ants: " + std::to_string(sim.antCount()), 13, sf::Color(180,220,180));
    label("Speed: " + std::to_string(simSpeed_) + "x", 13, sf::Color(180,220,180));
    y += 4.f;
    hline();

    // Playback controls
    label("CONTROLS", 13, sf::Color(200,200,200));
    button(paused_ ? "  RESUME  (Space)" : "  PAUSE  (Space)",
           [this](Simulator&){ paused_ = !paused_; });
    button("  STEP  (->, while paused)",
           [](Simulator& s){ s.step(); });
    button("  RESET  (R)",
           [](Simulator& s){ s.reset(); });
    y += 4.f;

    std::string speedStr = "Speed: " + std::to_string(simSpeed_) + "x  (+/- keys)";
    label(speedStr, 12, sf::Color(160,160,160));
    y += 4.f;
    hline();

    // Display toggles
    label("DISPLAY", 13, sf::Color(200,200,200));
    button(showPheromone_ ? "[S] Pheromone overlay: ON" : "[S] Pheromone overlay: OFF",
           [this](Simulator&){ showPheromone_ = !showPheromone_; });
    button(showAnts_ ? "[A] Show ants: ON" : "[A] Show ants: OFF",
           [this](Simulator&){ showAnts_ = !showAnts_; });
    y += 4.f;
    hline();

    // Switches
    if (!sim.switches().empty()) {
        label("SWITCHES", 13, sf::Color(200,200,200));
        for (const auto& [lbl, coord] : sim.switches()) {
            bool active = sim.getSwitchActive(lbl);
            std::string btnTxt = (active ? "[ON]  " : "[OFF] ") + lbl;
            button(btnTxt, [lbl, this](Simulator& s){
                std::cout << "Button clicked for switch: " << lbl << "\n";
                s.setSwitchActive(lbl, !s.getSwitchActive(lbl));
            });
        }
        hline();
    }

    // Probe selection
    if (!sim.probes().empty()) {
        label("PROBES  [Tab to cycle]", 13, sf::Color(200,200,200));
        for (const auto& [lbl, rec] : sim.probes()) {
            bool sel = (lbl == selectedProbeLabel_);
            std::string arrow = sel ? "► " : "  ";
            std::ostringstream oss;
            oss << arrow << lbl << std::fixed << std::setprecision(2)
                << " flow=" << rec.currentAntFlow
                << " ph=" << rec.currentPheromone;
            label(oss.str(), 11, sel ? sf::Color(120,255,120) : sf::Color(160,160,160));
        }
    }
}

// -----------------------------------------------------------------------------
//  drawProbeGraph - renders ALL probe ant-flow histories as colored lines
// -----------------------------------------------------------------------------
void SFMLRenderer::drawProbeGraph(const Simulator& sim) {
    if (sim.probes().empty()) return;

    // Place graph inside UI panel, below buttons (~60% down)
    float graphX = uiPanel_.left + 8.f;
    float graphW = uiPanel_.width - 16.f;
    float graphH = 110.f;
    float graphY = window_.getSize().y * 0.55f;

    // Background
    sf::RectangleShape bg(sf::Vector2f(graphW, graphH));
    bg.setPosition(graphX, graphY);
    bg.setFillColor(sf::Color(12, 12, 22));
    bg.setOutlineColor(sf::Color(50, 50, 75));
    bg.setOutlineThickness(1.f);
    window_.draw(bg);

    // Grid lines at 0.25, 0.5, 0.75
    for (float v : {0.25f, 0.5f, 0.75f}) {
        float gy = graphY + graphH * (1.f - v);
        sf::Vertex gl[2] = {
            sf::Vertex(sf::Vector2f(graphX, gy),          sf::Color(40,40,60)),
            sf::Vertex(sf::Vector2f(graphX+graphW, gy),   sf::Color(40,40,60))
        };
        window_.draw(gl, 2, sf::Lines);
    }

    // Color palette for probes
    static const sf::Color PROBE_COLORS[] = {
        sf::Color(80, 220, 80),    sf::Color(220, 200, 50),
        sf::Color(80, 180, 240),   sf::Color(240, 100, 80),
        sf::Color(200, 80, 240),   sf::Color(80, 240, 200),
        sf::Color(240, 160, 60),   sf::Color(180, 240, 80),
    };
    int colorIdx = 0;

    for (const auto& [lbl, rec] : sim.probes()) {
        const auto& hist = rec.antFlowHistory;
        if (hist.size() < 2) { colorIdx++; continue; }

        sf::Color col = PROBE_COLORS[colorIdx % 8];
        colorIdx++;

        sf::VertexArray va(sf::LineStrip, hist.size());
        for (size_t i = 0; i < hist.size(); ++i) {
            float fx = graphX + graphW * i / static_cast<float>(hist.size()-1);
            float fy = graphY + graphH * (1.f - std::min(1.f, hist[i]));
            va[i] = sf::Vertex(sf::Vector2f(fx, fy), col);
        }
        window_.draw(va);
    }

    // Legend
    if (fontLoaded_) {
        sf::Text title("Probe flows", font_, 11);
        title.setFillColor(sf::Color(180,180,200));
        title.setPosition(graphX + 2.f, graphY - 15.f);
        window_.draw(title);

        float lx = graphX, ly = graphY + graphH + 4.f;
        colorIdx = 0;
        for (const auto& [lbl, rec] : sim.probes()) {
            sf::Color col = PROBE_COLORS[colorIdx++ % 8];
            sf::RectangleShape swatch(sf::Vector2f(8.f, 3.f));
            swatch.setPosition(lx, ly + 4.f);
            swatch.setFillColor(col);
            window_.draw(swatch);

            std::ostringstream oss;
            oss << lbl << " " << std::fixed << std::setprecision(2) << rec.currentAntFlow;
            sf::Text t(oss.str(), font_, 9);
            t.setFillColor(col);
            t.setPosition(lx + 10.f, ly);
            window_.draw(t);
            lx += t.getLocalBounds().width + 20.f;

            // Wrap to next line if needed
            if (lx > uiPanel_.left + uiPanel_.width - 20.f) {
                lx = graphX; ly += 14.f;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  drawMiniMap - small overview of the whole circuit in lower-right of main view
// -----------------------------------------------------------------------------
void SFMLRenderer::drawMiniMap(const Simulator& sim) {
    const Grid& grid = sim.grid();
    if (grid.cells().empty()) return;

    // Compute circuit bounding box in axial space
    int minQ = std::numeric_limits<int>::max(),  maxQ = std::numeric_limits<int>::min();
    int minR = std::numeric_limits<int>::max(),  maxR = std::numeric_limits<int>::min();
    for (const auto& [coord, cell] : grid.cells()) {
        if (cell.type == CellType::VOID) continue;
        minQ = std::min(minQ, coord.q); maxQ = std::max(maxQ, coord.q);
        minR = std::min(minR, coord.r); maxR = std::max(maxR, coord.r);
    }
    if (minQ == std::numeric_limits<int>::max()) return;

    // Mini-map panel in bottom-left of main view (above pheromone scale bar)
    float mmW = 160.f, mmH = 100.f;
    float mmX = 8.f;
    float mmY = static_cast<float>(window_.getSize().y) - mmH - 48.f;

    sf::RectangleShape bg(sf::Vector2f(mmW, mmH));
    bg.setPosition(mmX, mmY);
    bg.setFillColor(sf::Color(15, 15, 25, 200));
    bg.setOutlineColor(sf::Color(60, 60, 80));
    bg.setOutlineThickness(1.f);
    window_.draw(bg);

    // Map circuit world-space -> mini-map pixels
    HexCoord hMin; hMin.q = minQ; hMin.r = minR;
    HexCoord hMax; hMax.q = maxQ; hMax.r = maxR;
    PixelPos worldMin = hexToPixel(hMin, 1.f, {0.f, 0.f});
    PixelPos worldMax = hexToPixel(hMax, 1.f, {0.f, 0.f});
    float wW = worldMax.x - worldMin.x + 2.f;
    float wH = worldMax.y - worldMin.y + 2.f;
    float scaleX = (mmW - 4.f) / (wW > 0.f ? wW : 1.f);
    float scaleY = (mmH - 4.f) / (wH > 0.f ? wH : 1.f);
    float mmScale = std::min(scaleX, scaleY);

    for (const auto& [coord, cell] : grid.cells()) {
        if (cell.type == CellType::VOID) continue;
        PixelPos wp = hexToPixel(coord, 1.f, {0.f, 0.f});
        float px = mmX + 2.f + (wp.x - worldMin.x) * mmScale;
        float py = mmY + 2.f + (wp.y - worldMin.y) * mmScale;
        float dotR = std::max(1.f, mmScale * 0.7f);

        sf::Color c = cellColor(cell, 0.f);
        if (cell.type == CellType::PATH || cell.type == CellType::PROBE)
            c = sf::Color(100, 100, 115);

        sf::CircleShape dot(dotR);
        dot.setOrigin(dotR, dotR);
        dot.setPosition(px, py);
        dot.setFillColor(c);
        window_.draw(dot);
    }

    // Viewport rectangle overlay on mini-map
    HexCoord topLeft  = screenToHex(sf::Vector2f(0.f, 0.f));
    HexCoord botRight = screenToHex(sf::Vector2f(uiPanel_.left, static_cast<float>(window_.getSize().y)));

    auto toMM = [&](HexCoord hc) -> sf::Vector2f {
        PixelPos wp = hexToPixel(hc, 1.f, {0.f, 0.f});
        return sf::Vector2f(mmX + 2.f + (wp.x - worldMin.x) * mmScale, mmY + 2.f + (wp.y - worldMin.y) * mmScale);
    };

    sf::Vector2f vTL = toMM(topLeft);
    sf::Vector2f vBR = toMM(botRight);
    float rw = vBR.x - vTL.x;
    float rh = vBR.y - vTL.y;
    if (rw > 1.f && rh > 1.f && rw < mmW * 1.5f && rh < mmH * 1.5f) {
        sf::RectangleShape vp(sf::Vector2f(rw, rh));
        vp.setPosition(vTL);
        vp.setFillColor(sf::Color::Transparent);
        vp.setOutlineColor(sf::Color(200, 200, 100, 200));
        vp.setOutlineThickness(1.f);
        window_.draw(vp);
    }
}

// -----------------------------------------------------------------------------
//  drawStatusBar
// -----------------------------------------------------------------------------
void SFMLRenderer::drawStatusBar(const Simulator& sim) {
    if (!fontLoaded_) return;
    float barH = 20.f;
    float barY = window_.getSize().y - barH;

    sf::RectangleShape bar(sf::Vector2f(uiPanel_.left, barH));
    bar.setPosition(0.f, barY);
    bar.setFillColor(sf::Color(15, 15, 25));
    window_.draw(bar);

    std::ostringstream oss;
    oss << (paused_ ? "[PAUSED] " : "[RUNNING]")
        << "  t=" << sim.timeStep()
        << "  ants=" << sim.antCount()
        << "  zoom=" << std::fixed << std::setprecision(0) << hexSize_
        << "  Speed=" << simSpeed_ << "x"
        << "  rot=" << static_cast<int>(viewRotation_) << "deg"  
        << "  [Space]=pause [F]=fit [S]=ph [A]=ants [R]=reset [[/]]=rotate";                                      
    if (!statusText_.empty()) oss << "  " << statusText_;

    sf::Text txt(oss.str(), font_, 11);
    txt.setFillColor(sf::Color(140, 160, 140));
    txt.setPosition(4.f, barY + 3.f);
    window_.draw(txt);
}

// -----------------------------------------------------------------------------
//  drawWallsBetween
// -----------------------------------------------------------------------------
void SFMLRenderer::drawWallsBetween(const Simulator& sim) {
    const Grid& grid = sim.grid();
    if (grid.cells().empty()) return;

    // To draw only once per wall pair
    std::unordered_set<WallKey, WallKeyHash> drawn;

    for (const auto& [coord, cell] : grid.cells()) {
        if (cell.type == CellType::VOID) continue;
        for (int d = 0; d < 6; ++d) {
            HexCoord nb{ coord.q + DIR_DQ[d], coord.r + DIR_DR[d] };
            if (!grid.hasCell(nb)) continue;
            if (!grid.hasWallBetween(coord, nb)) continue;
            // Ensure we draw each wall only once
            WallKey key(coord, nb);
            if (drawn.count(key)) continue;
            drawn.insert(key);

            sf::Vector2f c1 = hexToScreen(coord);
            sf::Vector2f c2 = hexToScreen(nb);
            sf::Vector2f mid = (c1 + c2) / 2.f;
            sf::Vector2f dir = c2 - c1;
            sf::Vector2f perp(-dir.y, dir.x);
            float len = std::sqrt(perp.x * perp.x + perp.y * perp.y);
            if (len < 0.001f) continue;
            perp /= len; // normalize
            float halfSide = hexSize_ / 2.f;
            sf::Vector2f p1 = mid + perp * halfSide;
            sf::Vector2f p2 = mid - perp * halfSide;

            sf::Vertex line[] = {
                sf::Vertex(p1, sf::Color::Red),
                sf::Vertex(p2, sf::Color::Red)
            };
            window_.draw(line, 2, sf::Lines);
        }
    }
}
