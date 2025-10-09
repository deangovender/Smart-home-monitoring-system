# Root CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Trim build: include 'main' and whatever it requires.
idf_build_set_property(MINIMAL_BUILD ON)

project(smart_home_monitoring)
