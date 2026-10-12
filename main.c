/* 大学生考勤系统：C11 控制台版本，源文件和数据文件均使用 UTF-8。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#define MAX_STUDENTS 500
#define MAX_RECORDS 20000
#define ID_SIZE 32
#define TEXT_SIZE 96
#define DB_FILE "attendance.db"
#define RULE_FILE "rules.ini"

typedef struct {
    char id[ID_SIZE];
    char name[TEXT_SIZE];
    char class_name[TEXT_SIZE];
} Student;

typedef struct {
    char student_id[ID_SIZE];
    int month, day, period, status;
} Attendance;

typedef struct {
    Student students[MAX_STUDENTS];
    Attendance records[MAX_RECORDS];
    int student_count, record_count;
} Database;

typedef struct {
    int id_min, id_max, id_digits_only;
    int name_min, name_max, name_strict;
    int abnormal_mask;
} Rules;

typedef struct {
    char class_name[TEXT_SIZE];
    int first_date, last_date;
} ReportFilter;

static Database db;
static const char *status_names[] = {"出勤", "旷课", "事假", "病假", "迟到", "早退"};
static const char *status_symbols[] = {"√", "X", "△", "○", "+", "–"};
static int input_ended;
static int sort_key;
static Rules rules = {1, 31, 0, 1, 31, 1, 50};
#ifdef _WIN32
static HANDLE console_output;
static WORD original_color = 7;
static int console_colors;
#endif

/* 设置控制台颜色；重定向输出不含颜色控制，非 Windows 平台保持普通文本。 */
static void set_color(int color)
{
#ifdef _WIN32
    if (console_colors) SetConsoleTextAttribute(console_output, color < 0 ? original_color : (WORD)color);
#else
    (void)color;
#endif
}

/* 解码一个 UTF-8 字符；更新偏移量，非法编码返回 0，成功返回 1。 */
static int next_codepoint(const char *text, size_t length, size_t *offset, unsigned long *value)
{
    unsigned char c;
    unsigned long result, minimum;
    size_t i = *offset;
    int extra, j;
    if (i >= length) return 0;
    c = (unsigned char)text[i++];
    if (c < 128) { result = c; extra = 0; minimum = 0; }
    else if (c >= 0xc2 && c <= 0xdf) { result = c & 31; extra = 1; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { result = c & 15; extra = 2; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { result = c & 7; extra = 3; minimum = 0x10000; }
    else return 0;
    for (j = 0; j < extra; ++j) {
        if (i >= length) return 0;
        c = (unsigned char)text[i++];
        if ((c & 0xc0) != 0x80) return 0;
        result = (result << 6) | (c & 63);
    }
    if (result < minimum || result > 0x10ffff || (result >= 0xd800 && result <= 0xdfff)) return 0;
    *offset = i; *value = result;
    return 1;
}

/* 读取一整行。参数为目标缓冲区及容量；成功返回 1，EOF 或超长返回 0。 */
static int read_line(char *buffer, size_t capacity)
{
    size_t length;
    int ch;
    if (!fgets(buffer, (int)capacity, stdin)) {
        input_ended = 1;
        return 0;
    }
    length = strlen(buffer);
    if (length && buffer[length - 1] == '\n') buffer[--length] = '\0';
    else if (!feof(stdin)) {
        ch = getchar();
        if (ch != '\n' && ch != EOF) {
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            puts("输入过长，请重新输入。");
            return 0;
        }
    }
    if (length && buffer[length - 1] == '\r') buffer[--length] = '\0';
    return 1;
}

/* 将字符串严格解析为范围内整数；参数为字符串、上下限和结果指针。 */
static int parse_number(const char *text, int low, int high, int *result)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < low || value > high) return 0;
    *result = (int)value;
    return 1;
}

/* 提示并读取范围内整数；成功返回 1，输入结束返回 0。 */
static int ask_number(const char *prompt, int low, int high, int *result)
{
    char line[128];
    while (!input_ended) {
        printf("%s", prompt);
        if (read_line(line, sizeof(line)) && parse_number(line, low, high, result)) return 1;
        if (!input_ended) printf("请输入 %d～%d 的整数。\n", low, high);
    }
    return 0;
}

/* 检查文本字段：不允许空值、分隔符、控制字符或首尾空白。 */
static int valid_text(const char *text, size_t capacity)
{
    size_t i = 0, length = strlen(text);
    unsigned long codepoint;
    if (!length || length >= capacity || isspace((unsigned char)text[0]) ||
        isspace((unsigned char)text[length - 1])) return 0;
    while (i < length) {
        if (!next_codepoint(text, length, &i, &codepoint) || codepoint < 32 ||
            (codepoint >= 127 && codepoint <= 159) || codepoint == '|' ||
            codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0xfeff) return 0;
    }
    return 1;
}

