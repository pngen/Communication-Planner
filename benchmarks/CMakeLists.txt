add_executable(cp_bench bench.cpp)
target_link_libraries(cp_bench PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_bench)
