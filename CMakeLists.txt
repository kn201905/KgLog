
cmake_minimum_required(VERSION 3.0)

project(gLog CXX)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)

if(NOT COMPILER_SUPPORTS_CXX17)
	message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support...")
endif()

message(STATUS "Compiler Supports C++17")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -pthread")
add_executable(glog main.cpp src/KgLog.cpp)