/* 学号允许 ASCII 字母、数字、下划线和连字符；保留前导零。 */
static int valid_id(const char *id)
{
    size_t i;
    if (!valid_text(id, ID_SIZE)) return 0;
    for (i = 0; id[i]; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* 按当前配置检查新增或修改学号；旧数据加载仅使用基本存储规则。 */
static int valid_configured_id(const char *id)
{
    size_t i, length = strlen(id);
    if (!valid_id(id) || length < (size_t)rules.id_min || length > (size_t)rules.id_max) return 0;
    if (rules.id_digits_only)
        for (i = 0; i < length; ++i) if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

/* 按字符数和可配置姓名规则校验；严格模式支持汉字、ASCII字母及内部连接符。 */
static int valid_name(const char *name)
{
    size_t i = 0, length = strlen(name);
    unsigned long c;
    int count = 0, previous_separator = 1;
    if (!valid_text(name, TEXT_SIZE)) return 0;
    while (i < length) {
        int letter, separator;
        if (!next_codepoint(name, length, &i, &c)) return 0;
        ++count;
        if (!rules.name_strict) continue;
        letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= 0x3400 && c <= 0x4dbf) || (c >= 0x4e00 && c <= 0x9fff) ||
                 (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x2fa1f);
        separator = c == ' ' || c == '-' || c == 0xb7;
        if (!letter && (!separator || previous_separator || i == length)) return 0;
        previous_separator = separator;
    }
    return count >= rules.name_min && count <= rules.name_max;
}

/* 显示当前规则及统计口径，无参数、无返回值。 */
static void show_rules(void)
{
    int i;
    printf("\n学号长度：%d～%d；%s。\n姓名字符数：%d～%d；严格姓名模式：%s。\n",
           rules.id_min, rules.id_max, rules.id_digits_only ? "仅数字" : "字母、数字、_、-",
           rules.name_min, rules.name_max, rules.name_strict ? "开启" : "关闭");
    puts("严格姓名模式：汉字、英文字母，可用内部空格、连字符、间隔号连接。");
    printf("异常表包含：");
    for (i = 0; i < 6; ++i) if (rules.abnormal_mask & (1 << i)) printf("%s ", status_names[i]);
    puts("\n请假合计为事假与病假；统计单位是记录次数，未登记不自动算旷课。");
    puts("学校规则未提供。可编辑 rules.ini 并重新启动；既有数据不会因规则收紧而被拒绝加载。");
    puts("修改字段直接回车可保留原值。备份保留最近一次变更前的数据，可在菜单14恢复。");
}

/* 从配置文件加载规则；缺失时使用默认值，未知、重复或矛盾配置返回 0。 */
static int load_rules(void)
{
    static const char *keys[] = {"id_min", "id_max", "id_digits_only", "name_min", "name_max", "name_strict", "abnormal_mask"};
    int *values[] = {&rules.id_min, &rules.id_max, &rules.id_digits_only, &rules.name_min,
                    &rules.name_max, &rules.name_strict, &rules.abnormal_mask};
    int seen[7] = {0}, ok = 1, line_number = 0;
    FILE *file = fopen(RULE_FILE, "rb");
    char line[256];
    if (!file) {
        if (errno == ENOENT) return 1;
        perror("无法打开规则文件"); return 0;
    }
    while (fgets(line, sizeof(line), file)) {
        char *equals;
        size_t length = strlen(line);
        int i, high;
        ++line_number;
        if (!length || (line[length - 1] != '\n' && !feof(file))) { ok = 0; break; }
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        if (!length || line[0] == '#') continue;
        equals = strchr(line, '=');
        if (!equals) { ok = 0; break; }
        *equals++ = '\0';
        for (i = 0; i < 7; ++i) if (!strcmp(line, keys[i])) break;
        high = i == 6 ? 63 : ((i == 2 || i == 5) ? 1 : 31);
        if (i == 7 || seen[i] || !parse_number(equals, (i == 2 || i == 5) ? 0 : 1, high, values[i])) {
            ok = 0; break;
        }
        seen[i] = 1;
    }
    if (ferror(file)) ok = 0;
    if (fclose(file)) ok = 0;
    if (rules.id_min > rules.id_max || rules.name_min > rules.name_max) ok = 0;
    if (!ok) printf("rules.ini 第 %d 行附近或范围设置无效，启动已停止。\n", line_number);
    return ok;
}

/* 读取学生字段，type 为 0 学号、1 姓名、2 班级；编辑时回车保留 current。 */
static int ask_student_field(const char *prompt, char *output, size_t capacity, int type, const char *current)
{
    char line[256];
    while (!input_ended) {
        printf("%s", prompt);
        if (!read_line(line, sizeof(line))) continue;
        if (!line[0] && current) { strcpy(output, current); return 1; }
        if (valid_text(line, capacity) && (type != 0 || valid_configured_id(line)) &&
            (type != 1 || valid_name(line))) { strcpy(output, line); return 1; }
        if (type == 0) printf("学号不符合规则：长度 %d～%d，%s。\n", rules.id_min, rules.id_max,
                            rules.id_digits_only ? "仅数字" : "仅字母、数字、_、-");
        else if (type == 1) printf("姓名不符合规则：%d～%d 个字符，请检查姓名格式。\n", rules.name_min, rules.name_max);
        else puts("班级不能为空、过长、含非法字符或首尾空白。");
    }
    return 0;
}

/* 提示并读取字段；is_id 指定是否按学号规则校验，成功返回 1。 */
static int ask_text(const char *prompt, char *output, size_t capacity, int is_id)
{
    char line[256];
    while (!input_ended) {
        printf("%s", prompt);
        if (read_line(line, sizeof(line)) && valid_text(line, capacity) && (!is_id || valid_id(line))) {
            strcpy(output, line);
            return 1;
        }
        if (!input_ended) printf("输入无效：不能为空、过长、含 | 或首尾空白。%s\n",
                                is_id ? "学号只能包含字母、数字、_、-。" : "");
    }
    return 0;
}

/* 检查月日是否合法；文档未要求年份，因此允许 2 月 29 日。 */
static int valid_date(int month, int day)
{
    static const int days[] = {31,29,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 && day >= 1 && day <= days[month - 1];
}

/* 读取月日；结果写入两个指针，成功返回 1，输入结束返回 0。 */
static int ask_date(int *month, int *day)
{
    while (!input_ended) {
        if (!ask_number("月份：", 1, 12, month) || !ask_number("日期：", 1, 31, day)) return 0;
        if (valid_date(*month, *day)) return 1;
        puts("该月没有这个日期，请重新输入。");
    }
    return 0;
}

/* 在指定数据库中按学号查找学生；返回下标，未找到返回 -1。 */
static int find_student(const Database *data, const char *id)
{
    int i;
    for (i = 0; i < data->student_count; ++i)
        if (!strcmp(data->students[i].id, id)) return i;
    return -1;
}

/* 按学号、月日和时间段查找考勤；返回下标，未找到返回 -1。 */
static int find_record(const Database *data, const char *id, int month, int day, int period)
{
    int i;
    for (i = 0; i < data->record_count; ++i) {
        const Attendance *r = &data->records[i];
        if (!strcmp(r->student_id, id) && r->month == month && r->day == day && r->period == period) return i;
    }
    return -1;
}

/* 将一行按 | 拆分，保留空字段；参数包括字段数组容量，返回字段数。 */
static int split_fields(char *line, char **fields, int capacity)
{
    int count = 1;
    char *p;
    fields[0] = line;
    for (p = line; *p; ++p) if (*p == '|') {
        if (count == capacity) return -1;
        *p = '\0';
        fields[count++] = p + 1;
    }
    return count;
}

/* 严格加载数据库，先完整校验再替换内存；缺失文件视为空库，损坏返回 0。 */
static int load_database_from(const char *path, Database *target, int allow_missing)
{
    FILE *file = fopen(path, "rb");
    Database *loaded;
    char line[512], *fields[8];
    int ok = 1, line_number = 1, count;
    if (!file) {
        if (errno == ENOENT && allow_missing) return 1;
        perror("无法打开数据文件");
        return 0;
    }
    loaded = calloc(1, sizeof(*loaded));
    if (!loaded) { fclose(file); puts("内存不足。"); return 0; }
    if (!fgets(line, sizeof(line), file) ||
        (strcmp(line, "ATTENDANCE_V1\n") && strcmp(line, "ATTENDANCE_V1\r\n"))) ok = 0;
    while (ok && fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        ++line_number;
        if (!length || line[length - 1] != '\n') { ok = 0; break; }
        line[--length] = '\0';
        if (length && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, 8);
        if (count == 4 && !strcmp(fields[0], "S")) {
            Student *s;
            if (loaded->student_count == MAX_STUDENTS || !valid_id(fields[1]) ||
                !valid_text(fields[2], TEXT_SIZE) || !valid_text(fields[3], TEXT_SIZE) ||
                find_student(loaded, fields[1]) >= 0) { ok = 0; break; }
            s = &loaded->students[loaded->student_count++];
            strcpy(s->id, fields[1]); strcpy(s->name, fields[2]); strcpy(s->class_name, fields[3]);
        } else if (count == 6 && !strcmp(fields[0], "A")) {
            Attendance r;
            if (loaded->record_count == MAX_RECORDS || !valid_id(fields[1]) ||
                find_student(loaded, fields[1]) < 0 ||
                !parse_number(fields[2], 1, 12, &r.month) || !parse_number(fields[3], 1, 31, &r.day) ||
                !parse_number(fields[4], 1, 99, &r.period) || !parse_number(fields[5], 0, 5, &r.status) ||
                !valid_date(r.month, r.day) || find_record(loaded, fields[1], r.month, r.day, r.period) >= 0) {
                ok = 0; break;
            }
            strcpy(r.student_id, fields[1]);
            loaded->records[loaded->record_count++] = r;
        } else { ok = 0; break; }
    }
    if (ferror(file)) ok = 0;
    if (fclose(file)) ok = 0;
    if (ok) *target = *loaded;
    else printf("数据文件第 %d 行附近无效。程序停止，原文件保留，请检查或恢复备份。\n", line_number);
    free(loaded);
    return ok;
}

/* 将临时文件替换为目标文件；返回替换是否成功。 */
static int replace_file(const char *temporary, const char *target)
{
#ifdef _WIN32
    return MoveFileExA(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, target) == 0;
#endif
}

/* 刷新、同步并关闭文件；无论成功或失败都会关闭，返回是否成功。 */
static int finish_file(FILE *file)
{
    int ok = !ferror(file);
    if (fflush(file)) ok = 0;
#ifdef _WIN32
    if (ok && _commit(_fileno(file))) ok = 0;
#endif
    if (fclose(file)) ok = 0;
    return ok;
}

/* 将当前数据库复制为备份，原库不存在时跳过；失败不替换已有备份。 */
static int backup_database(void)
{
    FILE *source = fopen(DB_FILE, "rb"), *destination;
    const char *temporary = DB_FILE ".bak.tmp";
    unsigned char buffer[4096];
    size_t length;
    int ok = 1;
    if (!source) return errno == ENOENT;
    destination = fopen(temporary, "wb");
    if (!destination) { fclose(source); return 0; }
    while ((length = fread(buffer, 1, sizeof(buffer), source)) > 0)
        if (fwrite(buffer, 1, length, destination) != length) { ok = 0; break; }
    if (ferror(source)) ok = 0;
    if (fclose(source)) ok = 0;
    if (!finish_file(destination)) ok = 0;
    if (ok) ok = replace_file(temporary, DB_FILE ".bak");
    if (!ok) remove(temporary);
    return ok;
}

/* 先写临时文件，成功关闭后替换原文件；保存成功返回 1，失败返回 0。 */
static int save_database(void)
{
    const char *temporary = DB_FILE ".tmp";
    FILE *file = fopen(temporary, "wb");
    int i, ok = 1;
    if (!file) { perror("无法创建临时数据文件"); return 0; }
    if (fprintf(file, "ATTENDANCE_V1\n") < 0) ok = 0;
    for (i = 0; ok && i < db.student_count; ++i) {
        Student *s = &db.students[i];
        if (fprintf(file, "S|%s|%s|%s\n", s->id, s->name, s->class_name) < 0) ok = 0;
    }
    for (i = 0; ok && i < db.record_count; ++i) {
        Attendance *r = &db.records[i];
        if (fprintf(file, "A|%s|%d|%d|%d|%d\n", r->student_id, r->month, r->day, r->period, r->status) < 0) ok = 0;
    }
    if (!finish_file(file)) ok = 0;
    if (ok && !backup_database()) { puts("备份写入失败，保存取消。"); ok = 0; }
    if (ok) ok = replace_file(temporary, DB_FILE);
    if (!ok) { remove(temporary); puts("保存失败，原数据文件保留。"); }
    return ok;
}

/* 打印学生列表，无参数、无返回值。 */
static void list_students(void)
{
    int i;
    puts("\n学号 | 姓名 | 班级");
    for (i = 0; i < db.student_count; ++i) {
        Student *s = &db.students[i];
        printf("%s | %s | %s\n", s->id, s->name, s->class_name);
    }
    printf("共 %d 名学生。\n", db.student_count);
}

/* 新增学生；实际变更返回 1，取消或校验失败返回 0。 */
static int add_student(void)
{
    Student s;
    if (db.student_count == MAX_STUDENTS) { puts("学生容量已满。"); return 0; }
    if (!ask_student_field("学号：", s.id, sizeof(s.id), 0, NULL)) return 0;
    if (find_student(&db, s.id) >= 0) { puts("学号已存在，不可重复增加。"); return 0; }
    if (!ask_student_field("姓名：", s.name, sizeof(s.name), 1, NULL) ||
        !ask_student_field("班级：", s.class_name, sizeof(s.class_name), 2, NULL)) return 0;
    db.students[db.student_count++] = s;
    return 1;
}

/* 修改学生三个字段，回车保留原值；改学号时同步关联考勤，冲突返回 0。 */
static int edit_student(void)
{
    char id[ID_SIZE];
    int index, conflict, i;
    Student s;
    if (!ask_text("待修改学号：", id, sizeof(id), 1)) return 0;
    index = find_student(&db, id);
    if (index < 0) { puts("学生不存在。"); return 0; }
    s = db.students[index];
    printf("当前：%s | %s | %s\n", s.id, s.name, s.class_name);
    if (!ask_student_field("新学号（回车保留）：", s.id, sizeof(s.id), 0, db.students[index].id)) return 0;
    conflict = find_student(&db, s.id);
    if (conflict >= 0 && conflict != index) { puts("新学号已存在，修改取消。"); return 0; }
    if (!ask_student_field("新姓名（回车保留）：", s.name, sizeof(s.name), 1, db.students[index].name) ||
        !ask_student_field("新班级（回车保留）：", s.class_name, sizeof(s.class_name), 2, db.students[index].class_name)) return 0;
    if (!strcmp(s.id, db.students[index].id) && !strcmp(s.name, db.students[index].name) &&
        !strcmp(s.class_name, db.students[index].class_name)) { puts("内容未变更。"); return 0; }
    for (i = 0; i < db.record_count; ++i)
        if (!strcmp(db.records[i].student_id, id)) strcpy(db.records[i].student_id, s.id);
    db.students[index] = s;
    return 1;
}

/* 独立查询学生，包含未登记考勤的学生；无参数、无返回值。 */
static void query_students(void)
{
    int mode, i, count = 0;
    char query[TEXT_SIZE];
    if (!ask_number("学生查询：1 学号，2 姓名，3 班级：", 1, 3, &mode)) return;
    if (!ask_text("查询值（精确匹配）：", query, mode == 1 ? ID_SIZE : sizeof(query), mode == 1)) return;
    puts("学号 | 姓名 | 班级");
    for (i = 0; i < db.student_count; ++i) {
        Student *s = &db.students[i];
        if (!strcmp(query, mode == 1 ? s->id : (mode == 2 ? s->name : s->class_name))) {
            printf("%s | %s | %s\n", s->id, s->name, s->class_name); ++count;
        }
    }
    printf("共找到 %d 名学生。\n", count);
}

/* 从最近备份恢复数据库，先验证再确认；变更返回 1，保存由主循环负责。 */
static int restore_backup(void)
{
    Database *restored = calloc(1, sizeof(*restored));
    int answer, ok;
    if (!restored) { puts("内存不足。"); return 0; }
    ok = load_database_from(DB_FILE ".bak", restored, 0);
    if (!ok) { free(restored); return 0; }
    printf("备份包含 %d 名学生、%d 条考勤；将替换当前数据。\n", restored->student_count, restored->record_count);
    if (!ask_number("确认恢复最近备份？1 是，0 取消：", 0, 1, &answer) || !answer) { free(restored); return 0; }
    db = *restored;
    free(restored);
    return 1;
}

/* 删除学生并删除关联考勤，确认后执行；实际变更返回 1。 */
static int delete_student(void)
{
    char id[ID_SIZE];
    int index, i, count = 0, answer, write_index = 0;
    if (!ask_text("待删除学号：", id, sizeof(id), 1)) return 0;
    index = find_student(&db, id);
    if (index < 0) { puts("学生不存在。"); return 0; }
    for (i = 0; i < db.record_count; ++i) if (!strcmp(db.records[i].student_id, id)) ++count;
    printf("将删除 %s（%s）和 %d 条关联考勤。\n", db.students[index].name, id, count);
    if (!ask_number("确认删除？1 是，0 取消：", 0, 1, &answer) || !answer) return 0;
    memmove(&db.students[index], &db.students[index + 1],
            (size_t)(db.student_count - index - 1) * sizeof(Student));
    --db.student_count;
    for (i = 0; i < db.record_count; ++i)
        if (strcmp(db.records[i].student_id, id)) db.records[write_index++] = db.records[i];
    db.record_count = write_index;
    return 1;
}

/* 读取考勤唯一键并验证学生存在；写入记录参数，成功返回 1。 */
static int ask_record_key(Attendance *r)
{
    if (!ask_text("学号：", r->student_id, sizeof(r->student_id), 1)) return 0;
    if (find_student(&db, r->student_id) < 0) { puts("学生不存在，请先新增学生。"); return 0; }
    return ask_date(&r->month, &r->day) && ask_number("时间段编号（1～99）：", 1, 99, &r->period);
}

/* 读取六类考勤状态；参数为结果指针，成功返回 1。 */
static int ask_status(int *status)
{
    puts("0 出勤√  1 旷课X  2 事假△  3 病假○  4 迟到+  5 早退–");
    return ask_number("状态编号：", 0, 5, status);
}

/* 新增考勤，拒绝重复唯一键；实际变更返回 1。 */
static int add_attendance(void)
{
    Attendance r;
    if (db.record_count == MAX_RECORDS) { puts("考勤容量已满。"); return 0; }
    if (!ask_record_key(&r)) return 0;
    if (find_record(&db, r.student_id, r.month, r.day, r.period) >= 0) {
        puts("该学生在这个日期和时间段已有考勤，不可重复增加。"); return 0;
    }
    if (!ask_status(&r.status)) return 0;
    db.records[db.record_count++] = r;
    return 1;
}

/* 修改考勤日期、时间段和状态，检查目标键冲突；实际变更返回 1。 */
static int edit_attendance(void)
{
    Attendance r;
    int index, conflict;
    puts("请输入原考勤的学号、日期和时间段。");
    if (!ask_record_key(&r)) return 0;
    index = find_record(&db, r.student_id, r.month, r.day, r.period);
    if (index < 0) { puts("考勤记录不存在。"); return 0; }
    printf("当前状态：%s\n", status_names[db.records[index].status]);
    puts("请输入新的日期、时间段和状态（不变的项目也要输入）。");
    if (!ask_date(&r.month, &r.day) || !ask_number("新时间段编号：", 1, 99, &r.period)) return 0;
    conflict = find_record(&db, r.student_id, r.month, r.day, r.period);
    if (conflict >= 0 && conflict != index) { puts("目标日期和时间段已有记录，修改取消。"); return 0; }
    if (!ask_status(&r.status)) return 0;
    db.records[index] = r;
    return 1;
}

/* 删除指定考勤，需确认；实际变更返回 1。 */
static int delete_attendance(void)
{
    Attendance r;
    int index, answer;
    if (!ask_record_key(&r)) return 0;
    index = find_record(&db, r.student_id, r.month, r.day, r.period);
    if (index < 0) { puts("考勤记录不存在。"); return 0; }
    if (!ask_number("确认删除这条考勤？1 是，0 取消：", 0, 1, &answer) || !answer) return 0;
    memmove(&db.records[index], &db.records[index + 1],
            (size_t)(db.record_count - index - 1) * sizeof(Attendance));
    --db.record_count;
    return 1;
}

/* 将一条考勤写到指定流；参数分别为输出流和记录，无返回值。 */
static void print_record(FILE *output, const Attendance *r)
{
    const Student *s = &db.students[find_student(&db, r->student_id)];
    static const int colors[] = {10, 12, 14, 14, 13, 13};
    fprintf(output, "%s | %s | %s | %02d-%02d | %d | ", s->id, s->name, s->class_name, r->month, r->day, r->period);
    if (output == stdout) set_color(colors[r->status]);
    fprintf(output, "%s %s\n", status_names[r->status], status_symbols[r->status]);
    if (output == stdout) set_color(-1);
}

/* 显示全部或按学号、姓名、月日、班级查询的考勤，无返回值。 */
static void query_attendance(void)
{
    int mode, month = 0, day = 0, i, count = 0;
    char query[TEXT_SIZE] = "";
    if (!ask_number("查询：0 全部，1 学号，2 姓名，3 日期，4 班级：", 0, 4, &mode)) return;
    if (mode == 3) { if (!ask_date(&month, &day)) return; }
    else if (mode && !ask_text("查询值（精确匹配）：", query, mode == 1 ? ID_SIZE : sizeof(query), mode == 1)) return;
    puts("\n学号 | 姓名 | 班级 | 日期 | 时间段 | 状态");
    for (i = 0; i < db.record_count; ++i) {
        Attendance *r = &db.records[i];
        Student *s = &db.students[find_student(&db, r->student_id)];
        if (mode == 0 || (mode == 1 && !strcmp(query, s->id)) ||
            (mode == 2 && !strcmp(query, s->name)) || (mode == 3 && month == r->month && day == r->day) ||
            (mode == 4 && !strcmp(query, s->class_name))) { print_record(stdout, r); ++count; }
    }
    printf("共 %d 条考勤记录。\n", count);
}

/* qsort 比较函数：根据 sort_key 比较学号、姓名或日期，并用其他键稳定区分。 */
static int compare_records(const void *left, const void *right)
{
    const Attendance *a = left, *b = right;
    int result = 0;
    if (sort_key == 1) result = strcmp(a->student_id, b->student_id);
    else if (sort_key == 2)
        result = strcmp(db.students[find_student(&db, a->student_id)].name,
                        db.students[find_student(&db, b->student_id)].name);
    else result = (a->month * 100 + a->day) - (b->month * 100 + b->day);
    if (!result) result = strcmp(a->student_id, b->student_id);
    if (!result) result = (a->month * 100 + a->day) - (b->month * 100 + b->day);
    if (!result) result = a->period - b->period;
    return result;
}

/* 升序排序考勤并显示结果；成功执行返回 1，以便保存排序结果。 */
static int sort_attendance(void)
{
    int i;
    if (!ask_number("升序排序：1 学号，2 姓名（UTF-8编码顺序），3 日期：", 1, 3, &sort_key)) return 0;
    qsort(db.records, (size_t)db.record_count, sizeof(Attendance), compare_records);
    puts("\n学号 | 姓名 | 班级 | 日期 | 时间段 | 状态");
    for (i = 0; i < db.record_count; ++i) print_record(stdout, &db.records[i]);
    return 1;
}

/* 读取班级和日期范围筛选；日期包含首尾，成功返回 1。 */
static int ask_report_filter(ReportFilter *filter, int daily)
{
    int mode, month, day;
    memset(filter, 0, sizeof(*filter));
    if (!ask_number(daily ? "日报范围：0 全部班级，1 指定班级：" :
                    "报表范围：0 全部，1 班级，2 日期范围，3 班级及日期范围：", 0, daily ? 1 : 3, &mode)) return 0;
    if ((mode == 1 || mode == 3) && !ask_text("班级（精确匹配）：", filter->class_name, sizeof(filter->class_name), 0)) return 0;
    if (mode == 2 || mode == 3) {
        puts("起始日期：");
        if (!ask_date(&month, &day)) return 0;
        filter->first_date = month * 100 + day;
        puts("结束日期：");
        if (!ask_date(&month, &day)) return 0;
        filter->last_date = month * 100 + day;
        if (filter->first_date > filter->last_date) {
            puts("起始日期不能晚于结束日期。跨年数据需分开管理，本次报表取消。"); return 0;
        }
    }
    return 1;
}

/* 判断一条记录是否满足班级、日期、日报和异常条件，返回 1 表示纳入。 */
static int report_matches(const Attendance *r, int type, const ReportFilter *filter)
{
    const Student *s = &db.students[find_student(&db, r->student_id)];
    int date = r->month * 100 + r->day;
    if (filter->class_name[0] && strcmp(s->class_name, filter->class_name)) return 0;
    if (filter->first_date && (date < filter->first_date || date > filter->last_date)) return 0;
    if (type == 4 && !(rules.abnormal_mask & (1 << r->status))) return 0;
    return 1;
}

/* 根据类型与筛选生成文本报表；type 为 1～5，输出到指定流。 */
static void write_report(FILE *output, int type, const ReportFilter *filter)
{
    int i, j, counts[6] = {0}, total = 0;
    static const char *titles[] = {"", "考勤明细表", "考勤日报表", "考勤汇总表", "考勤异常表", "请假汇总表"};
    fprintf(output, "%s\n", titles[type]);
    fprintf(output, "班级：%s\n", filter->class_name[0] ? filter->class_name : "全部班级");
    if (filter->first_date) fprintf(output, "日期范围：%02d-%02d 至 %02d-%02d\n", filter->first_date / 100,
            filter->first_date % 100, filter->last_date / 100, filter->last_date % 100);
    else fprintf(output, "日期范围：全部日期\n");
    if (type == 1 || type == 2 || type == 4) {
        fprintf(output, "学号 | 姓名 | 班级 | 日期 | 时间段 | 状态\n");
        for (i = 0; i < db.record_count; ++i) {
            Attendance *r = &db.records[i];
            if (!report_matches(r, type, filter)) continue;
            print_record(output, r); ++counts[r->status]; ++total;
        }
        fprintf(output, "记录数：%d\n", total);
        for (i = 0; i < 6; ++i) fprintf(output, "%s：%d%s", status_names[i], counts[i], i == 5 ? "\n" : " | ");
    } else {
        fprintf(output, type == 3 ? "学号 | 姓名 | 班级 | 出勤 | 旷课 | 事假 | 病假 | 迟到 | 早退 | 记录数\n" :
                                   "学号 | 姓名 | 班级 | 事假 | 病假 | 请假合计\n");
        for (i = 0; i < db.student_count; ++i) {
            Student *s = &db.students[i];
            int subtotal = 0;
            if (filter->class_name[0] && strcmp(filter->class_name, s->class_name)) continue;
            memset(counts, 0, sizeof(counts));
            for (j = 0; j < db.record_count; ++j)
                if (!strcmp(db.records[j].student_id, s->id) && report_matches(&db.records[j], type, filter)) {
                    ++counts[db.records[j].status]; ++subtotal;
                }
            if (type == 3) {
                fprintf(output, "%s | %s | %s", s->id, s->name, s->class_name);
                for (j = 0; j < 6; ++j) fprintf(output, " | %d", counts[j]);
                fprintf(output, " | %d\n", subtotal); total += subtotal;
            } else {
                fprintf(output, "%s | %s | %s | %d | %d | %d\n", s->id, s->name, s->class_name,
                        counts[2], counts[3], counts[2] + counts[3]);
                total += counts[2] + counts[3];
            }
        }
        fprintf(output, type == 3 ? "记录总数：%d\n" : "请假总数：%d\n", total);
    }
    fprintf(output, "统计单位为考勤记录次数；未登记不自动视为旷课。\n");
    if (type == 4) {
        fprintf(output, "异常包含：");
        for (i = 0; i < 6; ++i) if (rules.abnormal_mask & (1 << i)) fprintf(output, "%s ", status_names[i]);
        fprintf(output, "\n");
    }
}

/* 写入带引号的 CSV 文本字段，转义引号并防止文本被作为公式解释。 */
static void csv_text(FILE *output, const char *text)
{
    const char *p;
    fputc('"', output);
    if (text[0] && strchr("=+-@", text[0])) fputc('\'', output);
    for (p = text; *p; ++p) { if (*p == '"') fputc('"', output); fputc(*p, output); }
    fputc('"', output);
}

/* 生成可导入表格软件的 UTF-8 CSV，输出同筛选口径的记录或分学生汇总。 */
static void write_csv_report(FILE *output, int type, const ReportFilter *filter)
{
    int i, j;
    fwrite("\xef\xbb\xbf", 1, 3, output);
    if (type == 1 || type == 2 || type == 4) {
        fprintf(output, "学号,姓名,班级,月份,日期,时间段,考勤状态\r\n");
        for (i = 0; i < db.record_count; ++i) {
            Attendance *r = &db.records[i];
            Student *s = &db.students[find_student(&db, r->student_id)];
            if (!report_matches(r, type, filter)) continue;
            csv_text(output, s->id); fputc(',', output); csv_text(output, s->name); fputc(',', output);
            csv_text(output, s->class_name);
            fprintf(output, ",%d,%d,%d,", r->month, r->day, r->period);
            csv_text(output, status_names[r->status]); fprintf(output, "\r\n");
        }
    } else {
        fprintf(output, type == 3 ? "学号,姓名,班级,出勤,旷课,事假,病假,迟到,早退,记录数\r\n" :
                                   "学号,姓名,班级,事假,病假,请假合计\r\n");
        for (i = 0; i < db.student_count; ++i) {
            Student *s = &db.students[i];
            int counts[6] = {0}, total = 0;
            if (filter->class_name[0] && strcmp(filter->class_name, s->class_name)) continue;
            for (j = 0; j < db.record_count; ++j)
                if (!strcmp(db.records[j].student_id, s->id) && report_matches(&db.records[j], type, filter)) {
                    ++counts[db.records[j].status]; ++total;
                }
            csv_text(output, s->id); fputc(',', output); csv_text(output, s->name); fputc(',', output);
            csv_text(output, s->class_name);
            if (type == 3) {
                for (j = 0; j < 6; ++j) fprintf(output, ",%d", counts[j]);
                fprintf(output, ",%d\r\n", total);
            } else fprintf(output, ",%d,%d,%d\r\n", counts[2], counts[3], counts[2] + counts[3]);
        }
    }
}

/* 通过临时文件导出一种报表；失败保留原报表，成功返回 1。 */
static int export_report(const char *path, int type, const ReportFilter *filter, int csv)
{
    char temporary[128];
    FILE *file;
    int ok;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (!file) { printf("无法创建报表临时文件：%s。\n", strerror(errno)); return 0; }
    if (csv) write_csv_report(file, type, filter);
    else write_report(file, type, filter);
    ok = finish_file(file);
    if (ok) ok = replace_file(temporary, path);
    if (!ok) { remove(temporary); puts("报表导出失败，原报表保留。"); }
    else printf("已导出到 %s。\n", path);
    return ok;
}

/* 显示报表，并按用户选择导出到固定文件；无参数、无返回值。 */
static void reports(void)
{
    static const char *stems[] = {"", "report_detail", "report_daily", "report_summary", "report_abnormal", "report_leave"};
    ReportFilter filter;
    char path[128];
    int type, month, day, export_mode;
    if (!ask_number("报表：1 明细，2 日报，3 汇总，4 异常，5 请假：", 1, 5, &type)) return;
    if (!ask_report_filter(&filter, type == 2)) return;
    if (type == 2) {
        puts("选择日报日期：");
        if (!ask_date(&month, &day)) return;
        filter.first_date = filter.last_date = month * 100 + day;
    }
    write_report(stdout, type, &filter);
    if (!ask_number("导出：0 不导出，1 文本，2 CSV，3 两种（覆盖同类旧报表）：", 0, 3, &export_mode)) return;
    if (export_mode == 1 || export_mode == 3) {
        snprintf(path, sizeof(path), "%s.txt", stems[type]); export_report(path, type, &filter, 0);
    }
    if (export_mode == 2 || export_mode == 3) {
        snprintf(path, sizeof(path), "%s.csv", stems[type]); export_report(path, type, &filter, 1);
    }
}

/* 向空库添加 3 名虚构学生和 10 个日期的 30 条演示考勤；变更返回 1。 */
static int demo_data(void)
{
    const char *names[] = {"张三", "李四", "王五"};
    int i, day, answer, id_length = 7;
    if (db.student_count || db.record_count) { puts("演示数据只能导入空数据库，现有数据保留。"); return 0; }
    for (i = 0; i < 3; ++i) if (!valid_name(names[i])) {
        puts("当前姓名规则不适用于演示姓名，请调整规则或手工录入。"); return 0;
    }
    if (id_length < rules.id_min) id_length = rules.id_min;
    if (id_length > rules.id_max) id_length = rules.id_max;
    if (!ask_number("导入虚构演示数据（3人、10个日期、10个时段、30条考勤）？1 是，0 取消：", 0, 1, &answer) || !answer) return 0;
    for (i = 0; i < 3; ++i) {
        Student *s = &db.students[db.student_count++];
        if (id_length == 7) snprintf(s->id, sizeof(s->id), "202600%d", i + 1);
        else snprintf(s->id, sizeof(s->id), "%0*d", id_length, i + 1);
        strcpy(s->name, names[i]); strcpy(s->class_name, "人文1班");
        for (day = 1; day <= 10; ++day) {
            Attendance *r = &db.records[db.record_count++];
            strcpy(r->student_id, s->id);
            r->month = 10; r->day = day; r->period = day; r->status = (day + i) % 6;
        }
    }
    puts("已生成演示数据，日期为 10 月 1 日至 10 日，时间段编号为 1～10。");
    return 1;
}

/* 输出主菜单，无参数、无返回值。 */
static void show_menu(void)
{
    int dates[13 * 32] = {0}, periods[100] = {0}, date_count = 0, period_count = 0, i;
    for (i = 0; i < db.record_count; ++i) {
        Attendance *r = &db.records[i];
        int key = r->month * 32 + r->day;
        if (!dates[key]) { dates[key] = 1; ++date_count; }
        if (!periods[r->period]) { periods[r->period] = 1; ++period_count; }
    }
    set_color(11);
    printf("\n========== 大学生考勤系统 ==========\n学生：%d / %d，考勤：%d / %d\n",
           db.student_count, MAX_STUDENTS, db.record_count, MAX_RECORDS);
    set_color(-1);
    printf("覆盖 %d 个日期、%d 个不同时间段编号\n", date_count, period_count);
    puts("1 新增学生    2 修改学生    3 删除学生    4 学生列表");
    puts("5 新增考勤    6 修改考勤    7 删除考勤    8 考勤查询");
    puts("9 考勤排序   10 统计报表   11 导入演示数据");
    puts("12 学生查询  13 使用规则   14 恢复最近备份");
    puts("0 退出（每次数据变更均自动保存）");
}

/* 程序入口：加载、循环交互、保存；返回 0 表示正常退出，1 表示初始化失败。 */
int main(void)
{
    Database *before;
    int choice, changed;
#ifdef _WIN32
    HANDLE database_lock;
    CONSOLE_SCREEN_BUFFER_INFO info;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    console_output = GetStdHandle(STD_OUTPUT_HANDLE);
    console_colors = GetConsoleScreenBufferInfo(console_output, &info) != 0;
    if (console_colors) original_color = info.wAttributes;
    database_lock = CreateFileA(DB_FILE ".lock", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (database_lock == INVALID_HANDLE_VALUE) {
        puts("无法取得数据文件锁：可能已有程序运行，或当前目录不可写。"); return 1;
    }
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("数据文件：当前工作目录下的 " DB_FILE "（UTF-8文本）。");
    if (!load_rules() || !load_database_from(DB_FILE, &db, 1)) {
#ifdef _WIN32
        CloseHandle(database_lock);
#endif
        return 1;
    }
    before = malloc(sizeof(*before));
    if (!before) {
        puts("内存不足。");
#ifdef _WIN32
        CloseHandle(database_lock);
#endif
        return 1;
    }
    while (!input_ended) {
        show_menu();
        if (!ask_number("请选择：", 0, 14, &choice) || choice == 0) break;
        *before = db;
        changed = 0;
        switch (choice) {
            case 1: changed = add_student(); break;
            case 2: changed = edit_student(); break;
            case 3: changed = delete_student(); break;
            case 4: list_students(); break;
            case 5: changed = add_attendance(); break;
            case 6: changed = edit_attendance(); break;
            case 7: changed = delete_attendance(); break;
            case 8: query_attendance(); break;
            case 9: changed = sort_attendance(); break;
            case 10: reports(); break;
            case 11: changed = demo_data(); break;
            case 12: query_students(); break;
            case 13: show_rules(); break;
            case 14: changed = restore_backup(); break;
        }
        if (changed) {
            if (save_database()) puts("操作成功，数据已保存。");
            else { db = *before; puts("本次变更已撤销，请检查目录写入权限和磁盘空间。"); }
        }
    }
    free(before);
#ifdef _WIN32
    CloseHandle(database_lock);
#endif
    puts("已退出。");
    return 0;
}
