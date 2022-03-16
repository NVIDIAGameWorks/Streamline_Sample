
if (WIN32)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/StackWalker/CMakeLists.txt)
        # StackWalker has been installed, add it to the project
        add_subdirectory(StackWalker)
        target_include_directories(StackWalker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/StackWalker/Main/StackWalker)
    else()
        message(STATUS "StackWalker not installed, stack traces will not be available")
    endif()

endif()
