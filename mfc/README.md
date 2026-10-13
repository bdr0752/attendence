# MFC 版本使用说明

用VS2026打开本目录的 `Attendance.sln`，选择 `Release`、`x64`，按 `Ctrl+Shift+B` 生成，按 `F5` 运行。工程使用本机默认MSVC工具集；需要安装对应MFC与Windows SDK。

也可在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build_mfc.ps1
```

生成根目录的 `attendance_mfc.exe`，双击 `run_mfc.bat` 启动。

## 题目所需操作

功能菜单包含学生管理、考勤记录，以及明细、日报、汇总、异常、请假五类报表。

- 学生管理：新增、修改、删除，按学号或姓名查找学生。
- 考勤记录：新增、修改、删除，按学号、姓名、月日查找，支持三种字段排序。
- 查询值留空显示全部。选择日期时使用 `MM-DD` 或 `M-D`，不支持日期范围。
- 日报使用查询框中的一个月日；其他报表统计全部记录。
- 五类报表查看后自动保存为项目目录中的文本文件。

数据修改后自动保存。六种考勤状态显示对应符号；顶部人数按学号去重，记录数按条统计。字段校验与统计规则见根目录README。

窗口与原有控制台共用C数据实现及同格式文件，只能在同一目录共用同一数据库。此版本仅提供题目功能。

界面使用浅色背景、统一字体和圆角按钮，考勤状态以不同颜色区分。主窗口支持调整大小，表格随窗口伸展，底部操作按钮保持可见。

## 源文件

`Attendance.cpp`为窗口操作，`Attendance.rc`与`resource.h`为界面资源，`Attendance.vcxproj`与`Attendance.sln`为工程。根目录`attendance_core.h`为接口，`main.c`为数据实现和控制台入口。定义`ATTENDANCE_CORE_ONLY`后，`main.c`提供MFC接口而不生成控制台入口。

`Theme.cpp`与`Theme.h`负责字体、配色、按钮和表格绘制，不处理考勤数据。

本机Release x64编译与窗口流程验证完成；验证范围包括新增、查询、人数显示、五类报表保存和退出，不代表所有显示缩放和部署环境均已测试。
