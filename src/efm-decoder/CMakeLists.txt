cmake_minimum_required(VERSION 3.16)

# Set the project name
project(efm-tools)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Git branch and commit are defined in the parent CMakeLists.txt

# Add subdirectories
add_subdirectory(libs/efm)
add_subdirectory(tools/efm-decoder-f2)
add_subdirectory(tools/efm-decoder-d24)
add_subdirectory(tools/efm-decoder-audio)
add_subdirectory(tools/efm-decoder-data)
add_subdirectory(tools/efm-stacker-f2)
add_subdirectory(tools/vfs-verifier)