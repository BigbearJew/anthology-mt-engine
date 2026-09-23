@echo off
setlocal
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0../.."
if not exist "_build\v143-validation" mkdir "_build\v143-validation"
cl /nologo /std:c++17 /EHsc /W4 /WX tools/tests/pip_temporal_cost_gpu_test.cpp /Fo:_build/v143-validation/pip_temporal_cost_gpu_test.obj /Fe:_build/v143-validation/pip_temporal_cost_gpu_test.exe d3d11.lib d3dcompiler.lib dxguid.lib
if errorlevel 1 exit /b 1
if "%~1"=="" (
    "_build\v143-validation\pip_temporal_cost_gpu_test.exe" "%CD%" "D:/ANTHOLOGY_DEV/releases/Anthology v142 - PiP Artifact Fix/gamedata/shaders/r3"
) else (
    "_build\v143-validation\pip_temporal_cost_gpu_test.exe" "%CD%" "D:/ANTHOLOGY_DEV/releases/Anthology v142 - PiP Artifact Fix/gamedata/shaders/r3" "%~1"
)
