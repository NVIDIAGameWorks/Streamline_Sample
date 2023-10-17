#
# Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.10)

project(StreamlineSample)

# Variables
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/_bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/framework")
if (MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_ITERATOR_DEBUG_LEVEL=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()
set(PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/_package")

# Overide donut options
option(STREAMLINE_FEATURE_DLSS_SR   "Include DLSS-SR dll"   ON)
option(STREAMLINE_FEATURE_IMGUI     "Include Imgui dll"     ON)
option(STREAMLINE_FEATURE_REFLEX    "Include Reflex dll"    ON)
option(STREAMLINE_FEATURE_NIS       "Include NIS dll"       ON)
option(STREAMLINE_FEATURE_DLSS_FG   "Include DLSS-FG dll"   ON)

set(STREAMLINE_INSTALL_DIR  "${PACKAGE_DIRECTORY}/_bin" CACHE STRING "Streamline Install Dir")

# Add libraries
add_subdirectory(donut)
add_subdirectory(streamline)

# Apply Patch for VK support
set(success OFF)
find_package(Git)
message(STATUS "Found Git for patching: ${GIT_EXECUTABLE}")
if(Git_FOUND)
    set(status 1)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_SOURCE_DIR}/VKpatch.patch 
        RESULT_VARIABLE status
        ERROR_QUIET
        )
    if(${status} EQUAL "0")
        set(success ON)
        message(STATUS "Successfully applied VKPatch.patch")
    endif()
endif()
if(NOT success)
    message("WARNING: Failed to apply VKPatch.patch. It is possible patch has already been applied, otherwise Streamline may fail with Vulkan.")
endif()


# Remove d3d linking from donut and relink with streamline
get_target_property(TARGET_LIBRARIES donut_app LINK_LIBRARIES)
LIST(REMOVE_ITEM TARGET_LIBRARIES d3d11)
LIST(REMOVE_ITEM TARGET_LIBRARIES d3d12)
LIST(REMOVE_ITEM TARGET_LIBRARIES dxgi)
set_property(TARGET donut_app PROPERTY LINK_LIBRARIES  ${TARGET_LIBRARIES} )

# Collect Code
file(GLOB src_header
    "src/StreamlineSample.h"
    "src/SLWrapper.h"
    "src/UIRenderer.h"
    "src/RenderTargets.h"
    "src/UIData.h"
    )
file(GLOB src_source
    "src/main.cpp"
    "src/StreamlineSample.cpp"
    "src/SLWrapper.cpp"
    )
file(GLOB src_overrides 
    "src/DeviceManagerOverride/DeviceManagerOverride.h"
    "src/DeviceManagerOverride/DeviceManagerOverride_DX11.cpp"
    "src/DeviceManagerOverride/DeviceManagerOverride_DX12.cpp"
    "src/DeviceManagerOverride/DeviceManagerOverride_VK.cpp"
    )

# Create exe and link
add_executable(StreamlineSample WIN32 ${src_header} ${src_source} ${src_overrides})
source_group("Headers"              FILES ${src_header})
source_group("Sources"              FILES ${src_source})
source_group("DeviceManagerOverride"       FILES ${src_overrides})

# Add AGS
option(AMD_AGS "Add AMD AGS support" OFF)
if(AMD_AGS)
    set(AGS_DIR "${CMAKE_SOURCE_DIR}/amd_ags/ags_lib")
    find_library(AGS_LIB amd_ags_x64 HINTS "${AGS_DIR}/lib" NO_CACHE)
    if(AGS_LIB)
        find_path(AGS_INCLUDE_DIR "amd_ags.h" HINTS "${AGS_DIR}/inc" NO_CACHE)
        find_file(AGS_DLL "amd_ags_x64.dll" HINTS "${AGS_DIR}/lib" NO_CACHE)
        add_library(amd_ags STATIC IMPORTED GLOBAL)
        set_target_properties(amd_ags
            PROPERTIES
            IMPORTED_LOCATION "${AGS_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${AGS_INCLUDE_DIR}")
        add_compile_definitions(AGS_ENABLE)
        install(FILES ${AGS_DLL} DESTINATION "${STREAMLINE_INSTALL_DIR}")
    else()
        comment("Warning: Failed to find AMD AGS")
    endif()
    target_link_libraries(StreamlineSample amd_ags)

    if (WIN32)
        add_custom_target(CopyAGSdll ALL DEPENDS ${AGS_DLL})
        add_custom_command(TARGET CopyAGSdll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AGS_DLL} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            COMMENT "Copied DLL: ${AGS_DLL}")
        add_dependencies(StreamlineSample CopyAGSdll)
    endif ()
endif()

# NDA ONLY DLSS-G DLSS_G Release
add_compile_definitions(DLSSG_ALLOWED)
# END NDA ONLY DLSS-G DLSS_G Release
target_link_libraries(StreamlineSample donut_render donut_app donut_engine streamline)
set_target_properties(StreamlineSample PROPERTIES FOLDER "Streamline Sample")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT StreamlineSample)

# Install
install(TARGETS StreamlineSample DESTINATION "${PACKAGE_DIRECTORY}/_bin")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/_bin/shaders" DESTINATION "${PACKAGE_DIRECTORY}/_bin")
install(FILES "${CMAKE_SOURCE_DIR}/media/sponza-plus.scene.json" DESTINATION "${PACKAGE_DIRECTORY}/media")
install(FILES "${CMAKE_SOURCE_DIR}/media/fonts/DroidSans/DroidSans-Mono.ttf" DESTINATION "${PACKAGE_DIRECTORY}/media/fonts/DroidSans")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/media/glTF-Sample-Models/2.0/Sponza" DESTINATION "${PACKAGE_DIRECTORY}/media/glTF-Sample-Models/2.0")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/media/glTF-Sample-Models/2.0/BrainStem" DESTINATION "${PACKAGE_DIRECTORY}/media/glTF-Sample-Models/2.0")
install(FILES "${CMAKE_SOURCE_DIR}/run.bat" DESTINATION "${PACKAGE_DIRECTORY}")
