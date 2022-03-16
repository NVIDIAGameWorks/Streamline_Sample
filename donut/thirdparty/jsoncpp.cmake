set(JSONCPP_WITH_TESTS OFF CACHE BOOL "")
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "")
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "")
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "")

set(__tmp_shared_libs ${BUILD_SHARED_LIBS})
set(__tmp_static_libs ${BUILD_STATIC_LIBS})

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

add_subdirectory(jsoncpp)

set(BUILD_SHARED_LIBS ${__tmp_shared_libs})
set(BUILD_STATIC_LIBS ${__tmp_static_libs})

add_library(jsoncpp INTERFACE IMPORTED GLOBAL)
target_link_libraries(jsoncpp INTERFACE jsoncpp_lib_static)
#target_include_directories(jsoncpp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/include)
