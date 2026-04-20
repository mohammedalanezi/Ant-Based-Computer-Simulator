cmake_minimum_required(VERSION 3.16)
project(AntSimulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GRAPHICS "Build the SFML graphical front-end" ON)
option(BUILD_TESTS    "Build unit tests"                   ON)
option(BUILD_HEADLESS "Build headless CLI runner"          ON)

if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# -- Core simulator library ----------------------------------------------------
add_library(ant_core STATIC
    src/simulator/Grid.cpp
    src/simulator/Simulator.cpp
    src/circuit/CircuitLoader.cpp
)
target_include_directories(ant_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -- Headless CLI runner -------------------------------------------------------
if(BUILD_HEADLESS)
    add_executable(ant_headless src/headless_main.cpp)
    target_link_libraries(ant_headless PRIVATE ant_core)
endif()

# -- Graphical front-end (SFML) ------------------------------------------------
if(BUILD_GRAPHICS)
    find_package(SFML 2.5 COMPONENTS graphics window system QUIET)
    if(SFML_FOUND)
        add_executable(ant_simulator
            src/main.cpp
            src/graphics/SFMLRenderer.cpp
        )
        target_link_libraries(ant_simulator PRIVATE ant_core
            sfml-graphics sfml-window sfml-system)
        target_include_directories(ant_simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        message(STATUS "SFML found – graphical build enabled")
    else()
        message(WARNING "SFML not found – graphical build disabled. Install: sudo apt install libsfml-dev")
    endif()
endif()

# -- Unit tests ----------------------------------------------------------------
if(BUILD_TESTS)
    add_executable(ant_tests tests/test_main.cpp)
    target_link_libraries(ant_tests PRIVATE ant_core)
    enable_testing()
    add_test(NAME ant_tests COMMAND ant_tests)
endif()

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/circuits DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
