// MFC负责窗口与交互，共用C接口负责校验、数据变更、文件保存与统计。
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include <vector>
#include <cstring>
#include <cwchar>
#include <direct.h>
#include "resource.h"
#include "Theme.h"
#include <uxtheme.h>
#include "../attendance_core.h"

// UTF-8与Unicode窗口字符串转换，非法输入返回空字符串。
static CString FromUtf8(const char* text)
{
    CString result;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (size > 0) {
        LPWSTR buffer = result.GetBuffer(size);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, buffer, size);
        result.ReleaseBuffer();
    }
    return result;
}

// 检查字节容量后转换；不会截断汉字或学号。
static bool ToUtf8(const CString& text, char* output, int capacity)
{
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    return size > 0 && size <= capacity && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, output, capacity, nullptr, nullptr) != 0;
}

static void CoreError() { AfxMessageBox(FromUtf8(AT_Error()), MB_ICONERROR); }

// 界面保留题目要求的六种考勤符号，统计仍使用状态编号。
static CString StatusText(int status) {
    static const wchar_t* symbols[] = {L"√", L"X", L"△", L"○", L"+", L"–"};
    CString result = FromUtf8(AT_Status(status));
    if (status >= 0 && status < 6) { result += L" "; result += symbols[status]; }
    return result;
}

// 编辑对话框在保存失败时保持打开，用户输入不会丢失。
class StudentDialog : public StyledDialog {
public:
    explicit StudentDialog(int index) : StyledDialog(IDD_STUDENT), index_(index) {}
protected:
    BOOL OnInitDialog() override {
        StyledDialog::OnInitDialog();
        SetWindowText(index_ < 0 ? L"新增学生" : L"修改学生");
        if (index_ >= 0) {
            const Student& s = AT_Data()->students[index_];
            SetDlgItemText(IDC_ID, FromUtf8(s.id)); SetDlgItemText(IDC_NAME, FromUtf8(s.name));
            SetDlgItemText(IDC_STUDENTCLASS, FromUtf8(s.class_name));
        }
        return TRUE;
    }
    void OnOK() override {
        Student s{};
        CString id, name, className;
        GetDlgItemText(IDC_ID, id); GetDlgItemText(IDC_NAME, name); GetDlgItemText(IDC_STUDENTCLASS, className);
        if (!ToUtf8(id, s.id, ID_SIZE) || !ToUtf8(name, s.name, TEXT_SIZE) || !ToUtf8(className, s.class_name, TEXT_SIZE)) {
            AfxMessageBox(L"字段过长或编码无效。", MB_ICONERROR); return;
        }
        if (!AT_SaveStudent(index_, &s)) { CoreError(); return; }
        CDialogEx::OnOK();
    }
private:
    int index_;
};

class RecordDialog : public StyledDialog {
public:
    explicit RecordDialog(int index) : StyledDialog(IDD_RECORD), index_(index) {}
protected:
    BOOL OnInitDialog() override {
        StyledDialog::OnInitDialog();
        SetWindowText(index_ < 0 ? L"新增考勤" : L"修改考勤");
        CComboBox* status = static_cast<CComboBox*>(GetDlgItem(IDC_STATUS));
        for (int i = 0; i < 6; ++i) status->AddString(StatusText(i));
        Attendance r{}; r.month = 1; r.day = 1; r.period = 1;
        if (index_ >= 0) r = AT_Data()->records[index_];
        SetDlgItemText(IDC_ID, FromUtf8(r.student_id));
        SetDlgItemInt(IDC_MONTH, r.month); SetDlgItemInt(IDC_DAY, r.day); SetDlgItemInt(IDC_PERIOD, r.period);
        status->SetCurSel(r.status); return TRUE;
    }
    void OnOK() override {
        Attendance r{}; CString id; BOOL monthOK, dayOK, periodOK;
        GetDlgItemText(IDC_ID, id);
        if (!ToUtf8(id, r.student_id, ID_SIZE)) { AfxMessageBox(L"学号过长或编码无效。"); return; }
        r.month = GetDlgItemInt(IDC_MONTH, &monthOK); r.day = GetDlgItemInt(IDC_DAY, &dayOK);
        r.period = GetDlgItemInt(IDC_PERIOD, &periodOK);
        r.status = static_cast<CComboBox*>(GetDlgItem(IDC_STATUS))->GetCurSel();
        if (!monthOK || !dayOK || !periodOK) { AfxMessageBox(L"月份、日期和时间段必须是整数。"); return; }
        if (!AT_SaveRecord(index_, &r)) { CoreError(); return; }
        CDialogEx::OnOK();
    }
private:
    int index_;
};

