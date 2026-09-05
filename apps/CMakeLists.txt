add_executable(cp_coordinator coordinator_main.cpp)
target_link_libraries(cp_coordinator PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_coordinator)

add_executable(cp_worker worker_main.cpp)
target_link_libraries(cp_worker PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_worker)

add_executable(cp_multiprocess_proof proof_main.cpp)
target_link_libraries(cp_multiprocess_proof PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_multiprocess_proof)
add_test(NAME multiprocess COMMAND cp_multiprocess_proof $<TARGET_FILE:cp_coordinator> $<TARGET_FILE:cp_worker>)

set(CP_CUDA_WORKER_EXE "${CMAKE_BINARY_DIR}/cuda/cp_cuda_worker.exe" CACHE FILEPATH "Path to nvcc-built CUDA Worker executable")
add_executable(cp_cuda_worker_death_proof cuda_worker_death_proof.cpp)
target_link_libraries(cp_cuda_worker_death_proof PRIVATE CommunicationPlanner::CommunicationPlanner)
cp_apply_warnings(cp_cuda_worker_death_proof)
add_test(NAME cuda_worker_death COMMAND cp_cuda_worker_death_proof $<TARGET_FILE:cp_coordinator> ${CP_CUDA_WORKER_EXE})
set_tests_properties(cuda_worker_death PROPERTIES SKIP_RETURN_CODE 77)
