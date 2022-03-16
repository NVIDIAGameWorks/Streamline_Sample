# Try to find Assimp library and include path.
# Once done this will define
#
# ASSIMP_FOUND
# ASSIMP_INCLUDE_DIR
# ASSIMP_LIBRARIES
#

if (NOT ASSIMP_PACKMAN_VER)
    set(ASSIMP_PACKMAN_VER "4.1.0-win32-x64")
endif()

find_path(ASSIMP_INCLUDE_DIR
    NAMES
        assimp/version.h
    PATHS
        "${ASSIMP_LOCATION}/include"
        "$ENV{ASSIMP_LOCATION}/include"
        "${DEPENDENCIES_ROOT}/chk/assimp/${ASSIMP_PACKMAN_VER}/include"
)

if (WIN32)

#    message("MSVC_VERSION ${MSVC_VERSION}")

    if (MSVC_VERSION GREATER_EQUAL 1900)
        set(MSVC_TOOLSET "vc140")
    endif()

    #
    # Assimp
    #

    find_library(ASSIMP_LIB
        NAMES
            assimp-${MSVC_TOOLSET}-mt
        PATHS
            "${ASSIMP_LOCATION}/lib"
            "$ENV{ASSIMP_LOCATION}/lib"
            "${DEPENDENCIES_ROOT}/chk/assimp/${ASSIMP_PACKMAN_VER}/lib"
        PATH_SUFFIXES
            "x64"
    )

    find_file(ASSIMP_DLL
        NAMES
            assimp-${MSVC_TOOLSET}-mt.dll
        PATHS
            "${ASSIMP_LOCATION}/bin"
            "$ENV{ASSIMP_LOCATION}/bin"
            "${DEPENDENCIES_ROOT}/chk/assimp/${ASSIMP_PACKMAN_VER}/bin"
        PATH_SUFFIXES
            "x64"
    )

    #
    # Z-lib
    #

    find_library(ASSIMP_ZLIB_LIBRARY_DEBUG
        NAMES
            zlibstaticd
        PATHS
            "${ASSIMP_LOCATION}/lib"
            "$ENV{ASSIMP_LOCATION}/lib"
        PATH_SUFFIXES
            "x64"
    )

    if (ASSIMP_ZLIB_LIBRARY_DEBUG)
        list(APPEND ASSIMP_ZLIB_LIBRARY "debug" "${ASSIMP_ZLIB_LIBRARY_DEBUG}")
    endif()


    find_library(ASSIMP_ZLIB_LIBRARY_RELEASE
        NAMES
            zlibstatic
        PATHS
            "${ASSIMP_LOCATION}/lib"
            "$ENV{ASSIMP_LOCATION}/lib"
        PATH_SUFFIXES
            "x64"
    )

    if (ASSIMP_ZLIB_LIBRARY_RELEASE)
        list(APPEND ASSIMP_ZLIB_LIBRARY "optimized" "${ASSIMP_ZLIB_LIBRARY_RELEASE}")
    endif()

    #
    # XML
    #

    find_library(ASSIMP_XML_LIBRARY
        NAMES
            IrrXML
        PATHS
            "${ASSIMP_LOCATION}/lib"
            "$ENV{ASSIMP_LOCATION}/lib"
        PATH_SUFFIXES
            "x64"
    )

    # complete
    if (ASSIMP_LIB AND ASSIMP_DLL)
        set(ASSIMP_LIBRARIES "${ASSIMP_LIB}")
        if (ASSIMP_ZLIB_LIBRARY)
            list(APPEND ASSIMP_LIBRARIES "${ASSIMP_ZLIB_LIBRARY}")
        endif()
        if (ASSIMP_XML_LIBRARY)
            list(APPEND ASSIMP_LIBRARIES "${ASSIMP_XML_LIBRARY}")
        endif()
    endif()

    FUNCTION(ASSIMP_COPY_BINARIES TargetDirectory)
            add_custom_target(AssimpCopyBinaries
                COMMAND ${CMAKE_COMMAND} -E make_directory "${TargetDirectory}/Debug/"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${TargetDirectory}/Release/"
                COMMAND ${CMAKE_COMMAND} -E copy ${ASSIMP_DLL} "${TargetDirectory}/Debug/"
                COMMAND ${CMAKE_COMMAND} -E copy ${ASSIMP_DLL} "${TargetDirectory}/Release/"
                COMMENT "Copying ${ASSIMP_DLL} to '${TargetDirectory}/${CMAKE_BUILD_TYPE}'"
                VERBATIM
    )
    ENDFUNCTION(ASSIMP_COPY_BINARIES)

#message("ASSIMP_LIBRARIES ${ASSIMP_LIBRARIES}")

endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(Assimp
    DEFAULT_MSG
        ASSIMP_INCLUDE_DIR
        ASSIMP_LIBRARIES
)
