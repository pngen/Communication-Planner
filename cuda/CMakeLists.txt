# The Visual Studio generator requires the CUDA MSBuild toolset to build .cu
# sources. When that toolset is not registered, check_language(CUDA) reports no
# compiler and we skip the CUDA proof cleanly. The proof source remains and is
# built directly with nvcc (see README); on this host it was built and passed.
include(CheckLanguage)
if(NOT CP_BUILD_CUDA)
  return()
endif()
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  add_executable(cp_cuda_plan_gated_proof cuda_plan_gated_proof.cu)
  target_link_libraries(cp_cuda_plan_gated_proof PRIVATE CommunicationPlanner::CommunicationPlanner)
  set_target_properties(cp_cuda_plan_gated_proof PROPERTIES CUDA_STANDARD 20)
  if(MSVC)
    target_compile_options(cp_cuda_plan_gated_proof PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4;-Xcompiler=/WX;-Xcompiler=/MD>)
  endif()
  add_test(NAME cuda_plan_gated COMMAND cp_cuda_plan_gated_proof)
else()
  message(STATUS "CUDA compiler not available under the selected generator; CUDA proof skipped (built via nvcc directly).")
endif()
