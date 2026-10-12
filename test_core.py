"""通过MFC实际调用的C接口测试数据逻辑，不冒充MFC窗口测试。"""
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


class Student(C.Structure):
    _fields_ = [("id", C.c_char * 32), ("name", C.c_char * 96), ("class_name", C.c_char * 96)]


class Record(C.Structure):
    _fields_ = [("student_id", C.c_char * 32), ("month", C.c_int), ("day", C.c_int), ("period", C.c_int), ("status", C.c_int)]


class Database(C.Structure):
    _fields_ = [("students", Student * 500), ("records", Record * 20000), ("student_count", C.c_int), ("record_count", C.c_int)]


class Filter(C.Structure):
    _fields_ = [("class_name", C.c_char * 96), ("first_date", C.c_int), ("last_date", C.c_int)]


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O2",
                        "-DATTENDANCE_CORE_ONLY", "-shared", "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
                        "main.c", "-o", "attendance_core_test.dll"], cwd=ROOT, check=True)
        cls.api = C.CDLL(str(ROOT / "attendance_core_test.dll"))
        cls.api.AT_Data.restype = C.POINTER(Database)
        cls.api.AT_Error.restype = C.c_char_p
        cls.api.AT_SaveStudent.argtypes = [C.c_int, C.POINTER(Student)]
        cls.api.AT_SaveRecord.argtypes = [C.c_int, C.POINTER(Record)]
        cls.api.AT_Export.argtypes = [C.c_char_p, C.c_int, C.POINTER(Filter), C.c_int]
        cls.api.AT_Count.argtypes = [C.POINTER(Filter), C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_int)]

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="attendance_core_")
        self.old_directory = Path.cwd()
        os.chdir(self.temp.name)
        self.assertEqual(self.api.AT_Open(), 1)

    def tearDown(self):
        self.api.AT_Close()
        os.chdir(self.old_directory)
        self.temp.cleanup()

    def data(self):
        return self.api.AT_Data().contents

    def student(self, index=-1, id=b"001", name="张三", cls="A班"):
        return self.api.AT_SaveStudent(index, C.byref(Student(id, name.encode(), cls.encode())))

    def record(self, index=-1, id=b"001", day=1, period=1, status=0):
        return self.api.AT_SaveRecord(index, C.byref(Record(id, 10, day, period, status)))

    def count(self, filter=None):
        filter = filter or Filter()
        values = [C.c_int() for _ in range(3)]
        self.api.AT_Count(C.byref(filter), *(C.byref(x) for x in values))
        return tuple(x.value for x in values)

    def test_people_are_deduplicated_but_records_are_not(self):
        self.assertEqual(self.student(), 1)
        self.assertEqual(self.record(day=1), 1)
        self.assertEqual(self.record(day=2), 1)
        self.assertEqual(self.record(day=2, period=2, status=1), 1)
        self.assertEqual(self.count(), (1, 1, 3))
        self.assertEqual(self.count(Filter(b"", 1002, 1002)), (1, 1, 2))
        self.assertEqual(self.count(Filter(b"B", 0, 0)), (0, 0, 0))

    def test_student_edit_and_cascade(self):
        self.student(); self.record()
        self.assertEqual(self.student(0, b"009", "李四", "B班"), 1)
        self.assertEqual(self.data().records[0].student_id, b"009")
        self.assertEqual(self.api.AT_DeleteStudent(0), 1)
        self.assertEqual((self.data().student_count, self.data().record_count), (0, 0))

    def test_validation_duplicates_and_record_edit(self):
        self.assertEqual(self.student(id=b"bad|id"), 0)
        self.assertEqual(self.student(name="张三1"), 0)
        self.assertEqual(self.student(), 1)
        self.assertEqual(self.student(), 0)
        self.assertEqual(self.record(), 1)
        self.assertEqual(self.record(), 0)
        self.assertEqual(self.record(day=31), 1)
        self.assertEqual(self.api.AT_SaveRecord(-1, C.byref(Record(b"001", 2, 30, 1, 0))), 0)
        self.assertEqual(self.record(0, day=31), 0)
        self.assertEqual(self.record(0, day=2, status=4), 1)
        self.assertEqual(self.api.AT_DeleteRecord(0), 1)
        self.assertEqual(self.data().record_count, 1)

    def test_save_failure_rolls_back_id_and_associations(self):
        self.student(); self.record()
        Path("attendance.db.tmp").mkdir()
        self.assertEqual(self.student(0, b"009"), 0)
        self.assertEqual(self.data().students[0].id, b"001")
        self.assertEqual(self.data().records[0].student_id, b"001")

    def test_backup_restore_and_reopen(self):
        self.student(); self.record()
        self.student(id=b"002", name="李四")
        self.assertEqual(self.api.AT_Restore(), 1)
        self.assertEqual(self.data().student_count, 1)
        self.api.AT_Close()
        self.assertEqual(self.api.AT_Open(), 1)
        self.assertEqual(self.count(), (1, 1, 1))

    def test_chinese_export_path_and_filter(self):
        self.student(); self.record(); self.record(day=2, status=2)
        directory = Path("中文报表目录")
        directory.mkdir()
        filter = Filter(b"", 1002, 1002)
        path = str(directory / "考勤汇总.csv").encode()
        self.assertEqual(self.api.AT_Export(path, 3, C.byref(filter), 1), 1)
        content = Path(path.decode()).read_text(encoding="utf-8-sig")
        self.assertIn('"001","张三","A班",0,0,1,0,0,0,1', content)

    def test_demo_and_sort(self):
        self.assertEqual(self.api.AT_Demo(), 1)
        self.assertEqual(self.count(), (3, 3, 30))
        self.assertEqual(self.api.AT_Sort(3), 1)
        dates = [self.data().records[i].day for i in range(30)]
        self.assertEqual(dates, sorted(dates))
        self.assertEqual(self.api.AT_Demo(), 0)

    def test_reopen_empty_directory_resets_state(self):
        self.student(); self.api.AT_Close()
        Path("empty").mkdir()
        os.chdir("empty")
        self.assertEqual(self.api.AT_Open(), 1)
        self.assertEqual(self.data().student_count, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
