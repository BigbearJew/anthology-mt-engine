@echo off
setlocal
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0../.."
if not exist "_build\v157-validation" mkdir "_build\v157-validation"
cl /nologo /std:c++17 /EHsc /W4 /WX tools/tests/texture_residency_test.cpp /Fo:_build/v157-validation/texture_residency_test.obj /Fe:_build/v157-validation/texture_residency_test.exe
if errorlevel 1 exit /b 1
"_build\v157-validation\texture_residency_test.exe"
