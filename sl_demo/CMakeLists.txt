add_executable(sl_demo WIN32 DemoMain.cpp SLWrapper.h SLWrapper.cpp glue/RenderTargets.h glue/UIRenderer.h)
target_link_libraries(sl_demo donut_core donut_engine donut_render donut_app)
target_include_directories(sl_demo PUBLIC . ./glue)


set(__sl_demo_libs "")
if(DONUT_WITH_ASSIMP)
    set(__sl_demo_libs ${__sl_demo_libs} assimp)
endif()
if(DONUT_WITH_HBAO_PLUS)
    if(DONUT_WITH_DX11)
        set(__sl_demo_libs ${__sl_demo_libs} hbao_plus_d3d11)
    endif()
    if(DONUT_WITH_DX12)
        set(__sl_demo_libs ${__sl_demo_libs} hbao_plus_d3d12)
    endif()
endif()
if(DONUT_WITH_VOLUME_LIGHTING)
    if(DONUT_WITH_DX11)
        set(__sl_demo_libs ${__sl_demo_libs} volumelighting_d3d11)
    endif()
    if(DONUT_WITH_DX12)
        set(__sl_demo_libs ${__sl_demo_libs} volumelighting_d3d12)
    endif()
endif()
if(DONUT_WITH_SL)
    set(__sl_demo_libs ${__sl_demo_libs} sl)
endif()

# xxxnsubtil: haven't been able to abstract this into a function or macro
# due to the $<TARGET_FILE_DIR:> generator expression
foreach(lib ${__sl_demo_libs})
    get_target_property(lib_type ${lib} TYPE)
    if ("${lib_type}" STREQUAL "SHARED_LIBRARY")
        get_property(dll_file TARGET ${lib} PROPERTY IMPORTED_LOCATION)

        add_custom_command(TARGET sl_demo
                           COMMENT "Dependant DLLs copy ${dll_file} $<TARGET_FILE_DIR:sl_demo>"
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll_file}" "$<TARGET_FILE_DIR:sl_demo>")

        STRING(REGEX REPLACE "\\\\" "/" dll_file ${dll_file})
        install(FILES ${dll_file} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif ()
endforeach()

# DLLS for the SL integration
if(DONUT_WITH_SL)
    # Need to copy all libraries NGX needs
    get_property(__SL_DLLS_LIST TARGET sl PROPERTY EXTRA_DLLS)
    foreach(dll_path ${__SL_DLLS_LIST})
        # Resolve wildcards
        file(GLOB dll_files "${dll_path}")
        foreach(dll_file ${dll_files})
            add_custom_command(TARGET sl_demo
                               COMMENT "SL DLL copy ${dll_file} $<TARGET_FILE_DIR:sl_demo>\n"
                               COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll_file}" "$<TARGET_FILE_DIR:sl_demo>")
            STRING(REGEX REPLACE "\\\\" "/" dll_file ${dll_file})
            install(FILES ${dll_file} DESTINATION ${CMAKE_INSTALL_BINDIR})
        endforeach()
    endforeach()
endif()

add_custom_command(TARGET sl_demo
                   COMMENT "MEDIA copy Media  $<TARGET_FILE_DIR:sl_demo>\n"
                   COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/media" "$<TARGET_FILE_DIR:sl_demo>/media")

set_target_properties(sl_demo PROPERTIES FOLDER "Donut SL Demo")

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP /we4838 /Zc:ternary")
endif()


install(TARGETS sl_demo RUNTIME DESTINATION bin/sl_demo)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" DESTINATION ".")
