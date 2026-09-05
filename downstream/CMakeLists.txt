cmake_minimum_required(VERSION 3.20)
project(CommunicationPlannerDownstream LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CommunicationPlanner CONFIG REQUIRED)

add_executable(downstream_consumer main.cpp)
target_link_libraries(downstream_consumer PRIVATE CommunicationPlanner::CommunicationPlanner)
