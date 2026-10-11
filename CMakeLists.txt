cmake_minimum_required(VERSION 3.20)
project(any_of_bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(any_of_bench main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(any_of_bench Threads::Threads)

find_package(TBB QUIET)
if(TBB_FOUND)
    target_link_libraries(any_of_bench TBB::tbb)
endif()