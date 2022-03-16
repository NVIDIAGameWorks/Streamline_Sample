project(nvrhi)
cmake_minimum_required(VERSION 3.7)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(thirdparty)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(Vulkan)

set(include_common
    include/nvrhi/nvrhi.h
    include/nvrhi/utils.h
    include/nvrhi/common/alloc.h
    include/nvrhi/common/containers.h
	include/nvrhi/common/stacktrace.h)

set(src_common
    src/common/alloc.cpp
    src/common/shader-blob.cpp
    src/common/crc.cpp
	src/common/stacktrace.cpp
    src/common/utils.cpp)

set(include_validation
    include/nvrhi/validation/validation.h)
set(src_validation
    src/validation/device.cpp
    src/validation/commandlist.cpp)

set(include_d3d11
    include/nvrhi/d3d11/d3d11.h)
set(src_d3d11
    src/d3d11/d3d11.cpp
    src/d3d11/resourcebindings.cpp
    src/d3d11/queries.cpp)

set(include_d3d12
    include/nvrhi/d3d12/d3d12.h
	include/nvrhi/d3d12/internals.h)
set(src_d3d12
    src/d3d12/d3d12.cpp
    src/d3d12/dxr.cpp
    src/d3d12/commandlist.cpp)

set(include_vk
    include/nvrhi/vulkan.h
    include/nvrhi/vulkan/allocator.h
    include/nvrhi/vulkan/constants.h
    include/nvrhi/vulkan/context.h
    include/nvrhi/vulkan/queue.h
    include/nvrhi/vulkan/renderer.h
    include/nvrhi/vulkan/sync.h
    include/nvrhi/vulkan/resources.h)

set(src_vk
    src/vulkan/allocator.cpp
    src/vulkan/buffer.cpp
    src/vulkan/compute.cpp
    src/vulkan/constants.cpp
    src/vulkan/draw.cpp
    src/vulkan/queries.cpp
    src/vulkan/queue.cpp
    src/vulkan/renderer.cpp
    src/vulkan/resourcebindings.cpp
    src/vulkan/shader.cpp
    src/vulkan/stagingtexture.cpp
    src/vulkan/sync.cpp
    src/vulkan/texture.cpp
    src/vulkan/vulkan_ext.c)

add_library(nvrhi INTERFACE)
target_include_directories(nvrhi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (WIN32)
    option(NVRHI_WITH_DXR "Build NVRHI with DirectX Raytracing support" OFF)

    add_library(nvrhi_d3d11 STATIC EXCLUDE_FROM_ALL
                ${include_common}
                ${src_common}
                ${include_validation}
                ${src_validation}
                ${include_d3d11}
                ${src_d3d11})
    target_link_libraries(nvrhi_d3d11)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/StackWalker/CMakeLists.txt)
        add_definitions( -DSTACKWALKER_ENABLED )
        target_link_libraries(nvrhi_d3d11 StackWalker)
    endif()

    target_include_directories(nvrhi_d3d11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(nvrhi_d3d11 PRIVATE
                                USE_DX=1
                                USE_DX11=1
                                USE_DX12=0
                                USE_VK=0
                                NOMINMAX)

    add_library(nvrhi_d3d12 STATIC EXCLUDE_FROM_ALL
                ${include_common}
                ${src_common}
                ${include_validation}
                ${src_validation}
                ${include_d3d12}
                ${src_d3d12})
    target_link_libraries(nvrhi_d3d12)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/StackWalker/CMakeLists.txt)
        add_definitions( -DSTACKWALKER_ENABLED )
        target_link_libraries(nvrhi_d3d12 StackWalker)
    endif()

    target_include_directories(nvrhi_d3d12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(nvrhi_d3d12 PRIVATE
                                USE_DX=1
                                USE_DX11=0
                                USE_DX12=1
                                USE_VK=0
                                NVRHI_D3D12_WITH_NVAPI=0
                                NOMINMAX)

    if(TARGET nvapi)
        target_link_libraries(nvrhi_d3d11 nvapi)
        target_link_libraries(nvrhi_d3d12 nvapi)
        target_compile_definitions(nvrhi_d3d11 PRIVATE NVRHI_D3D11_WITH_NVAPI=1)
        target_compile_definitions(nvrhi_d3d12 PRIVATE NVRHI_D3D12_WITH_NVAPI=1)
    else()
        target_compile_definitions(nvrhi_d3d11 PRIVATE NVRHI_D3D11_WITH_NVAPI=0)
        target_compile_definitions(nvrhi_d3d12 PRIVATE NVRHI_D3D12_WITH_NVAPI=0)
    endif() # nvapi

    if(NVRHI_WITH_DXR)
        target_compile_definitions(nvrhi_d3d12 PUBLIC NVRHI_WITH_DXR=1)
    else()
        target_compile_definitions(nvrhi_d3d12 PUBLIC NVRHI_WITH_DXR=0)
    endif()
    
    set_target_properties(nvrhi_d3d11 PROPERTIES FOLDER NVRHI)
    set_target_properties(nvrhi_d3d12 PROPERTIES FOLDER NVRHI)
endif() # WIN32

if(Vulkan_FOUND)

    add_library(nvrhi_vk STATIC EXCLUDE_FROM_ALL
                ${include_common}
                ${src_common}
                ${include_validation}
                ${src_validation}
                ${include_vk}
                ${src_vk})
    target_link_libraries(nvrhi_vk ${Vulkan_LIBRARIES})

    if (WIN32)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/StackWalker/CMakeLists.txt)
            add_definitions( -DSTACKWALKER_ENABLED )
            target_link_libraries(nvrhi_vk StackWalker)
        endif()
    endif()

    target_include_directories(nvrhi_vk PUBLIC ${Vulkan_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(nvrhi_vk PRIVATE
                               USE_DX=0
                               USE_DX11=0
                               USE_DX12=0
                               USE_VK=1
                               NOMINMAX)

    if (UNIX)
        target_compile_definitions(nvrhi_vk PRIVATE LINUX)
    endif()

    set_target_properties(nvrhi_vk PROPERTIES FOLDER NVRHI)
endif() # Vulkan_FOUND

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /MP")

    # workaround for warning C4714: function 'std::string vk::to_string(vk::Result)' marked as __forceinline not inlined
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4714")
endif()

install(DIRECTORY cmake;include;src DESTINATION "donut/nvrhi")
install(FILES CMakeLists.txt DESTINATION "donut/nvrhi")
