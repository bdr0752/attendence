$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 main.c -o attendance.exe
    if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
    Write-Host '编译完成：attendance.exe'
} finally {
    Pop-Location
}