// 主窗口：学生/考勤可编辑，五种报表只读，并按题目要求保存文本。
class MainDialog : public StyledDialog {
public:
    MainDialog() : StyledDialog(IDD_MAIN) {}
protected:
    StyledList table_;
    CImageList rowSpacing_;
    std::vector<CString> columnTitles_;
    CRect searchArea_, countsArea_;
    bool ready_ = false;
    ReportFilter filter_{};
    int mode_ = 0;

    BOOL OnInitDialog() override {
        StyledDialog::OnInitDialog(); table_.SubclassDlgItem(IDC_TABLE, this);
        table_.ModifyStyle(WS_BORDER, 0, SWP_FRAMECHANGED);
        table_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        table_.SetBkColor(Palette::Surface); table_.SetTextBkColor(Palette::Surface); table_.SetTextColor(Palette::Text);
        SetWindowTheme(table_.GetSafeHwnd(), L"Explorer", nullptr);
        rowSpacing_.Create(1, Scale(30), ILC_COLOR32, 1, 1); table_.SetImageList(&rowSpacing_, LVSIL_SMALL);
        const wchar_t* views[] = {L"学生管理", L"考勤记录", L"考勤明细表", L"考勤日报表", L"考勤汇总表", L"考勤异常表", L"请假汇总表"};
        CComboBox* view = static_cast<CComboBox*>(GetDlgItem(IDC_VIEW));
        for (const auto* title : views) view->AddString(title);
        view->SetCurSel(0);
        CComboBox* sort = static_cast<CComboBox*>(GetDlgItem(IDC_SORTKEY));
        sort->AddString(L"学号"); sort->AddString(L"姓名"); sort->AddString(L"日期"); sort->SetCurSel(0);
        ready_ = true;
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor);
        int width = min(Scale(1100), monitor.rcWork.right - monitor.rcWork.left - Scale(40));
        int height = min(Scale(720), monitor.rcWork.bottom - monitor.rcWork.top - Scale(40));
        SetWindowPos(nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        CenterWindow(); CRect area; GetClientRect(&area); Layout(area.Width(), area.Height());
        OnView(); return TRUE;
    }

    // 按当前客户区尺寸排列控件；使用DPI缩放后的像素，不改变数据或筛选条件。
    void Layout(int width, int height) {
        if (!ready_) return;
        int margin = Scale(24), gap = Scale(12), header = Scale(92), control = Scale(36);
        auto place = [this](int id, int x, int y, int w, int h) { GetDlgItem(id)->MoveWindow(x,y,max(1,w),max(1,h)); };
        place(IDC_TITLE, margin + Scale(56), Scale(18), width - 2*margin - Scale(56), Scale(36));
        place(IDC_SUBTITLE, margin + Scale(56), Scale(58), width - 2*margin - Scale(56), Scale(20));
        int y = header + Scale(18), x = margin + Scale(18);
        searchArea_.SetRect(margin, y, width-margin, y+Scale(90));
        place(IDC_VIEW_LABEL, x, y+Scale(21), Scale(38), Scale(22)); x += Scale(44);
        place(IDC_VIEW, x, y+Scale(15), Scale(188), Scale(230)); x += Scale(188)+gap;
        place(IDC_QUERYTYPE, x, y+Scale(15), Scale(90), Scale(130)); x += Scale(90)+gap;
        int queryWidth = width - margin - Scale(18) - x - gap - Scale(116);
        place(IDC_QUERY, x, y+Scale(15), queryWidth, control);
        place(IDC_REFRESH, x+queryWidth+gap, y+Scale(13), Scale(116), Scale(40));
        place(IDC_HINT, margin+Scale(18), y+Scale(61), width-2*margin-Scale(36), Scale(18));
        y += Scale(106);
        countsArea_.SetRect(margin,y,width-margin,y+Scale(52));
        place(IDC_COUNTS, margin+Scale(18), y+Scale(15), width-2*margin-Scale(36), Scale(24));
        y += Scale(68);
        place(IDC_TABLE, margin+1,y,width-2*margin-2,height-y-Scale(102));
        int actions = height-Scale(83);
        place(IDC_ADD,margin,actions,Scale(96),Scale(40));
        place(IDC_EDIT,margin+Scale(108),actions,Scale(82),Scale(40));
        place(IDC_DELETE,margin+Scale(202),actions,Scale(82),Scale(40));
        place(IDC_SORT_LABEL,margin+Scale(310),actions+Scale(10),Scale(60),Scale(22));
        place(IDC_SORTKEY,margin+Scale(375),actions+Scale(2),Scale(100),Scale(130));
        place(IDC_SORT,margin+Scale(487),actions,Scale(76),Scale(40));
        place(IDCANCEL,width-margin-Scale(80),actions,Scale(80),Scale(40));
        place(IDC_FOOTER,margin,height-Scale(29),width-2*margin,Scale(18));
        ResizeColumns(); Invalidate(FALSE);
    }

