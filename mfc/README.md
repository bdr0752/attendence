# MFC 图形界面版本

本版本使用真正的 MFC `CWinApp`、`CDialogEx`、`CListCtrl`、组合框、编辑框和文件保存对话框，不是网页或控制台界面的模拟。保留根目录的控制台程序。

## 使用

在项目根目录双击 `run_mfc.bat`，或者双击 `attendance_mfc.exe`。学生和考勤视图可新增、修改、删除；五种报表视图只读，可导出文本或 CSV。

学号、姓名查询为精确匹配；留空显示全部。班级留空表示全部班级。日期使用 `MM-DD` 或 `M-D`，日期范围必须同时填写起止日期；日报只填写起始月日。输入后点击“应用筛选”。学生视图不使用日期筛选。

表头和顶部统计明确区分人数与记录次数。学生总人数取学生名单；当前结果涉及人数按学号去重；出勤人数只统计当前结果中至少有一条“出勤”记录的不同学号；记录数按条计算。不同状态次数不能直接解释为人数，迟到和早退没有合并到“出勤”状态中。

汇总表按学生列出各状态次数，包含范围内没有记录的学生；请假表列出事假、病假与合计，顶部人数只计算实际有请假记录的学生。异常表使用 `rules.ini` 的状态分类设置。

导出先重新应用筛选，使用选中的文本或 CSV 格式，通过保存对话框选择路径。中文路径支持 UTF-8/Unicode 转换。字段校验、修改学号后的关联更新、重复记录检查、保存失败回滚、最近备份恢复及文件锁使用共用 C 接口。

程序总是使用可执行文件所在目录的数据与规则。只有位于同一目录的控制台版与 MFC 版才共用 `attendance.db`，不能同时运行。原来另一个文件夹的数据库不会自动迁移；需要迁移时先关闭两个程序，再备份并复制数据文件，勿直接覆盖未经备份的数据。

## 编译脚本怎么使用

在项目根目录打开 PowerShell，运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build_mfc.ps1
```

成功后根目录生成 `attendance_mfc.exe`。原来的 `build.ps1` 仍然用于编译控制台版本，不能用于编译 MFC。

脚本通过 Visual Studio Installer 的 `vswhere` 查找安装了 MFC 的实例，工程自动使用该实例的默认工具集。需要 Visual Studio 的“使用 C++ 的桌面开发”、对应 MSVC、Windows SDK 和匹配工具集的 MFC 组件。GCC不能代替这个脚本所需的 MSVC/MFC 工具链。

已在本机 Visual Studio 18 Community、MSVC v145、Windows SDK 10.0.26100.0 下编译 Release x64。需要指定另一套已安装工具集时可以运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build_mfc.ps1 -PlatformToolset v143
```

只有已安装 v143 和对应 MFC 时才能使用这个参数；默认不需要指定。也可双击 `Attendance.sln`，在 Visual Studio 选择 `Release`、`x64`，然后生成解决方案。MFC与CRT使用静态链接，程序仍然需要Windows系统组件；未验证其他机器上的部署。

## 文件与验证

- `Attendance.cpp`：窗口、编辑对话框、筛选与表格显示。
- `Attendance.rc`、`resource.h`：对话框资源及控件编号。
- `Attendance.vcxproj`、`Attendance.sln`：Visual Studio 工程。
- `../attendance_core.h`：供 C++ 窗口调用的 C 接口与共用数据类型。
- `../main.c`：现有数据实现，定义 `ATTENDANCE_CORE_ONLY` 后不生成控制台入口，而提供共用接口。
- `../test_system.py`：控制台22项测试。
- `../test_core.py`：MFC实际调用接口的8项测试，包含人数去重、中文路径导出、保存回滚、备份和数据兼容。
- `../test_mfc_ui.py`：启动隔离副本，验证实际窗口中的学生新增、考勤登记、人数、班级和日期筛选、日报与汇总、正常关闭，生成测试窗口预览；不访问用户其他应用或实际考勤文件。

窗口验证覆盖了上述操作流程，没有覆盖所有按钮交互，也没有验证所有显示缩放和屏幕大小。数据业务规则仍以根目录说明中的限制为准，包括仅存月日、六种状态互斥、姓名编码排序。
