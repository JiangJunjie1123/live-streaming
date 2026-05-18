@echo off
REM === MedieaPlayer 自动部署脚本 ===
REM 每次构建后运行此脚本，自动复制所有依赖 DLL

set "SRC_DIR=%~dp0"
set "BUILD_DIR=%SRC_DIR%..\build-MedieaPlayer-Desktop_Qt_5_12_11_MinGW_32_bit-Debug\debug"
set "EXE=%BUILD_DIR%\MedieaPlayer.exe"
set "WINDEPLOYQT=C:\Qt\Qt5.12.11\5.12.11\mingw73_32\bin\windeployqt.exe"
set "FFMPEG_BIN=%SRC_DIR%ffmpeg-4.2.2\ffmpeg-4.2.2\bin"
set "SDL2_DLL=%SRC_DIR%SDL2-2.0.10\SDL2-2.0.10\lib\x86\SDL2.dll"

if not exist "%EXE%" (
    echo [ERROR] MedieaPlayer.exe not found at %EXE%
    echo Please build the project first.
    exit /b 1
)

echo [1/3] Running windeployqt...
"%WINDEPLOYQT%" "%EXE%"

echo [2/3] Copying FFmpeg DLLs...
copy /Y "%FFMPEG_BIN%\*.dll" "%BUILD_DIR%\" >nul

echo [3/3] Copying SDL2 DLL...
copy /Y "%SDL2_DLL%" "%BUILD_DIR%\" >nul

echo [4/3] Ensuring correct MinGW runtime DLLs (DW2, not SJLJ)...
set "MINGW_BIN=C:\Qt\Qt5.12.11\Tools\mingw730_32\bin"
copy /Y "%MINGW_BIN%\libgcc_s_dw2-1.dll" "%BUILD_DIR%\" >nul
copy /Y "%MINGW_BIN%\libstdc++-6.dll" "%BUILD_DIR%\" >nul
copy /Y "%MINGW_BIN%\libwinpthread-1.dll" "%BUILD_DIR%\" >nul
del /Q "%BUILD_DIR%\libgcc_s_sjlj-1.dll" 2>nul

echo.
echo [DONE] All dependencies deployed to %BUILD_DIR%
echo MedieaPlayer is ready to run.
