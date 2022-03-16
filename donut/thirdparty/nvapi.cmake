set(__nvapi_package_path ${DEPENDENCIES_ROOT}/chk/nvapi/${nvapi_package_version})
if (NOT EXISTS ${__nvapi_package_path}/nvapi-import.cmake)
  message(FATAL_ERROR "nvapi package not installed --- please run update-dependencies.bat")
endif()

include(${__nvapi_package_path}/nvapi-import.cmake)
