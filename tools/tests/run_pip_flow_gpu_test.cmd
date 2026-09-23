@echo off
setlocal
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0../.."
if not exist "_build\v142-validation" mkdir "_build\v142-validation"
cl /nologo /std:c++17 /EHsc /W4 /WX tools/tests/pip_flow_gpu_test.cpp /Fo:_build/v142-validation/pip_flow_gpu_test.obj /Fe:_build/v142-validation/pip_flow_gpu_test.exe d3d11.lib d3dcompiler.lib dxguid.lib
if errorlevel 1 exit /b 1
if "%~1"=="" (
    "_build\v142-validation\pip_flow_gpu_test.exe" "%CD%"
) else (
    "_build\v142-validation\pip_flow_gpu_test.exe" "%CD%" "%~1"
)
