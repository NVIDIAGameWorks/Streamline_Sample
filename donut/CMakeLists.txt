project(donut)
#cmake_minimum_required(VERSION 3.11.1)
cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set_property( GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()

string(APPEND CMAKE_CXX_FLAGS_DEBUG " -D_DEBUG")

include(CMakeDependentOption)

cmake_dependent_option(DONUT_WITH_DX11 "Enable the DX11 version of Donut" ON "WIN32" OFF)
cmake_dependent_option(DONUT_WITH_DX12 "Enable the DX12 version of Donut" ON "WIN32" OFF)
option(DONUT_WITH_VULKAN "Enable the Vulkan version of Donut" OFF)
option(DONUT_USE_PACKMAN_VULKAN "Use a vulkan SDK from packman; disable to use a locally installed SDK" ON)

option(DONUT_WITH_ASSIMP "Include ASSIMP+" OFF)
option(DONUT_WITH_AUDIO "Include Audio features (XAudio2)" OFF)
cmake_dependent_option(DONUT_WITH_HBAO_PLUS "Include HBAO+" ON "WIN32" OFF)
option(DONUT_WITH_LZ4 "Include LZ4" ON)
option(DONUT_WITH_SL "Use SL integration" ON)
cmake_dependent_option(DONUT_WITH_NVAPI "Include NVAPI" ON "WIN32" OFF)
set(DONUT_WITH_OCULUS_VR OFF)
option(DONUT_WITH_RTAO "Include RTAO" OFF)
option(DONUT_WITH_RT_REFLECTIONS "Include the GameWorks RTReflections library" OFF)
option(DONUT_WITH_SQLITE "Include SQLite" ON)
set(DONUT_WITH_VOLUME_LIGHTING OFF)

option(DONUT_WITH_TOOLS "Include DCC pipeline tools" OFF)

if (DEFINED DEPENDENCIES_ROOT AND DONUT_USE_PACKMAN_VULKAN)
    if (WIN32)
        file(TO_CMAKE_PATH ${DEPENDENCIES_ROOT}/chk/vulkansdk/1.1.106.0-windows-x86_64 VULKAN_PACKMAN_PATH)
    else()
        file(TO_CMAKE_PATH ${DEPENDENCIES_ROOT}/chk/vulkansdk/1.1.130.0-linux-x86_64 VULKAN_PACKMAN_PATH)
    endif()
    set(Vulkan_SDK_ROOT_DIR ${VULKAN_PACKMAN_PATH})
    install(DIRECTORY ${VULKAN_PACKMAN_PATH} DESTINATION "donut/thirdparty/chk/vulkansdk"
        USE_SOURCE_PERMISSIONS PATTERN "*.pdb" EXCLUDE
    )
endif()

add_subdirectory(thirdparty)

# warning: thirdparty must be included before nvrhi because that folder defines the 'nvapi' target
add_subdirectory(nvrhi)

# set up for compileshaders.cmake
if (DEFINED DEPENDENCIES_ROOT)
    file(TO_CMAKE_PATH ${DEPENDENCIES_ROOT}/chk/python/3.7.0-windows-x64 PYTHON_PACKMAN_PATH)
    if (EXISTS "${PYTHON_PACKMAN_PATH}")
    	set(Python_ROOT_DIR ${PYTHON_PACKMAN_PATH})
    endif()
endif()
find_package(Python 3 COMPONENTS Interpreter REQUIRED)

set(DONUT_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include CACHE PATH "" FORCE)

if(DONUT_WITH_DX12)
	set(DONUT_USE_DXIL_ON_DX12 TRUE CACHE BOOL "Use DXC to compile DXIL shaders on DX12 - otherwise FXC and DXBC")
endif()

add_subdirectory(shaders)

include(donut-core.cmake)
include(donut-engine.cmake)
include(donut-render.cmake)
include(donut-app.cmake)

if(DONUT_WITH_FEATURE_DEMO)
    add_subdirectory(feature_demo)
endif()

if(DONUT_WITH_TOOLS)
    add_subdirectory(tools)
endif()

install(DIRECTORY src;include;cmake;scripts DESTINATION "donut")
file(GLOB CMAKE_FILES *.cmake)
install(FILES CMakeLists.txt;${CMAKE_FILES} DESTINATION "donut")
#TODO packman directories
