@echo off
setlocal
call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0../.."
if not exist "_build\v157-validation" mkdir "_build\v157-validation"
cl /nologo /std:c++17 /O2 /MD /W3 /wd4995 /wd4267 /DNO_XRNEW /DWIN32 /DNDEBUG /DPURE_ALLOC /D_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS /I"sdk/include" /I"sdk/include/dxsdk" /I"src/3rd party" tools/tests/load_memory_concurrency_test.cpp /Fo:_build/v157-validation/load_memory_concurrency_test.obj /Fe:_build/v157-validation/load_memory_concurrency_test.exe
if errorlevel 1 exit /b 1
"_build\v157-validation\load_memory_concurrency_test.exe"
