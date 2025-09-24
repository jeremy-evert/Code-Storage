cmake_minimum_required(VERSION 3.15)
project(Perudo)

set(CMAKE_CXX_STANDARD 17)

# Add include directories
include_directories(include)

# ---------------------------
# Client executable (with graphics)
# ---------------------------
add_executable(PerudoGame
    src/main.cpp
    src/ResourceManager.cpp
    src/SeatManager.cpp
    src/Client.cpp
)

# ---------------------------
# Server executable (headless)
# ---------------------------
add_executable(PerudoServer
    src/Server.cpp
    src/ServerMain.cpp    # <-- tiny main() that just starts the server
)

# Point CMake to your SFML installation
set(SFML_DIR "C:/SFML/lib/cmake/SFML")

# Find SFML modules
find_package(SFML 2.5 COMPONENTS graphics window system network audio REQUIRED)

# Link client
target_link_libraries(PerudoGame
    sfml-graphics
    sfml-window
    sfml-system
    sfml-network
    sfml-audio
)

# Link server (needs only system + network)
target_link_libraries(PerudoServer
    sfml-system
    sfml-network
)
