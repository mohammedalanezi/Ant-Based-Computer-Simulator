# Ant-Based Computer Simulator

A faithful C++17 implementation of the ant-based computing model from:

- **Michael, L. (2009).** Ant-Based Computing. *Artificial Life*, 15(3):337-349.
- **Michael, L. & Yiannakides, A. (2012).** An Ant-Based Computer Simulator. *AAAI*.

Ants navigate a flat-top hexagonal grid following pheromone gradients, collectively implementing universal computation. The simulator rigorously implements every algorithm in the papers.

---

## Quick Start

```bash
# Build (headless only - no SFML needed)
mkdir build && cd build
cmake .. -DBUILD_GRAPHICS=OFF
make -j4
./ant_tests                              # 13 unit tests - all should pass
./ant_headless                           # built-in inverter → CSV on stdout
./ant_headless circuits/inverter.circuit 800 out.csv

# Build with SFML graphics
sudo apt install libsfml-dev             # Ubuntu/Debian
cmake .. -DBUILD_GRAPHICS=ON
make -j4
./ant_simulator                          # built-in inverter, interactive
./ant_simulator circuits/inverter.circuit
```

---

## Project Layout

```
ant_simulator/
├── src/
│   ├── core/
│   │   ├── Types.h          # CellType enum, Direction (6-way hex), AntID
│   │   └── HexCoord.h       # Axial (q,r) hex coords, pixel↔hex, neighbour arcs
│   ├── simulator/
│   │   ├── SimConfig.h      # All model parameters (Table 1, Michael 2009)
│   │   ├── Cell.h           # Per-cell data: type, pheromone[], pump rates, probes
│   │   ├── Ant.h            # Ant agent: location, direction, claim (collision step)
│   │   ├── Grid.h/.cpp      # Sparse hex grid, pheromone dynamics, inter-cell walls
│   │   └── Simulator.h/.cpp # CHOOSEACTION + COLLISIONHANDLING loop, probes, switches
│   ├── circuit/
│   │   └── CircuitLoader.h/.cpp  # .circuit text-file parser
│   ├── graphics/
│   │   ├── IRenderer.h           # Abstract renderer - swap 2D/3D without rewrite
│   │   └── SFMLRenderer.h/.cpp   # SFML2 renderer: hex grid, ants, pheromone heatmap,
│   │                             #   probe graphs, mini-map, switch buttons
│   ├── main.cpp             # Graphical entry point
│   └── headless_main.cpp    # CLI runner → CSV probe output
├── circuits/
│   ├── inverter_classic.circuit  # Single inverter 
│   ├── bridge_test.circuit       # tests 3 paths into a single bridge
│   ├── boost_inverter.circuit    # Inverter used for boosting
│   ├── inverter_sensitive.circuit# Inverter that is more sensitive to low inputs
│   ├── inverter_resistant.circuit# Inverter that is more resistent to low inputs
│   └── nor.circuit               # NOR gate (wire-merge + inverter)
├── tests/
│   └── test_main.cpp        # 13 self-contained unit tests (no external framework)
└── CMakeLists.txt
```

---

## Graphical Controls

| Key / Mouse        | Action                                      |
|--------------------|---------------------------------------------|
| **Space**          | Pause / Resume                              |
| **->** (Right)      | Single step (while paused)                  |
| **+** / **-**      | Increase / decrease simulation speed (1-256×)|
| **F**              | Auto-fit view to circuit bounding box       |
| **S**              | Toggle pheromone heat-map overlay           |
| **A**              | Toggle ant rendering                        |
| **Tab**            | Cycle highlighted probe in UI list          |
| **R**              | Reset simulation                            |
| **Scroll**         | Zoom in / out (centred on cursor)           |
| **Drag LMB**       | Pan view                                    |
| **[/]**       | Rotate view 30 degrees                              |
| **0**              | Reset view to origin                        |
| **Click UI button**| Toggle switches, select probes              |

---

## Circuit File Format

