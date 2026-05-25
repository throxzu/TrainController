cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TrainDetector)
