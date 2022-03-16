if (NOT DEFINED DEPENDENCIES_ROOT)
    message(FATAL_ERROR "Packman package root not found --- please run packman or set PM_PACKAGES_ROOT")
endif()

include(imgui.cmake)
include(jsoncpp.cmake)
include(stb.cmake)

# Required DXC dependencies
if (WIN32)
    set(dxc_package_version "win32-5bf6dd18")
else()
    set(dxc_package_version "linux-x86_64-7f7a2f1c")
endif()
include(dxc.cmake)
STRING(REGEX REPLACE "\\\\" "/" __dxc_package_path ${__dxc_package_path})
install(DIRECTORY ${__dxc_package_path} DESTINATION "donut/thirdparty/chk/dxc" USE_SOURCE_PERMISSIONS PATTERN "*.pdb" EXCLUDE)

option(GLFW_BUILD_EXAMPLES "" OFF)
option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)
add_subdirectory(glfw)

set(third_party_folder "Third-Party Libraries")
set_target_properties(glfw PROPERTIES FOLDER ${third_party_folder})
set_target_properties(imgui PROPERTIES FOLDER ${third_party_folder})
set_target_properties(jsoncpp_lib_static PROPERTIES FOLDER ${third_party_folder})

# Optional dependencies

if(DONUT_WITH_ASSIMP)
if (WIN32)
    set(assimp_package_version "4.1.0-win32-x64")
else()
    set(assimp_package_version "4.1.0-linux-x86_64")
endif()
    include(assimp.cmake)
    STRING(REGEX REPLACE "\\\\" "/" __assimp_package_path ${__assimp_package_path})
    install(DIRECTORY ${__assimp_package_path} DESTINATION "donut/thirdparty/chk/assimp" USE_SOURCE_PERMISSIONS)
endif()

if(DONUT_WITH_HBAO_PLUS AND WIN32)
    set(hbao_plus_package_version "p4sw-4.0.0.24284062-win32-x64")
    include(hbao_plus.cmake)
    STRING(REGEX REPLACE "\\\\" "/" __hbao_plus_package_path ${__hbao_plus_package_path})
    install(DIRECTORY ${__hbao_plus_package_path} DESTINATION "donut/thirdparty/chk/hbao_plus")
endif()

if(DONUT_WITH_LZ4)
    include(lz4.cmake)
    set_target_properties(lz4 PROPERTIES FOLDER ${third_party_folder})

    install(DIRECTORY lz4 DESTINATION "donut/thirdparty" USE_SOURCE_PERMISSIONS)
endif()

if(DONUT_WITH_SL)
    include(sl.cmake)
endif()

if(DONUT_WITH_NVAPI AND WIN32)
    set(nvapi_package_version "donut-p4sw-24219035-win32-x64")
    include(nvapi.cmake)
    STRING(REGEX REPLACE "\\\\" "/" __nvapi_package_path ${__nvapi_package_path})
    install(DIRECTORY ${__nvapi_package_path} DESTINATION "donut/thirdparty/chk/nvapi")
endif()

if(DONUT_WITH_RTAO)
    set(rtao_package_version "1.1")
    include(rtao.cmake)
    STRING(REGEX REPLACE "\\\\" "/" rtao_package_path ${rtao_package_path})
    install(DIRECTORY ${rtao_package_path} DESTINATION "donut/thirdparty/chk/rtao" USE_SOURCE_PERMISSIONS)
endif()

if(DONUT_WITH_RT_REFLECTIONS)
    set(rtreflections_package_version "1.103")
    include(rtreflections.cmake)
    STRING(REGEX REPLACE "\\\\" "/" rtreflections_package_path ${rtreflections_package_path})
    install(DIRECTORY ${rtreflections_package_path} DESTINATION "donut/thirdparty/chk/gfsdk_rtreflections" USE_SOURCE_PERMISSIONS)
endif()

if(DONUT_WITH_SQLITE)
    add_subdirectory(sqlite)
    set_target_properties(sqlite PROPERTIES FOLDER ${third_party_folder})
    install(DIRECTORY sqlite DESTINATION "donut/thirdparty" USE_SOURCE_PERMISSIONS)
endif()

file(GLOB CMAKE_FILES *.cmake)
install(FILES CMakeLists.txt;${CMAKE_FILES} DESTINATION "donut/thirdparty")
install(DIRECTORY glfw;imgui;jsoncpp;stb DESTINATION "donut/thirdparty")
STRING(REGEX REPLACE "/" "\\\\\\\\" PACKMAN_DEPENDENCIES_PATH "${CMAKE_INSTALL_PREFIX}donut/thirdparty")
install(CODE "execute_process(COMMAND cmd /c ${PROJECT_SOURCE_DIR}/packman/remove_packman_sha1s.bat ${PACKMAN_DEPENDENCIES_PATH})")
#todo remove packman sha1s
