# 1. Create a build directory
mkdir build 
cd build

# 2. Configure
cmake ..                    # with SFML graphics (default)
# OR if you don't have SFML:
# cmake .. -DBUILD_GRAPHICS=OFF

# 3. Build
 make -j4                  # Linux/macOS
# OR on Windows: # (UNTESTED)
#cmake --build . --config Release

# 4. Run the tests (should print 13 passed)
./ant_tests

# 5. Run the simulator
./ant_simulator # runs with built-in inverter
./ant_simulator circuits/inverter_classic.circuit # runs explicit circuit file