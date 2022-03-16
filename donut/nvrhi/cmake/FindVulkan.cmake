# xxxnsubtil: the vulkan SDK helpfully moves things around willy-nilly
# we're forced to maintain this file on our own

# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#.rst:
# FindVulkan
# ----------
#
# Try to find Vulkan
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines :prop_tgt:`IMPORTED` target ``Vulkan::Vulkan``, if
# Vulkan has been found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables::
#
#   Vulkan_FOUND          - True if Vulkan was found
#   Vulkan_INCLUDE_DIRS   - include directories for Vulkan
#   Vulkan_LIBRARIES      - link against this library to use Vulkan
#
# The module will also define two cache variables::
#
#   Vulkan_INCLUDE_DIR    - the Vulkan include directory
#   Vulkan_LIBRARY        - the path to the Vulkan library
#

if(NOT DEFINED Vulkan_SDK_ROOT_DIR)
    set(Vulkan_SDK_ROOT_DIR $ENV{VULKAN_SDK})
endif()

if(WIN32)
  find_path(Vulkan_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS
      "${Vulkan_SDK_ROOT_DIR}/Include"
    )

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    find_library(Vulkan_LIBRARY
      NAMES vulkan-1
      HINTS
        "${Vulkan_SDK_ROOT_DIR}/Bin"
        "${Vulkan_SDK_ROOT_DIR}/Lib")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_library(Vulkan_LIBRARY
      NAMES vulkan-1
      HINTS
        "${Vulkan_SDK_ROOT_DIR}/Bin32"
        "${Vulkan_SDK_ROOT_DIR}/Lib32")
  endif()
else()
  find_path(Vulkan_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS
      "${Vulkan_SDK_ROOT_DIR}/include"
      "$ENV{VULKAN_SDK}/include")
  find_library(Vulkan_LIBRARY
    NAMES vulkan
    HINTS
      "${Vulkan_SDK_ROOT_DIR}/lib"
      "$ENV{VULKAN_SDK}/lib")
endif()

set(Vulkan_LIBRARIES ${Vulkan_LIBRARY})
set(Vulkan_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vulkan
  DEFAULT_MSG
  Vulkan_LIBRARY Vulkan_INCLUDE_DIR)

mark_as_advanced(Vulkan_INCLUDE_DIR Vulkan_LIBRARY)

# jukim - setting VULKAN_* variables so that glfw uses the results of this find as well
set(VULKAN_FOUND        ${Vulkan_FOUND}         CACHE INTERNAL "VULKAN_FOUND")
set(VULKAN_INCLUDE_DIR  ${Vulkan_INCLUDE_DIR}   CACHE INTERNAL "VULKAN_INCLUDE_DIR")
set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS}  CACHE INTERNAL "VULKAN_INCLUDE_DIRS")
set(VULKAN_LIBRARIES    ${Vulkan_LIBRARIES}     CACHE INTERNAL "VULKAN_LIBRARIES")
set(VULKAN_LIBRARY      ${Vulkan_LIBRARY}       CACHE INTERNAL "VULKAN_LIBRARY")
mark_as_advanced(VULKAN_FOUND VULKAN_INCLUDE_DIR VULKAN_INCLUDE_DIRS VULKAN_LIBRARIES VULKAN_LIBRARY)

if(Vulkan_FOUND AND NOT TARGET Vulkan::Vulkan)
  add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
  set_target_properties(Vulkan::Vulkan PROPERTIES
    IMPORTED_LOCATION "${Vulkan_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}")
endif()
