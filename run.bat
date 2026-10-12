@echo off
chcp 65001 >nul
pushd "%~dp0"
if not exist attendance.exe (
    echo 未找到 attendance.exe，请先使用 build.ps1 编译。
    pause
    popd
    exit /b 1
)
attendance.exe
pause
popd
