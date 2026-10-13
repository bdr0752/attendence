#include "Theme.h"
#include "resource.h"

BEGIN_MESSAGE_MAP(StyledButton, CButton)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
END_MESSAGE_MAP()

// item包含标准按钮的按下、禁用和键盘焦点状态，无返回值。
void StyledButton::DrawItem(LPDRAWITEMSTRUCT item)
{
    CDC dc; dc.Attach(item->hDC);
    int saved = dc.SaveDC();
    CRect area(item->rcItem); dc.FillSolidRect(area, surrounding);
    int dpi = dc.GetDeviceCaps(LOGPIXELSX), padding = MulDiv(2, dpi, 96);
    area.DeflateRect(padding, padding);
    int id = GetDlgCtrlID();
    bool primary = id == IDC_ADD || id == IDOK || id == IDC_REFRESH;
    bool danger = id == IDC_DELETE;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF fill = Palette::Surface, text = danger ? Palette::Danger : Palette::Text, border = Palette::Border;
    if (primary) { fill = Palette::Primary; text = RGB(255,255,255); border = fill; }
    if (hovered_) {
        fill = primary ? RGB(29,78,216) : (danger ? RGB(255,239,240) : RGB(237,243,253));
        border = primary ? fill : (danger ? RGB(234,174,179) : RGB(174,195,229));
    }
    if (pressed) fill = primary ? RGB(30,64,175) : RGB(224,232,244);
    if (disabled) { fill = RGB(235,239,245); text = RGB(151,162,180); border = RGB(229,234,242); }
    CBrush brush(fill); CPen pen(PS_SOLID, 1, border);
    dc.SelectObject(&brush); dc.SelectObject(&pen);
    int radius = MulDiv(12, dpi, 96);
    dc.RoundRect(area, CPoint(radius, radius));
    if (GetFont()) dc.SelectObject(GetFont());
    dc.SetBkMode(TRANSPARENT); dc.SetTextColor(text);
    CString caption; GetWindowText(caption);
    dc.DrawText(caption, area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
        CRect focus(area); focus.DeflateRect(MulDiv(5,dpi,96), MulDiv(5,dpi,96)); dc.DrawFocusRect(focus);
    }
    dc.RestoreDC(saved); dc.Detach();
}

// 只更新悬停视觉状态，标准按钮仍处理点击。
void StyledButton::OnMouseMove(UINT flags, CPoint point)
{
    if (!hovered_) {
        hovered_ = true;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, m_hWnd, 0}; TrackMouseEvent(&tracking);
        Invalidate(FALSE);
    }
    CButton::OnMouseMove(flags, point);
}
void StyledButton::OnMouseLeave() { hovered_ = false; Invalidate(FALSE); }

BEGIN_MESSAGE_MAP(StyledList, CListCtrl)
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &StyledList::OnCustomDraw)
    ON_WM_PAINT()
END_MESSAGE_MAP()

// 分阶段设置行背景和状态文字颜色，result通知原生控件继续绘制内容。
void StyledList::OnCustomDraw(NMHDR* notification, LRESULT* result)
{
    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(notification);
    *result = CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) { *result = CDRF_NOTIFYITEMDRAW; return; }
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) { *result = CDRF_NOTIFYSUBITEMDRAW; return; }
    if (draw->nmcd.dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) return;
    int row = static_cast<int>(draw->nmcd.dwItemSpec);
    bool selected = (GetItemState(row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    draw->clrTextBk = selected ? RGB(219,234,254) : (row % 2 ? RGB(247,250,254) : Palette::Surface);
    draw->clrText = Palette::Text;
    CString value = GetItemText(row, draw->iSubItem);
    if (draw->iSubItem == 5 && GetHeaderCtrl()->GetItemCount() == 6) {
        if (value.Find(L"出勤") == 0) draw->clrText = RGB(21,128,81);
        else if (value.Find(L"旷课") == 0) draw->clrText = Palette::Danger;
        else if (value.Find(L"迟到") == 0 || value.Find(L"早退") == 0) draw->clrText = RGB(161,98,7);
        else if (value.Find(L"事假") == 0 || value.Find(L"病假") == 0) draw->clrText = RGB(37,99,180);
    }
}

// 表头通知由列表接收，重绘不改变列拖动、列宽与表格索引。
BOOL StyledList::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    auto* notification = reinterpret_cast<NMHDR*>(lParam);
    CHeaderCtrl* header = GetHeaderCtrl();
    if (header && notification->hwndFrom == header->GetSafeHwnd() && notification->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(notification);
        if (draw->dwDrawStage == CDDS_PREPAINT) { *result = CDRF_NOTIFYITEMDRAW; return TRUE; }
        if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            CDC dc; dc.Attach(draw->hdc); int saved = dc.SaveDC();
            CRect area(draw->rc); dc.FillSolidRect(area, RGB(239,244,251));
            dc.FillSolidRect(area.left, area.bottom-1, area.Width(), 1, Palette::Border);
            wchar_t text[128] = {};
            HDITEM item{}; item.mask = HDI_TEXT; item.pszText = text; item.cchTextMax = _countof(text);
            header->GetItem(static_cast<int>(draw->dwItemSpec), &item);
            area.DeflateRect(MulDiv(10,dc.GetDeviceCaps(LOGPIXELSX),96), 0);
            if (GetFont()) dc.SelectObject(GetFont());
            dc.SetBkMode(TRANSPARENT); dc.SetTextColor(Palette::Muted);
            dc.DrawText(text, area, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            dc.RestoreDC(saved); dc.Detach(); *result = CDRF_SKIPDEFAULT; return TRUE;
        }
    }
    return CListCtrl::OnNotify(wParam, lParam, result);
}