```
# Lines starting with # are comments
PARAM  spawn_prob    0.95      # Override any SimConfig field
PARAM  nonlinearity  30.0
PARAM  threshold     6.0
PARAM  epsilon       0.1
PARAM  secretion     12.0
PARAM  dissipation   0.10
PARAM  diffusion     0.10
PARAM  probe_window  30

PATH   q r                     # Traversable path cell
WALL   q r                     # Solid wall (ants cannot enter; pheromone blocked)
SOURCE q r  DIR                # Ant source; DIR ∈ {E NE NW W SW SE}
SINK   q r                     # Ant sink (removes entering ants)
PUMP   q r  rate               # Pheromone pump (species 0), rate units/step
SWITCH q r  DIR  label         # Controllable source (toggle in UI or code)
PROBE  q r  label              # Measurement probe (ant-flow graph in UI)
BRIDGE q r                     # Bridge (two crossing paths without mixing)
WALL_BETWEEN q1 r1  q2 r2      # Inter-cell wall: blocks ant movement AND diffusion
INIT_ANT  q r  DIR             # Place an ant facing DIR at simulation start
PHEROMONE q r  amount          # Set initial pheromone at cell (species 0)
```

---

## Verified Inverter Topology

```
Source(-4,0) → (-3,0) → (-2,0) → (-1,0) → Junction(0,0)
                                                 │E→  Pump-B(1,0) → Output(3,0) → Sink_out
                                                 └SE→ Sink-B(0,1)

Input(-4,1) → (-3,1) → (-2,1) → Pump-A(-1,1) → Sink-B(0,1)
```

**Mechanism:**
- **Input=0:** P(Pump-B) ≈ 1.37 > P(Sink-B) ≈ 1.33 → source ants prefer Output → **Output=HIGH (~0.7)**
- **Input=1:** Ants at Pump-A secrete (P > T+ε); P(Sink-B) rises to ~6 >> P(Pump-B) → source ants divert to Sink-B → **Output=LOW (~0)**
- **Recovery:** When Input switches OFF, P decays via dissipation → Output returns to HIGH within ~100 steps

---

## Algorithm Implementations

### CHOOSEACTION (Michael 2009, Figure 1 / Michael & Yiannakides 2012, Fig 1)
1. Identify R(LC, DC): up to 3 cells in the front arc, filtered by walls and cell type  
2. Sense P(L) for each L ∈ {LC} ∪ R  
3. Conditional secretion: if P(LC) ≥ T+ε AND ∃L∈R: P(L) ≤ T−ε → secrete s units at LC  
4. Choose LN ∈ R with probability P(LN)^n / Σ P(L)^n  
5. (Deferred to collision handler) Move to LN

### COLLISIONHANDLING (Michael & Yiannakides 2012, Figure 4)
List E holds cells that are empty (in new state SN) and claimed by ≥1 ant.  
Each iteration: pop LN from E, choose one claimant randomly, move it to LN, free its source LC (add to E if claimed). Theorem 1 guarantees termination, safety (no two ants same cell), and liveness (all claimed empty cells fill).

### Pheromone Dynamics
```
P^t(L) = (1−d) · [(1−f)·P^{t−1}(L) + f·W^{t−1}(L)] + s^{t−1}(L) + p(L)
```
where W^t(L) = average of P at L and its non-walled neighbours (diffusion average), d = dissipation rate, f = diffusion rate, s = ant secretions, p = pump rate.  
**Theorem 2** (upper bound): P^t(L) ≤ (s + p_max) / d for all L, t when d > 0.

---

## Extensibility

| Goal | How |
|---|---|
| **Multiple pheromone species** | Set `SimConfig::numPheromoneSpecies > 1`. All Cell/Grid/Simulator code already uses `vector<float>` indexed by species. |
| **3D rendering** | Implement `IRenderer` with an OpenGL/Vulkan backend; pass to the main loop. |
| **New cell types** | Add to `CellType` enum, handle in `CircuitLoader::parseLine()`, `Grid::reachableCells()`, and the renderer. |
| **Step hooks / logging** | `sim.setPreStepCallback(fn)` / `sim.setPostStepCallback(fn)`. |

Created by Mohammed Al-Anezi, may produce future work with this ant model, stay tuned.
| **Parallel collision** | The `COLLISIONHANDLING` algorithm in `Simulator::substepCollisionHandle()` is the natural parallelisation point (noted as future work in the 2012 paper). |
| **Additional circuits** | Write a `.circuit` text file; the loader handles all topology. |
