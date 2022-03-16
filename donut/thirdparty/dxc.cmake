# locate DXC

set(__dxc_package_path ${DEPENDENCIES_ROOT}/chk/dxc/${dxc_package_version})

if (NOT DEFINED DXC_BIN_SPIRV)
    if (WIN32)
        if (NOT EXISTS ${__dxc_package_path}/bin/dxc.exe)
            message(FATAL_ERROR "packman package for dxc not installed --- please run update-dependencies.bat or set DXC_BIN_SPIRV to point at a SPIR-V capable dxc binary")
        endif()
        set(DXC_BIN_SPIRV ${__dxc_package_path}/bin/dxc.exe CACHE FILEPATH "" FORCE)
    else()
        if (NOT EXISTS ${__dxc_package_path}/bin/dxc)
            message(FATAL_ERROR "packman package for dxc not installed --- please run update-dependencies.sh or set DXC_BIN_SPIRV to point at a SPIR-V capable dxc binary")
        endif()
        set(DXC_BIN_SPIRV ${__dxc_package_path}/bin/dxc CACHE FILEPATH "" FORCE)
    endif()
endif()

# locate DXC or FXC
if (NOT DXC_BIN_DXIL OR NOT FXC_BIN)
    if (WIN32)
        # try to locate a suitable DXC compiler
        get_filename_component(kit10_dir "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" REALPATH)
        file(GLOB W10SDK_VERSIONS RELATIVE ${kit10_dir}/Include ${kit10_dir}/Include/10.*)          # enumerate pre-release and not yet known release versions
        list(APPEND W10SDK_VERSIONS "10.0.15063.0" "10.0.16299.0" "10.0.17134.0")   # enumerate well known release versions
        list(REMOVE_DUPLICATES W10SDK_VERSIONS)
        list(SORT W10SDK_VERSIONS)  # sort from low to high
        list(REVERSE W10SDK_VERSIONS) # reverse to start from high
        foreach(W10SDK_VER ${W10SDK_VERSIONS})
            unset(DXC_EXE_CANDIDATE CACHE)
            unset(FXC_EXE_CANDIDATE CACHE)
            set(WINSDK_PATHS "${kit10_dir}/bin/${W10SDK_VER}/x64" "C:/Program Files (x86)/Windows Kits/10/bin/${W10SDK_VER}/x64" "C:/Program Files/Windows Kits/10/bin/${W10SDK_VER}/x64")
            find_program(DXC_EXE_CANDIDATE dxc PATHS ${WINSDK_PATHS})
            find_program(FXC_EXE_CANDIDATE fxc PATHS ${WINSDK_PATHS})
            if (EXISTS ${DXC_EXE_CANDIDATE} AND NOT DXC_BIN_DXIL)
                set(DXC_BIN_DXIL ${DXC_EXE_CANDIDATE} CACHE FILEPATH "" FORCE)
            endif()
            if (EXISTS ${FXC_EXE_CANDIDATE} AND NOT FXC_BIN)
                set(FXC_BIN ${FXC_EXE_CANDIDATE} CACHE FILEPATH "" FORCE)
            endif()
        endforeach()

        unset(DXC_EXE_CANDIDATE CACHE)
        unset(FXC_EXE_CANDIDATE CACHE)
        unset(WINSDK_PATHS)
    else()
        #message(FATAL_ERROR "DXC binary not found --- please set DXC_BIN_DXIL to the full path to the DXC binary")
    endif()
endif()
