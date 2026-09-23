@echo off
setlocal
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0../.."
if not exist "_build\pip-motion-validation" mkdir "_build\pip-motion-validation"
cl /nologo /std:c++17 /EHsc /W4 /WX tools/tests/render_surface_owner_test.cpp /Fo:_build/pip-motion-validation/render_surface_owner_test.obj /Fe:_build/pip-motion-validation/render_surface_owner_test.exe
if errorlevel 1 exit /b 1
"_build\pip-motion-validation\render_surface_owner_test.exe"
