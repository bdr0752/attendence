param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$MSBuildPath = '',
    [string]$PlatformToolset = ''
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'mfc/Attendance.vcxproj'
if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw '未找到 Visual Studio Installer。请安装“使用 C++ 的桌面开发”和 MFC 组件。' }
    $found = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.ATLMFC -find 'MSBuild\**\Bin\MSBuild.exe')
    if ($found.Count -eq 0) { throw '未找到可用的 MSVC/MFC 环境。请安装 MSVC、Windows SDK 和匹配工具集的 MFC 组件。' }
    $MSBuildPath = $found[0]
}
if (-not (Test-Path -LiteralPath $MSBuildPath)) { throw "MSBuild 文件不存在：$MSBuildPath" }
$buildArguments = @($project, '/m', '/t:Build', "/p:Configuration=$Configuration", '/p:Platform=x64', '/v:minimal')
if ($PlatformToolset) { $buildArguments += "/p:PlatformToolset=$PlatformToolset" }
& $MSBuildPath @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'MFC 编译失败，请查看上方的编译诊断。' }
Write-Host '编译完成：项目目录中的 attendance_mfc.exe。'
