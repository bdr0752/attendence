#ifndef ATTENDANCE_CORE_H
#define ATTENDANCE_CORE_H
#define MAX_STUDENTS 500
#define MAX_RECORDS 20000
#define ID_SIZE 32
#define TEXT_SIZE 96
typedef struct { char id[ID_SIZE], name[TEXT_SIZE], class_name[TEXT_SIZE]; } Student;
typedef struct { char student_id[ID_SIZE]; int month, day, period, status; } Attendance;
typedef struct { Student students[MAX_STUDENTS]; Attendance records[MAX_RECORDS]; int student_count, record_count; } Database;
typedef struct { int date; } ReportFilter;
#ifdef __cplusplus
extern "C" {
#endif
/* 所有字符串均为 UTF-8，失败返回0，AT_Error返回原因；返回的数据库指针只读。 */
int AT_Open(void);
void AT_Close(void);
const char *AT_Error(void);
const Database *AT_Data(void);
const char *AT_Status(int status);
/* index=-1新增；index>=0修改，修改学生学号会同步关联记录。 */
int AT_SaveStudent(int index, const Student *student);
int AT_DeleteStudent(int index);
int AT_SaveRecord(int index, const Attendance *record);
int AT_DeleteRecord(int index);
int AT_Sort(int key);
/* type=1明细/2日报/3汇总/4异常/5请假；日报date=MMDD，其他date=0。 */
int AT_SaveReport(int type, int date);
/* 按日期和报表类别判断记录是否匹配，匹配返回1。 */
int AT_Matches(const Attendance *record, int type, const ReportFilter *filter);
#ifdef __cplusplus
}
#endif
#endif
