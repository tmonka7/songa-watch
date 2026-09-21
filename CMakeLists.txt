# Songa Watch - ESP32-S3-Touch-AMOLED-2.06 smart watch firmware
#
# Every third-party dependency is vendored under components/, so this project
# builds with no network access. The IDF component manager is disabled on
# purpose (see the IDF_COMPONENT_MANAGER set() below) - there is no
# idf_component.yml anywhere in this tree, and nothing is ever fetched.

cmake_minimum_required(VERSION 3.16)

# Hard-disable the component manager. Without this it would still scan for
# manifests and contact the registry when one appears.
set(ENV{IDF_COMPONENT_MANAGER} "0")

set(COMPONENTS_DIR "${CMAKE_CURRENT_LIST_DIR}/components")
set(EXTRA_COMPONENT_DIRS "${COMPONENTS_DIR}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(songa_watch)
