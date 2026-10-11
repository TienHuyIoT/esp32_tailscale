cmake_minimum_required(VERSION 3.16)

# Add microlink-v2 component path
set(EXTRA_COMPONENT_DIRS
    "../../components"
)

# Overlay local credentials (gitignored) when generating sdkconfig.
#  - Skipped when SDKCONFIG_DEFAULTS is already provided (-D or environment).
#  - Requires sdkconfig.credentials to exist; the build errors out when a file
#    listed in SDKCONFIG_DEFAULTS is missing.
#  - Like any sdkconfig defaults file, values apply when sdkconfig is
#    regenerated: after editing credentials, delete sdkconfig (or use menuconfig).
if(NOT SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS}
        AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.credentials")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.credentials")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(microlink_v2_basic_connect)