    // 根据列含义分配宽度，汇总中的数字列保持紧凑。
    void ResizeColumns() {
        if (columnTitles_.empty()) return;
        CRect area; table_.GetClientRect(&area); int available = max(1,area.Width()-Scale(20));
        const int record[] = {18,17,22,14,12,17}, summary[] = {15,13,14,8,8,8,8,8,8,10}, students[] = {25,25,50};
        for (size_t i=0; i<columnTitles_.size(); ++i) {
            int weight = columnTitles_.size()==3 ? students[i] : (columnTitles_.size()==10 ? summary[i] : record[i]);
            table_.SetColumnWidth(static_cast<int>(i),max(Scale(76),available*weight/100));
        }
    }

    afx_msg void OnSize(UINT type, int width, int height) { StyledDialog::OnSize(type,width,height); Layout(width,height); }
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info) {
        StyledDialog::OnGetMinMaxInfo(info);
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST),&monitor);
        info->ptMinTrackSize.x = min(Scale(900),monitor.rcWork.right-monitor.rcWork.left-Scale(24));
        info->ptMinTrackSize.y = min(Scale(600),monitor.rcWork.bottom-monitor.rcWork.top-Scale(24));
    }
    afx_msg void OnPaint() {
        CPaintDC dc(this); int saved=dc.SaveDC(); CRect area; GetClientRect(&area); dc.FillSolidRect(area,Palette::Background);
        dc.FillSolidRect(0,0,area.Width(),Scale(92),Palette::Surface);
        CPen line(PS_SOLID,1,Palette::Border); CBrush surface(Palette::Surface);
        dc.SelectObject(&line); dc.SelectObject(&surface);
        dc.RoundRect(searchArea_,CPoint(Scale(16),Scale(16))); dc.RoundRect(countsArea_,CPoint(Scale(16),Scale(16)));
        CRect mark(Scale(24),Scale(25),Scale(64),Scale(65)); CBrush blue(Palette::Primary); CPen blueLine(PS_SOLID,1,Palette::Primary);
        dc.SelectObject(&blue); dc.SelectObject(&blueLine); dc.RoundRect(mark,CPoint(Scale(14),Scale(14)));
        dc.SetTextColor(Palette::Surface); dc.SetBkMode(TRANSPARENT); dc.SelectObject(&strongFont_);
        dc.DrawText(L"考",mark,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        dc.RestoreDC(saved);
    }
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* control, UINT kind) {
        HBRUSH brush = StyledDialog::OnCtlColor(dc,control,kind);
        int id = control->GetDlgCtrlID();
        if (id==IDC_TITLE || id==IDC_SUBTITLE || id==IDC_COUNTS || id==IDC_VIEW_LABEL || id==IDC_HINT) {
            dc->SetBkMode(TRANSPARENT); dc->SetTextColor(id==IDC_SUBTITLE || id==IDC_HINT ? Palette::Muted : Palette::Text);
            return surfaceBrush_;
        }
        if (id==IDC_FOOTER || id==IDC_SORT_LABEL) dc->SetTextColor(Palette::Muted);
        return brush;
    }

    // 输入提示随查询字段更新，不自动改变已输入内容或触发查询。
    afx_msg void OnQueryType() {
        int field=static_cast<CComboBox*>(GetDlgItem(IDC_QUERYTYPE))->GetCurSel();
        const wchar_t* cue=field==2 ? L"输入月日，如 10-12" : (field==1 ? L"输入姓名" : L"输入学号");
        GetDlgItem(IDC_QUERY)->SendMessage(EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(cue));
        SetDlgItemText(IDC_HINT,field==2 ? L"日期只含月日，格式为 MM-DD；留空显示全部考勤。" : L"按所选字段精确查询，留空显示全部记录。");
        if (mode_==3) SetDlgItemText(IDC_HINT,L"填写一个月日后查看日报；报表会自动保存为文本。");
        else if (mode_>=2) SetDlgItemText(IDC_HINT,L"当前报表统计全部记录；查看后自动保存为文本。");
    }

    // 解析月日，不采用系统年份；只接受MM-DD或M-D以及合法月日。
    bool ParseDate(int control, int& value) {
        CString text; GetDlgItemText(control, text); value = 0;
        if (text.IsEmpty()) return true;
        int dash = text.Find(L'-');
        if (dash < 1 || dash > 2 || text.GetLength() - dash - 1 < 1 || text.GetLength() - dash - 1 > 2) return false;
        for (int i = 0; i < text.GetLength(); ++i) if (i != dash && (text[i] < L'0' || text[i] > L'9')) return false;
        int month = _wtoi(text.Left(dash)), day = _wtoi(text.Mid(dash + 1));
        static const int days[] = {31,29,31,30,31,30,31,31,30,31,30,31};
        if (month < 1 || month > 12 || day < 1 || day > days[month - 1]) return false;
        value = month * 100 + day; return true;
    }

    // 查询日期只支持一个月日；日报必须指定日期，其他报表显示全部。
    bool ReadFilter() {
        filter_.date = 0;
        int field = static_cast<CComboBox*>(GetDlgItem(IDC_QUERYTYPE))->GetCurSel();
        if (mode_ == 3 || (mode_ == 1 && field == 2)) {
            if (!ParseDate(IDC_QUERY, filter_.date) || (mode_ == 3 && !filter_.date)) {
                AfxMessageBox(L"请输入合法月日，格式为MM-DD。"); return false;
            }
        }
        return true;
    }

    // 学生/考勤可按学号或姓名精确查找，日期通过共用记录匹配接口处理。
    bool QueryMatches(const Student& student) {
        if (mode_ >= 2) return true;
        CString query; GetDlgItemText(IDC_QUERY, query);
        int field = static_cast<CComboBox*>(GetDlgItem(IDC_QUERYTYPE))->GetCurSel();
        if (query.IsEmpty() || field == 2) return true;
        return query == FromUtf8(field == 0 ? student.id : student.name);
    }

    const Student* RecordStudent(const Attendance& r) {
        const Database* data = AT_Data();
        for (int i = 0; i < data->student_count; ++i) if (!strcmp(r.student_id, data->students[i].id)) return &data->students[i];
        return nullptr;
    }

    void Columns(const std::vector<CString>& titles) {
        columnTitles_ = titles;
        table_.DeleteAllItems(); while (table_.DeleteColumn(0)) {}
        for (size_t i = 0; i < titles.size(); ++i) table_.InsertColumn(static_cast<int>(i), titles[i], LVCFMT_LEFT, 0);
        ResizeColumns();
    }

    void AddRow(const std::vector<CString>& cells, int originalIndex) {
        int row = table_.InsertItem(table_.GetItemCount(), cells[0]);
        for (size_t i = 1; i < cells.size(); ++i) table_.SetItemText(row, static_cast<int>(i), cells[i]);
        table_.SetItemData(row, static_cast<DWORD_PTR>(originalIndex));
    }

    static CString Number(int n) { CString result; result.Format(L"%d", n); return result; }

    void RefreshRows() {
        if (!ReadFilter()) { table_.DeleteAllItems(); SetDlgItemText(IDC_COUNTS, L"筛选无效，请修正后重新应用。"); return; }
        const Database* data = AT_Data();
        int records = 0, registered = 0, present = 0;
        int seen[MAX_STUDENTS] = {}, attended[MAX_STUDENTS] = {};
        if (mode_ == 0) {
            Columns({L"学号", L"姓名", L"班级"});
            for (int i = 0; i < data->student_count; ++i) if (QueryMatches(data->students[i])) {
                const Student& s = data->students[i]; AddRow({FromUtf8(s.id), FromUtf8(s.name), FromUtf8(s.class_name)}, i);
            }
            CString summary; summary.Format(L"学生总人数：%d 人    当前匹配：%d 人", data->student_count, table_.GetItemCount());
            SetDlgItemText(IDC_COUNTS, summary); return;
        }
        int reportType = mode_ >= 2 ? mode_ - 1 : 1;
        bool aggregate = reportType == 3 || reportType == 5;
        if (aggregate) {
            Columns(reportType == 3 ? std::vector<CString>{L"学号",L"姓名",L"班级",L"出勤次数",L"旷课次数",L"事假次数",L"病假次数",L"迟到次数",L"早退次数",L"记录数"} :
                std::vector<CString>{L"学号",L"姓名",L"班级",L"事假次数",L"病假次数",L"请假合计"});
            for (int i = 0; i < data->student_count; ++i) {
                const Student& s = data->students[i]; int counts[6] = {}, total = 0;
                for (int j = 0; j < data->record_count; ++j) {
                    const Attendance& r = data->records[j];
                    if (!strcmp(r.student_id, s.id) && AT_Matches(&r, reportType, &filter_)) { ++counts[r.status]; ++total; }
                }
                std::vector<CString> row{FromUtf8(s.id), FromUtf8(s.name), FromUtf8(s.class_name)};
                if (reportType == 3) { for (int n : counts) row.push_back(Number(n)); row.push_back(Number(total)); }
                else { row.push_back(Number(counts[2])); row.push_back(Number(counts[3])); row.push_back(Number(counts[2] + counts[3])); }
                AddRow(row, i);
            }
        } else {
            Columns({L"学号",L"姓名",L"班级",L"月日",L"时间段",L"考勤状态"});
        }
        for (int i = 0; i < data->record_count; ++i) {
            const Attendance& r = data->records[i]; const Student* s = RecordStudent(r);
            if (!s || !QueryMatches(*s) || !AT_Matches(&r, reportType, &filter_)) continue;
            if (reportType == 5 && r.status != 2 && r.status != 3) continue;
            ++records;
            int student = static_cast<int>(s - data->students);
            if (!seen[student]) { seen[student] = 1; ++registered; }
            if (r.status == 0 && !attended[student]) { attended[student] = 1; ++present; }
            if (!aggregate) {
                CString date; date.Format(L"%02d-%02d", r.month, r.day);
                AddRow({FromUtf8(s->id), FromUtf8(s->name), FromUtf8(s->class_name), date, Number(r.period), StatusText(r.status)}, i);
            }
        }
        CString summary;
        summary.Format(L"学生总人数：%d 人    当前结果涉及：%d 人    出勤人数：%d 人    记录数：%d 条", data->student_count, registered, present, records);
        SetDlgItemText(IDC_COUNTS, summary);
        if (mode_ >= 2 && !AT_SaveReport(reportType, filter_.date)) CoreError();
    }

    afx_msg void OnView() {
        mode_ = static_cast<CComboBox*>(GetDlgItem(IDC_VIEW))->GetCurSel();
        const wchar_t* descriptions[]={L"维护学号、姓名与班级信息",L"登记课堂考勤，查看每一次出勤情况",L"查看全部考勤明细",L"按月日查看当日考勤",L"按学生汇总六种考勤状态",L"集中查看旷课、迟到和早退记录",L"汇总事假、病假与请假总次数"};
        SetDlgItemText(IDC_SUBTITLE,descriptions[mode_]);
        SetDlgItemText(IDC_REFRESH,mode_==0 ? L"查询学生" : (mode_==1 ? L"查询考勤" : (mode_==3 ? L"查看日报" : L"刷新报表")));
        bool editable = mode_ <= 1;
        for (int id : {IDC_ADD, IDC_EDIT, IDC_DELETE}) GetDlgItem(id)->EnableWindow(editable);
        for (int id : {IDC_SORTKEY, IDC_SORT}) GetDlgItem(id)->EnableWindow(mode_ == 1);
        GetDlgItem(IDC_QUERYTYPE)->EnableWindow(editable);
        GetDlgItem(IDC_QUERY)->EnableWindow(editable || mode_ == 3);
        CComboBox* queryType = static_cast<CComboBox*>(GetDlgItem(IDC_QUERYTYPE));
        queryType->ResetContent(); queryType->AddString(L"学号"); queryType->AddString(L"姓名");
        if (mode_ != 0) queryType->AddString(L"日期");
        if (mode_ == 3) {
            queryType->SetCurSel(2); CString date;
            const Database* data = AT_Data();
            if (data->record_count) date.Format(L"%02d-%02d", data->records[0].month, data->records[0].day);
            else { SYSTEMTIME today; GetLocalTime(&today); date.Format(L"%02d-%02d", today.wMonth, today.wDay); }
            SetDlgItemText(IDC_QUERY, date);
        } else { queryType->SetCurSel(0); SetDlgItemText(IDC_QUERY, L""); }
        OnQueryType(); RefreshRows();
    }

    afx_msg void OnRefresh() { RefreshRows(); }
    int SelectedIndex() { int row = table_.GetNextItem(-1, LVNI_SELECTED); return row < 0 ? -1 : static_cast<int>(table_.GetItemData(row)); }
    void OpenEditor(int index) {
        if (mode_ == 0) { StudentDialog dialog(index); if (dialog.DoModal() == IDOK) RefreshRows(); }
        else if (mode_ == 1) { RecordDialog dialog(index); if (dialog.DoModal() == IDOK) RefreshRows(); }
    }
    afx_msg void OnAdd() { OpenEditor(-1); }
    afx_msg void OnEdit() {
        if (mode_ > 1) return;
        int index = SelectedIndex(); if (index < 0) { AfxMessageBox(L"请先选择一行。"); return; }
        OpenEditor(index);
    }
    afx_msg void OnDelete() {
        if (mode_ > 1) return;
        int index = SelectedIndex(); if (index < 0) { AfxMessageBox(L"请先选择一行。"); return; }
        CString message = L"确定删除这条考勤记录？";
        if (mode_ == 0) {
            int related = 0; const Database* data = AT_Data();
            for (int i = 0; i < data->record_count; ++i) if (!strcmp(data->records[i].student_id, data->students[index].id)) ++related;
            message.Format(L"确定删除学生 %s？同时删除 %d 条关联考勤。", FromUtf8(data->students[index].name).GetString(), related);
        }
        if (AfxMessageBox(message, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        if (!(mode_ == 0 ? AT_DeleteStudent(index) : AT_DeleteRecord(index))) CoreError();
        RefreshRows();
    }
    afx_msg void OnSort() { if (!AT_Sort(static_cast<CComboBox*>(GetDlgItem(IDC_SORTKEY))->GetCurSel() + 1)) CoreError(); RefreshRows(); }
    afx_msg void OnDoubleClick(NMHDR*, LRESULT* result) { *result = 0; OnEdit(); }
    void OnOK() override { OnRefresh(); } // Enter刷新，避免默认关闭主窗口。
    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(MainDialog, StyledDialog)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_PAINT()
    ON_WM_CTLCOLOR()
    ON_CBN_SELCHANGE(IDC_QUERYTYPE, &MainDialog::OnQueryType)
    ON_CBN_SELCHANGE(IDC_VIEW, &MainDialog::OnView)
    ON_BN_CLICKED(IDC_REFRESH, &MainDialog::OnRefresh)
    ON_BN_CLICKED(IDC_ADD, &MainDialog::OnAdd)
    ON_BN_CLICKED(IDC_EDIT, &MainDialog::OnEdit)
    ON_BN_CLICKED(IDC_DELETE, &MainDialog::OnDelete)
    ON_BN_CLICKED(IDC_SORT, &MainDialog::OnSort)
    ON_NOTIFY(NM_DBLCLK, IDC_TABLE, &MainDialog::OnDoubleClick)
END_MESSAGE_MAP()

class AttendanceApp : public CWinApp {
    int exitCode_ = 1;
public:
    int ExitInstance() override { CWinApp::ExitInstance(); return exitCode_; }
    BOOL InitInstance() override {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        CWinApp::InitInstance();
        wchar_t path[32768]; DWORD size = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!size || size == _countof(path)) { AfxMessageBox(L"无法取得程序路径。"); return FALSE; }
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) { AfxMessageBox(L"程序目录无效。"); return FALSE; }
        if (slash == path + 2 && path[1] == L':') slash[1] = 0;
        else *slash = 0;
        if (_wchdir(path) != 0) { AfxMessageBox(L"无法访问程序目录。"); return FALSE; }
        if (!AT_Open()) { CoreError(); return FALSE; }
        MainDialog dialog; m_pMainWnd = &dialog;
        exitCode_ = dialog.DoModal() == -1 ? 1 : 0;
        m_pMainWnd = nullptr;
        AT_Close(); return FALSE;
    }
};
AttendanceApp theApp;
