add_executable(cp_test_core test_core.cpp)
target_link_libraries(cp_test_core PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_test_core)
add_test(NAME core COMMAND cp_test_core)
