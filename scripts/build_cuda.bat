@echo off
rem Builds the CUDA Worker and the plan-gated CUDA proof with nvcc (the Visual Studio
rem generator has no registered CUDA MSBuild toolset, so .cu is built directly).
setlocal
if "%CUDA_PATH%"=="" set CUDA_PATH=C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul 2>&1
set NXFLAGS=-std=c++20 -arch=sm_120 -Xcompiler=/W4 -Xcompiler=/MD -Xcompiler=/EHsc
if not exist "%~dp0..\build\cuda" mkdir "%~dp0..\build\cuda"
"%CUDA_PATH%/bin/nvcc.exe" %NXFLAGS% -x cu -I "%~dp0..\include" -I "%~dp0..\src" -I "%~dp0..\build\Release" "%~dp0..\cuda\cuda_worker_main.cu" -o "%~dp0..\build\cuda\cp_cuda_worker.exe" -L "%~dp0..\build\Release" -l communication_planner
"%CUDA_PATH%/bin/nvcc.exe" %NXFLAGS% -x cu -I "%~dp0..\include" -I "%~dp0..\build\Release" "%~dp0..\cuda\cuda_plan_gated_proof.cu" -o "%~dp0..\build\cuda\cp_cuda_plan_gated_proof.exe" -L "%~dp0..\build\Release" -l communication_planner
endlocal