// 没有匹配数据时绘制提示，不向表格中添加虚假的数据行。
void StyledList::OnPaint()
{
    Default();
    if (GetItemCount() != 0) return;
    CClientDC dc(this); CRect area; GetClientRect(&area);
    if (GetHeaderCtrl()) { CRect header; GetHeaderCtrl()->GetWindowRect(&header); area.top += header.Height(); }
    if (GetFont()) dc.SelectObject(GetFont());
    dc.SetTextColor(Palette::Muted); dc.SetBkMode(TRANSPARENT);
    dc.DrawText(L"暂无记录，请新增数据或调整查询条件", area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

BEGIN_MESSAGE_MAP(StyledDialog, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

// 以当前系统DPI创建字体并设置控件样式，初始化成功返回TRUE。
BOOL StyledDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog(); CClientDC dc(this); dpi_ = dc.GetDeviceCaps(LOGPIXELSX);
    auto font = [this](CFont& output, int points, int weight) {
        LOGFONT definition{}; definition.lfHeight = -MulDiv(points, dpi_, 72);
        definition.lfWeight = weight; definition.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(definition.lfFaceName, L"Microsoft YaHei UI"); output.CreateFontIndirect(&definition);
    };
    font(bodyFont_, 10, FW_NORMAL); font(titleFont_, 20, FW_SEMIBOLD);
    font(captionFont_, 9, FW_NORMAL); font(strongFont_, 10, FW_SEMIBOLD);
    backgroundBrush_.CreateSolidBrush(Palette::Background); surfaceBrush_.CreateSolidBrush(Palette::Surface);
    SetFont(&bodyFont_);
    for (HWND child = ::GetWindow(m_hWnd, GW_CHILD); child;) {
        HWND next = ::GetWindow(child, GW_HWNDNEXT);
        ::SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_.GetSafeHandle()), TRUE);
        wchar_t type[64] = {}; ::GetClassName(child, type, _countof(type));
        int id = ::GetDlgCtrlID(child);
        if (!wcscmp(type, L"Button")) {
            auto button = std::make_unique<StyledButton>(); button->SubclassWindow(child);
            button->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
            if (id == IDC_REFRESH) button->surrounding = Palette::Surface;
            buttons_.push_back(std::move(button));
        } else if (!wcscmp(type, L"Edit")) {
            ::SendMessage(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(Scale(8), Scale(8)));
        } else if (!wcscmp(type, L"ComboBox")) {
            ::SendMessage(child, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scale(24));
            ::SendMessage(child, CB_SETITEMHEIGHT, 0, Scale(25));
        }
        child = next;
    }
    if (GetDlgItem(IDC_TITLE)) GetDlgItem(IDC_TITLE)->SetFont(&titleFont_);
    if (GetDlgItem(IDC_SUBTITLE)) GetDlgItem(IDC_SUBTITLE)->SetFont(&captionFont_);
    if (GetDlgItem(IDC_FOOTER)) GetDlgItem(IDC_FOOTER)->SetFont(&captionFont_);
    if (GetDlgItem(IDC_COUNTS)) GetDlgItem(IDC_COUNTS)->SetFont(&strongFont_);
    return TRUE;
}

// 输入框使用白色背景，其他区域使用统一底色；不修改控件内容。
HBRUSH StyledDialog::OnCtlColor(CDC* dc, CWnd* control, UINT kind)
{
    CDialogEx::OnCtlColor(dc, control, kind);
    dc->SetTextColor(control->IsWindowEnabled() ? Palette::Text : Palette::Muted);
    if (kind == CTLCOLOR_EDIT || kind == CTLCOLOR_LISTBOX) {
        dc->SetBkColor(Palette::Surface); return surfaceBrush_;
    }
    dc->SetBkMode(TRANSPARENT); return backgroundBrush_;
}
BOOL StyledDialog::OnEraseBkgnd(CDC* dc) { CRect area; GetClientRect(&area); dc->FillSolidRect(area, Palette::Background); return TRUE; }
