cmake_minimum_required(VERSION 3.12)

project(sl_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(DONUT_WITH_FEATURE_DEMO "Include the FeatureDemo project" OFF)
option(DONUT_WITH_ASSIMP "" OFF)

set(DEPENDENCIES_ROOT "${CMAKE_SOURCE_DIR}/donut/thirdparty")

add_subdirectory(donut)
add_subdirectory(sl_demo)

add_subdirectory(PostInstall)
