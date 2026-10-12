"""通过真实控制台输入和持久化文件验证系统，所有数据隔离在临时目录。"""
from pathlib import Path
import csv
import subprocess
import tempfile
import unittest


EXE = Path(__file__).resolve().with_name("attendance.exe")


class AttendanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="attendance_test_")
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def run_app(self, *items, expected_code=0):
        result = subprocess.run(
            [str(EXE)], cwd=self.root,
            input=("\n".join(map(str, items)) + "\n").encode("utf-8"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15,
        )
        output = result.stdout.decode("utf-8", errors="strict")
        self.assertEqual(result.returncode, expected_code, output + result.stderr.decode("utf-8", errors="replace"))
        return output

    def rows(self, prefix):
        return [line for line in (self.root / "attendance.db").read_text(encoding="utf-8").splitlines()
                if line.startswith(prefix + "|")]

    def seed(self):
        self.run_app(1, "001", "张三", "人文1班", 5, "001", 10, 1, 1, 0, 0)

    def test_add_duplicate_and_restart(self):
        self.seed()
        output = self.run_app(1, "001", 5, "001", 10, 1, 1, 8, 1, "001", 0)
        self.assertIn("学号已存在", output)
        self.assertIn("已有考勤", output)
        self.assertIn("001 | 张三 | 人文1班 | 10-01 | 1 | 出勤", output)
        self.assertEqual(len(self.rows("S")), 1)
        self.assertEqual(len(self.rows("A")), 1)

    def test_invalid_input_and_date(self):
        output = self.run_app("abc", 1, "bad|id", "001", "", "张三", "人文1班",
                              5, "001", 4, 31, 4, 30, 1, 9, 4, 0)
        self.assertIn("请输入 0～14", output)
        self.assertIn("该月没有这个日期", output)
        self.assertEqual(self.rows("A"), ["A|001|4|30|1|4"])

    def test_edit_student_and_attendance_conflict(self):
        self.seed()
        output = self.run_app(2, "001", "", "张小三", "人文2班",
                              5, "001", 10, 2, 1, 1,
                              6, "001", 10, 1, 1, 10, 2, 1,
                              6, "001", 10, 1, 1, 10, 3, 2, 5,
                              8, 2, "张小三", 0)
        self.assertIn("目标日期和时间段已有记录", output)
        self.assertIn("001 | 张小三 | 人文2班 | 10-03 | 2 | 早退", output)
        self.assertEqual(self.rows("S"), ["S|001|张小三|人文2班"])
        self.assertCountEqual(self.rows("A"), ["A|001|10|3|2|5", "A|001|10|2|1|1"])

    def test_delete_confirmation_and_cascade(self):
        self.seed()
        self.run_app(3, "001", 0, 7, "001", 10, 1, 1, 0, 0)
        self.assertEqual(len(self.rows("A")), 1)
        self.run_app(7, "001", 10, 1, 1, 1, 5, "001", 10, 2, 1, 2, 3, "001", 1, 0)
        self.assertEqual(self.rows("S"), [])
        self.assertEqual(self.rows("A"), [])

    def test_all_reports_and_demo_dates(self):
        self.run_app(11, 1, 10, 1, 0, 1, 10, 2, 0, 10, 1, 1,
                     10, 3, 0, 1, 10, 4, 0, 1, 10, 5, 0, 1, 0)
        self.assertEqual(len(self.rows("S")), 3)
        records = self.rows("A")
        self.assertEqual(len(records), 30)
        self.assertEqual(len({tuple(row.split("|")[2:4]) for row in records}), 10)
        self.assertEqual(len({row.split("|")[4] for row in records}), 10)
        detail = (self.root / "report_detail.txt").read_text(encoding="utf-8")
        self.assertIn("记录数：30", detail)
        for name, count in [("出勤", 4), ("旷课", 4), ("事假", 5), ("病假", 6), ("迟到", 6), ("早退", 5)]:
            self.assertIn(f"{name}：{count}", detail)
        daily = (self.root / "report_daily.txt").read_text(encoding="utf-8")
        self.assertIn("记录数：3", daily)
        self.assertNotIn("10-02", daily)
        summary = (self.root / "report_summary.txt").read_text(encoding="utf-8")
        self.assertIn("记录总数：30", summary)
        abnormal = (self.root / "report_abnormal.txt").read_text(encoding="utf-8")
        self.assertIn("记录数：15", abnormal)
        leave = (self.root / "report_leave.txt").read_text(encoding="utf-8")
        self.assertIn("请假总数：11", leave)
        output = self.run_app(11, 0)
        self.assertIn("演示数据只能导入空数据库", output)
        self.assertEqual(len(self.rows("A")), 30)

    def test_query_filters(self):
        self.run_app(11, 1, 0)
        for mode, value, expected in [(1, "2026001", 10), (2, "李四", 10), (4, "人文1班", 30)]:
            output = self.run_app(8, mode, value, 0)
            self.assertIn(f"共 {expected} 条考勤记录", output)
        self.assertIn("共 3 条考勤记录", self.run_app(8, 3, 10, 1, 0))
        self.assertIn("共 0 条考勤记录", self.run_app(8, 2, "不存在", 0))

    def test_sort_keys(self):
        self.run_app(1, "002", "王五", "A班", 1, "001", "张三", "A班",
                     5, "002", 10, 2, 1, 0, 5, "001", 10, 3, 1, 0,
                     5, "001", 10, 1, 1, 0, 0)
        for mode, key in [(1, lambda r: (r[1], int(r[2]), int(r[3]), int(r[4]))),
                          (2, lambda r: ({"001": "张三", "002": "王五"}[r[1]], r[1], int(r[2]), int(r[3]), int(r[4]))),
                          (3, lambda r: (int(r[2]), int(r[3]), r[1], int(r[4])))]:
            self.run_app(9, mode, 0)
            rows = [r.split("|") for r in self.rows("A")]
            self.assertEqual(rows, sorted(rows, key=key))

    def test_damaged_database_not_overwritten(self):
        cases = ["BAD\n", "ATTENDANCE_V1\nA|001|10|1|1|0\n",
                 "ATTENDANCE_V1\nS|001|张三|A班\nS|001|李四|A班\n",
                 "ATTENDANCE_V1\nS|001|张三|A班\nA|001|2|30|1|0\n",
                 "ATTENDANCE_V1\nS|001|张三|A班\nA|001|10|1|1|9\n",
                 "ATTENDANCE_V1\nS|001|张三|A班\nA|001|10|1|1|0\nA|001|10|1|1|1\n"]
        for content in cases:
            with self.subTest(content=content):
                path = self.root / "attendance.db"
                path.write_bytes(content.encode("utf-8"))
                output = self.run_app(0, expected_code=1)
                self.assertIn("原文件保留", output)
                self.assertEqual(path.read_bytes(), content.encode("utf-8"))

    def test_save_failure_rolls_back(self):
        self.seed()
        original = (self.root / "attendance.db").read_bytes()
        (self.root / "attendance.db.tmp").mkdir()
        output = self.run_app(1, "002", "李四", "A班", 4, 0)
        self.assertIn("本次变更已撤销", output)
        self.assertIn("共 1 名学生", output)
        self.assertEqual((self.root / "attendance.db").read_bytes(), original)

    def test_eof_cancels_partial_action_and_crlf_load(self):
        self.run_app(1, "001", "张三")
        self.assertFalse((self.root / "attendance.db").exists())
        self.seed()
        path = self.root / "attendance.db"
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertIn("共 1 条考勤记录", self.run_app(8, 0, 0))

    def test_change_id_updates_associations_and_restart(self):
        self.seed()
        self.run_app(2, "001", "009", "", "", 0)
        self.assertEqual(self.rows("S"), ["S|009|张三|人文1班"])
        self.assertEqual(self.rows("A"), ["A|009|10|1|1|0"])
        output = self.run_app(8, 1, "009", 0)
        self.assertIn("共 1 条考勤记录", output)
        self.assertIn("009 | 张三", output)

    def test_change_id_conflict_and_unchanged_edit(self):
        self.seed()
        self.run_app(1, "002", "李四", "人文1班", 0)
        original = (self.root / "attendance.db").read_bytes()
        backup = (self.root / "attendance.db.bak").read_bytes()
        output = self.run_app(2, "001", "002", 2, "001", "", "", "", 0)
        self.assertIn("新学号已存在", output)
        self.assertIn("内容未变更", output)
        self.assertEqual((self.root / "attendance.db").read_bytes(), original)
        self.assertEqual((self.root / "attendance.db.bak").read_bytes(), backup)

    def test_student_query_without_attendance(self):
        self.run_app(1, "001", "张三", "A班", 0)
        for mode, value in [(1, "001"), (2, "张三"), (3, "A班")]:
            self.assertIn("共找到 1 名学生", self.run_app(12, mode, value, 0))
        self.assertIn("共找到 0 名学生", self.run_app(12, 1, "009", 0))

    def test_configurable_validation_and_unicode_names(self):
        (self.root / "rules.ini").write_text("id_min=4\nid_max=4\nid_digits_only=1\nname_min=2\nname_max=10\n", encoding="utf-8")
        output = self.run_app(1, "ABCD", "123", "0001", "张三1", "张", "阿·布", "A班", 0)
        self.assertIn("学号不符合规则", output)
        self.assertIn("姓名不符合规则", output)
        self.assertEqual(self.rows("S"), ["S|0001|阿·布|A班"])
        self.assertIn("共 1 名学生", self.run_app(4, 0))
        (self.root / "rules.ini").write_text("name_strict=0\n", encoding="utf-8")
        self.run_app(1, "ABC", "Émile", "A班", 0)
        self.assertEqual(len(self.rows("S")), 2)

    def test_invalid_configuration_protects_database(self):
        self.seed()
        original = (self.root / "attendance.db").read_bytes()
        for config in ["id_min=5\nid_max=4\n", "unknown=1\n", "name_strict=2\n", "id_min=1\nid_min=2\n"]:
            with self.subTest(config=config):
                (self.root / "rules.ini").write_text(config, encoding="utf-8")
                self.assertIn("启动已停止", self.run_app(0, expected_code=1))
                self.assertEqual((self.root / "attendance.db").read_bytes(), original)

    def test_old_data_survives_stricter_rules(self):
        path = self.root / "attendance.db"
        path.write_text("ATTENDANCE_V1\nS|AB|张三1|A班\n", encoding="utf-8")
        (self.root / "rules.ini").write_text("id_min=8\nid_digits_only=1\n", encoding="utf-8")
        output = self.run_app(4, 2, "AB", "", "", "B班", 0)
        self.assertIn("AB | 张三1", output)
        self.assertEqual(self.rows("S"), ["S|AB|张三1|B班"])

    def test_backup_restore_cancel_and_failure(self):
        self.seed()
        self.run_app(1, "002", "李四", "B班", 0)
        original = (self.root / "attendance.db").read_bytes()
        prior = (self.root / "attendance.db.bak").read_bytes()
        self.run_app(14, 0, 0)
        self.assertEqual((self.root / "attendance.db").read_bytes(), original)
        (self.root / "attendance.db.tmp").mkdir()
        output = self.run_app(14, 1, 4, 0)
        self.assertIn("本次变更已撤销", output)
        self.assertIn("共 2 名学生", output)
        (self.root / "attendance.db.tmp").rmdir()
        self.run_app(14, 1, 0)
        self.assertEqual((self.root / "attendance.db").read_bytes(), prior)
        self.assertEqual((self.root / "attendance.db.bak").read_bytes(), original)

    def test_csv_and_filtered_reports(self):
        self.seed()
        self.run_app(1, "002", "李四", 'B,"班',
                     5, "001", 10, 2, 1, 2,
                     5, "002", 10, 1, 1, 1,
                     10, 1, 3, "人文1班", 10, 1, 10, 1, 3,
                     10, 3, 1, 'B,"班', 2,
                     10, 5, 2, 10, 2, 10, 2, 2, 0)
        def csv_rows(filename):
            with (self.root / filename).open(encoding="utf-8-sig", newline="") as file:
                return list(csv.reader(file))
        detail = csv_rows("report_detail.csv")
        self.assertEqual(len(detail), 2)
        self.assertEqual(detail[1], ["001", "张三", "人文1班", "10", "1", "1", "出勤"])
        summary = csv_rows("report_summary.csv")
        self.assertEqual(summary[1], ["002", "李四", 'B,"班', "0", "1", "0", "0", "0", "0", "1"])
        leave = csv_rows("report_leave.csv")
        self.assertEqual(leave[1][-3:], ["1", "0", "1"])
        self.assertIn("记录数：1", (self.root / "report_detail.txt").read_text(encoding="utf-8"))
        self.assertIn("起始日期不能晚于结束日期", self.run_app(10, 1, 2, 10, 2, 10, 1, 0))

    def test_abnormal_mask_and_atomic_export(self):
        self.seed()
        (self.root / "rules.ini").write_text("abnormal_mask=1\n", encoding="utf-8")
        output = self.run_app(10, 4, 0, 1, 0)
        self.assertIn("记录数：1", output)
        report = self.root / "report_abnormal.txt"
        original = report.read_bytes()
        (self.root / "report_abnormal.txt.tmp").mkdir()
        self.assertIn("无法创建报表临时文件", self.run_app(10, 4, 0, 1, 0))
        self.assertEqual(report.read_bytes(), original)

    def test_single_instance_lock(self):
        first = subprocess.Popen([str(EXE)], cwd=self.root, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertIn("数据文件", first.stdout.readline().decode("utf-8"))
            output = self.run_app(0, expected_code=1)
            self.assertIn("无法取得数据文件锁", output)
            first.communicate("0\n".encode("utf-8"), timeout=5)
            self.assertEqual(first.returncode, 0)
        finally:
            if first.poll() is None:
                first.kill()
                first.communicate(timeout=5)
        self.assertIn("已退出", self.run_app(0))

    def test_backup_failure_preserves_data_and_prior_backup(self):
        self.seed()
        database = (self.root / "attendance.db").read_bytes()
        backup = (self.root / "attendance.db.bak").read_bytes()
        (self.root / "attendance.db.bak.tmp").mkdir()
        output = self.run_app(2, "001", "009", "", "", 8, 1, "001", 0)
        self.assertIn("备份写入失败", output)
        self.assertIn("本次变更已撤销", output)
        self.assertIn("共 1 条考勤记录", output)
        self.assertEqual((self.root / "attendance.db").read_bytes(), database)
        self.assertEqual((self.root / "attendance.db.bak").read_bytes(), backup)

    def test_invalid_utf8_and_capacity_limits(self):
        path = self.root / "attendance.db"
        invalid = b"ATTENDANCE_V1\nS|001|\xed\xa0\x80|A\n"
        path.write_bytes(invalid)
        self.assertIn("原文件保留", self.run_app(0, expected_code=1))
        self.assertEqual(path.read_bytes(), invalid)
        content = "ATTENDANCE_V1\n" + "".join(f"S|{i:04}|张三|A班\n" for i in range(500))
        path.write_bytes(content.encode("utf-8"))
        self.assertIn("学生容量已满", self.run_app(1, 0))
        self.assertEqual(len(self.rows("S")), 500)
        path.write_bytes((content + "S|0500|李四|A班\n").encode("utf-8"))
        self.assertIn("原文件保留", self.run_app(0, expected_code=1))


if __name__ == "__main__":
    unittest.main(verbosity=2)
