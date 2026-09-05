cmake_minimum_required(VERSION 3.20)

project(CommunicationPlanner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CP_BUILD_TESTS "Build tests" ON)
option(CP_BUILD_EXAMPLES "Build examples" ON)
option(CP_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CP_BUILD_TOOLS "Build CLI tooling" ON)
option(CP_BUILD_CUDA "Build CUDA proof targets (requires CUDA + GPU)" OFF)
option(CP_BUILD_ASAN "Build with AddressSanitizer (MSVC)" OFF)

include(GNUInstallDirs)

function(cp_apply_warnings tgt)
if(MSVC)
  target_compile_options(${tgt} PRIVATE /W4 /WX /permissive-)
  if(CP_BUILD_ASAN)
    target_compile_options(${tgt} PRIVATE /fsanitize=address /Zi)
    target_link_options(${tgt} PRIVATE /fsanitize=address /DEBUG)
  endif()
else()
  target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
endfunction()

add_library(communication_planner STATIC
  src/version.cpp
  src/planner.cpp
  src/feasibility.cpp
  src/cost.cpp
  src/ranking.cpp
  src/revalidation.cpp
  src/lifecycle.cpp
  src/persistence.cpp
  src/protocol.cpp
  src/multicast.cpp
  src/collective.cpp
  src/coordinator.cpp
)
add_library(CommunicationPlanner::CommunicationPlanner ALIAS communication_planner)
set_target_properties(communication_planner PROPERTIES EXPORT_NAME CommunicationPlanner)

target_include_directories(communication_planner
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(communication_planner PUBLIC cxx_std_20)
cp_apply_warnings(communication_planner)

if(MSVC)
  target_compile_options(communication_planner PRIVATE /EHsc)
endif()

include(CMakePackageConfigHelpers)

install(TARGETS communication_planner
  EXPORT CommunicationPlannerTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/communication_planner
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
  cmake/CommunicationPlannerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CommunicationPlannerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationPlanner
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/CommunicationPlannerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CommunicationPlannerConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CommunicationPlannerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationPlanner
)
install(EXPORT CommunicationPlannerTargets
  FILE CommunicationPlannerTargets.cmake
  NAMESPACE CommunicationPlanner::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommunicationPlanner
)

if(CP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(CP_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(CP_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(CP_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(CP_BUILD_CUDA)
  add_subdirectory(cuda)
endif()

add_subdirectory(apps)

