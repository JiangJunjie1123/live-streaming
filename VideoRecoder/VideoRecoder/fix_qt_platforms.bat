@echo off
echo ==========================================
echo Fix Qt Platform Plugin Issue
echo ==========================================

set SOURCE_DIR=D:\VideoRecoder\build-VideoRecoder-Desktop_Qt_5_12_11_MinGW_32_bit-Debug\debug
set TARGET_DIR=%SOURCE_DIR%\debug

if not exist "%SOURCE_DIR%\platforms" (
    echo [ERROR] Source platforms folder not found!
    echo Path: %SOURCE_DIR%\platforms
    pause
    exit /b 1
)

if not exist "%TARGET_DIR%" (
    echo [ERROR] Target directory not found!
    echo Path: %TARGET_DIR%
    pause
    exit /b 1
)

echo.
echo [1/2] Copying platforms folder...
echo From: %SOURCE_DIR%\platforms
echo To: %TARGET_DIR%\platforms

xcopy /E /I /Y "%SOURCE_DIR%\platforms" "%TARGET_DIR%\platforms"

if %errorlevel% == 0 (
    echo.
    echo [2/2] Copy success!
    echo.
    echo ==========================================
    echo Fix complete! You can now run the program.
    echo ==========================================
) else (
    echo.
    echo [ERROR] Copy failed!
)

pause
