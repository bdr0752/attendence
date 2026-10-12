@echo off
chcp 65001 >nul
pushd "%~dp0"
if not exist attendance_mfc.exe (
    echo 未找到 MFC 程序，请先运行 build_mfc.ps1 编译。
    pause
    popd
    exit /b 1
)
start "" /wait attendance_mfc.exe
popd
