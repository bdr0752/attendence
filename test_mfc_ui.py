"""启动本项目MFC程序，通过Windows消息检查窗口操作；使用独立测试目录。"""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
user = C.WinDLL("user32", use_last_error=True)
user.SetProcessDPIAware()  # 与MFC的系统DPI感知一致，避免预览图只截到窗口的一部分。
user.GetDlgItem.argtypes = [W.HWND, C.c_int]
user.GetDlgItem.restype = W.HWND
user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.restype = W.LPARAM
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.IsWindow.argtypes = [W.HWND]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, W.LPARAM]


def text(window):
    buffer = C.create_unicode_buffer(1024)
    user.SendMessageW(window, 0xD, len(buffer), C.cast(buffer, C.c_void_p).value)
    return buffer.value


def wait_for(predicate, message):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError(message)


def window_for(pid, title):
    found = []
    @CALLBACK
    def callback(window, _):
        owner = W.DWORD()
        user.GetWindowThreadProcessId(window, C.byref(owner))
        if owner.value == pid and text(window) == title:
            found.append(window)
        return True
    user.EnumWindows(callback, 0)
    return found[0] if found else None


def click(dialog, control):
    user.PostMessageW(dialog, 0x111, control, user.GetDlgItem(dialog, control))


def set_field(dialog, control, value):
    buffer = C.create_unicode_buffer(str(value))
    handle = user.GetDlgItem(dialog, control)
    assert user.SendMessageW(handle, 0xC, 0, C.cast(buffer, C.c_void_p).value)
    assert text(handle) == str(value)


def select_view(main, index):
    view = user.GetDlgItem(main, 1000)
    user.SendMessageW(view, 0x14E, index, 0)
    user.PostMessageW(main, 0x111, 1000 | (1 << 16), view)
    time.sleep(0.1)


def screenshot(window, path):
    """只渲染本测试程序窗口，不读取用户其他窗口或屏幕。"""
    from PIL import Image
    gdi = C.WinDLL("gdi32")
    user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user.GetWindowDC.argtypes = [W.HWND]
    user.GetWindowDC.restype = W.HDC
    user.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
    user.ReleaseDC.argtypes = [W.HWND, W.HDC]
    gdi.CreateCompatibleDC.argtypes = [W.HDC]
    gdi.CreateCompatibleDC.restype = W.HDC
    gdi.CreateCompatibleBitmap.argtypes = [W.HDC, C.c_int, C.c_int]
    gdi.CreateCompatibleBitmap.restype = W.HBITMAP
    gdi.SelectObject.argtypes = [W.HDC, W.HANDLE]
    gdi.SelectObject.restype = W.HANDLE
    gdi.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, C.c_void_p, C.c_void_p, W.UINT]
    gdi.DeleteObject.argtypes = [W.HANDLE]
    gdi.DeleteDC.argtypes = [W.HDC]
    rect = W.RECT()
    user.GetWindowRect(window, C.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    # 在桌面可见范围之外渲染本测试窗口，不激活窗口或读取其他应用。
    user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
    user.ShowWindow.argtypes = [W.HWND, C.c_int]
    user.UpdateWindow.argtypes = [W.HWND]
    user.SetWindowPos(window, None, user.GetSystemMetrics(76) - width - 100,
                      user.GetSystemMetrics(77), 0, 0, 0x1 | 0x4 | 0x10)
    user.ShowWindow(window, 4)
    user.UpdateWindow(window)
    dc = user.GetWindowDC(window)
    memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    try:
        if not user.PrintWindow(window, memory, 0):
            return False
        class Header(C.Structure):
            _fields_ = [("size", W.DWORD), ("width", W.LONG), ("height", W.LONG), ("planes", W.WORD),
                        ("bits", W.WORD), ("compression", W.DWORD), ("image_size", W.DWORD),
                        ("x", W.LONG), ("y", W.LONG), ("used", W.DWORD), ("important", W.DWORD)]
        header = Header(C.sizeof(Header), width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        pixels = C.create_string_buffer(width * height * 4)
        gdi.SelectObject(memory, previous)
        if gdi.GetDIBits(dc, bitmap, 0, height, pixels, C.byref(header), 0) != height:
            return False
        image = Image.frombytes("RGB", (width, height), pixels.raw, "raw", "BGRX")
        if not image.getbbox():
            if path.exists():
                path.unlink()
            return False
        image.save(path)
        return True
    finally:
        gdi.SelectObject(memory, previous)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(window, dc)


def main():
    exe = ROOT / "attendance_mfc.exe"
    if not exe.exists():
        raise SystemExit("请先运行 build_mfc.ps1 编译MFC程序。")
    with tempfile.TemporaryDirectory(prefix="attendance_mfc_ui_") as directory:
        target = Path(directory) / exe.name
        shutil.copy2(exe, target)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen([str(target)], cwd=directory, startupinfo=startup)
        try:
            main_window = wait_for(lambda: window_for(process.pid, "大学生考勤系统 MFC"), "MFC主窗口未启动")
            for id, name, cls in [("001", "张三", "A班"), ("002", "李四", "B班")]:
                click(main_window, 1009)
                editor = wait_for(lambda: window_for(process.pid, "学生信息"), "学生编辑窗口未打开")
                for control, value in [(1020, id), (1021, name), (1022, cls)]:
                    set_field(editor, control, value)
                click(editor, 1)
                wait_for(lambda: not user.IsWindow(editor), "学生保存失败")
            assert "学生总人数：2 人" in text(user.GetDlgItem(main_window, 1008))
            select_view(main_window, 1)
            for day in (1, 2):
                click(main_window, 1009)
                editor = wait_for(lambda: window_for(process.pid, "考勤登记"), "考勤编辑窗口未打开")
                for control, value in [(1020, "001"), (1023, 10), (1024, day), (1025, 1)]:
                    set_field(editor, control, value)
                click(editor, 1)
                wait_for(lambda: not user.IsWindow(editor), "考勤保存失败")
            banner = text(user.GetDlgItem(main_window, 1008))
            assert "当前结果涉及：1 人" in banner and "出勤人数：1 人" in banner and "记录数：2 条" in banner, banner
            set_field(main_window, 1003, "B班")
            click(main_window, 1006)
            wait_for(lambda: "记录数：0 条" in text(user.GetDlgItem(main_window, 1008)), "班级筛选错误")
            set_field(main_window, 1003, "A班")
            set_field(main_window, 1004, "10-02")
            set_field(main_window, 1005, "10-02")
            click(main_window, 1006)
            wait_for(lambda: "记录数：1 条" in text(user.GetDlgItem(main_window, 1008)), "月日筛选错误")
            select_view(main_window, 4)
            assert user.SendMessageW(user.GetDlgItem(main_window, 1007), 0x1004, 0, 0) == 1
            select_view(main_window, 3)
            assert "记录数：1 条" in text(user.GetDlgItem(main_window, 1008))
            set_field(main_window, 1003, "")
            set_field(main_window, 1004, "")
            set_field(main_window, 1005, "")
            select_view(main_window, 1)
            wait_for(lambda: "记录数：2 条" in text(user.GetDlgItem(main_window, 1008)), "解除筛选错误")
            screenshot(main_window, ROOT / "mfc" / "preview.png")
            click(main_window, 2)
            process.wait(timeout=8)
            assert process.returncode == 0
            print("MFC窗口验证通过：学生新增、考勤新增、人数去重、班级/日期筛选、汇总/日报、正常退出。")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
