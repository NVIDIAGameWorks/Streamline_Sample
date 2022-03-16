set(__donut_compileshaders_script
    ${CMAKE_CURRENT_LIST_DIR}/scripts/CompileShaders.py CACHE INTERNAL "")

# generates a build target that will compile shaders for a given config file
#
# usage: donut_compile_shaders(TARGET <generated build target name>
#                              CONFIG <shader-config-file>
#                              [DXIL <dxil-output-path>]
#                              [DXBC <dxbc-output-path>]
#                              [SPIRV_DXC <spirv-output-path>])

function(donut_compile_shaders)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER DXIL DXBC SPIRV_DXC)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "donut_compile_shaders: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "donut_compile_shaders: CONFIG argument missing")
    endif()

    # just add the source files to the project as documents, they are built by the script
    set_source_files_properties(${params_SOURCES} PROPERTIES VS_TOOL_OVERRIDE "None") 

    add_custom_target(${params_TARGET}
        DEPENDS ${__donut_compileshaders_script}
        SOURCES ${params_SOURCES})

    if (params_DXIL AND (DONUT_WITH_DX12 AND DONUT_USE_DXIL_ON_DX12))
        if (NOT DXC_BIN_DXIL)
            #message(FATAL_ERROR "donut_compile_shaders: DXC not found --- please set DXC_BIN_DXIL to the full path to the DXC binary")
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND ${Python_EXECUTABLE} ${__donut_compileshaders_script}
                                   -infile ${params_CONFIG}
                                   -parallel
                                   -out ${params_DXIL}
                                   -dxil
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
                                   -dxc ${DXC_BIN_DXIL})
    endif()

    if (params_DXBC AND (DONUT_WITH_DX11 OR (DONUT_WITH_DX12 AND NOT DONUT_USE_DXIL_ON_DX12)))
        if (NOT FXC_BIN)
            #message(FATAL_ERROR "donut_compile_shaders: FXC not found --- please set FXC_BIN to the full path to the FXC binary")
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND ${Python_EXECUTABLE} ${__donut_compileshaders_script}
                                   -infile ${params_CONFIG}
                                   -parallel
                                   -out ${params_DXBC}
                                   -dxbc
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
                                   -fxc ${FXC_BIN})
    endif()

    if (params_SPIRV_DXC AND DONUT_WITH_VULKAN)
        if (NOT DXC_BIN_SPIRV)
            message(FATAL_ERROR "donut_compile_shaders: DXC for SPIR-V not found --- please set DXC_BIN_SPIRV to the full path to the DXC binary")
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND ${Python_EXECUTABLE} ${__donut_compileshaders_script}
                                   -infile ${params_CONFIG}
                                   -parallel
                                   -out ${params_SPIRV_DXC}
                                   -spirv
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
                                   -dxc ${DXC_BIN_SPIRV})
    endif()

    if(params_FOLDER)
        set_target_properties(${params_TARGET} PROPERTIES FOLDER ${params_FOLDER})
    endif()
endfunction()

# Generates a build target that will compile shaders for a given config file for all enabled Donut platforms.
#
# The shaders will be placed into subdirectories of ${OUTPUT_BASE}, with names compatible with
# the FindDirectoryWithShaderBin framework function.

function(donut_compile_shaders_all_platforms)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER OUTPUT_BASE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: CONFIG argument missing")
    endif()
    if (NOT params_OUTPUT_BASE)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: OUTPUT_BASE argument missing")
    endif()

    donut_compile_shaders(TARGET ${params_TARGET}
                          CONFIG ${params_CONFIG}
                          FOLDER ${params_FOLDER}
                          DXBC ${params_OUTPUT_BASE}/dxbc
                          DXIL ${params_OUTPUT_BASE}/dxil
                          SPIRV_DXC ${params_OUTPUT_BASE}/spirv
                          SOURCES ${params_SOURCES})

endfunction()