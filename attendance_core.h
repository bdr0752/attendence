#ifndef ATTENDANCE_CORE_H
#define ATTENDANCE_CORE_H
#define MAX_STUDENTS 500
#define MAX_RECORDS 20000
#define ID_SIZE 32
#define TEXT_SIZE 96
typedef struct { char id[ID_SIZE], name[TEXT_SIZE], class_name[TEXT_SIZE]; } Student;
typedef struct { char student_id[ID_SIZE]; int month, day, period, status; } Attendance;
typedef struct { Student students[MAX_STUDENTS]; Attendance records[MAX_RECORDS]; int student_count, record_count; } Database;
typedef struct { char class_name[TEXT_SIZE]; int first_date, last_date; } ReportFilter;
#ifdef __cplusplus
extern "C" {
#endif
/* 所有字符串均为 UTF-8，失败返回0，AT_Error返回原因；返回的数据库指针只读。 */
int AT_Open(void);
void AT_Close(void);
const char *AT_Error(void);
const Database *AT_Data(void);
const char *AT_Status(int status);
int AT_AbnormalMask(void);
/* index=-1新增；index>=0修改，修改学生学号会同步关联记录。 */
int AT_SaveStudent(int index, const Student *student);
int AT_DeleteStudent(int index);
int AT_SaveRecord(int index, const Attendance *record);
int AT_DeleteRecord(int index);
int AT_Sort(int key);
int AT_Restore(void);
int AT_Demo(void);
/* type=1明细/2日报/3汇总/4异常/5请假，csv=0文本/1CSV；日期为MMDD。 */
int AT_Export(const char *path, int type, const ReportFilter *filter, int csv);
/* 与导出使用同一筛选口径，人数按学号去重，与记录次数分别统计。 */
int AT_Matches(const Attendance *record, int type, const ReportFilter *filter);
void AT_Count(const ReportFilter *filter, int *people, int *present_people, int *records);
#ifdef __cplusplus
}
#endif
#endif
