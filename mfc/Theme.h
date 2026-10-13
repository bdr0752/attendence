#pragma once
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include <memory>
#include <vector>

// 仅处理显示样式，不参与数据校验、统计和文件保存。
namespace Palette {
    constexpr COLORREF Background = RGB(244, 247, 252);
    constexpr COLORREF Surface = RGB(255, 255, 255);
    constexpr COLORREF Text = RGB(30, 41, 59);
    constexpr COLORREF Muted = RGB(100, 116, 139);
    constexpr COLORREF Border = RGB(218, 226, 237);
    constexpr COLORREF Primary = RGB(37, 99, 235);
    constexpr COLORREF Danger = RGB(190, 45, 55);
}

// 保留标准按钮的点击和键盘行为，只重绘配色、圆角与焦点状态。
class StyledButton : public CButton {
public:
    COLORREF surrounding = Palette::Background;
protected:
    bool hovered_ = false;
    void DrawItem(LPDRAWITEMSTRUCT item) override;
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();
    DECLARE_MESSAGE_MAP()
};

// 列表保留原有数据与选中行为，增加交替行背景、状态颜色和表头样式。
class StyledList : public CListCtrl {
protected:
    afx_msg void OnCustomDraw(NMHDR* notification, LRESULT* result);
    afx_msg void OnPaint();
    BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result) override;
    DECLARE_MESSAGE_MAP()
};

// 共用对话框样式；字体与画刷在窗口关闭后由成员对象释放。
class StyledDialog : public CDialogEx {
public:
    explicit StyledDialog(UINT resource) : CDialogEx(resource) {}
protected:
    int dpi_ = 96;
    CFont bodyFont_, titleFont_, captionFont_, strongFont_;
    CBrush backgroundBrush_, surfaceBrush_;
    std::vector<std::unique_ptr<StyledButton>> buttons_;
    int Scale(int value) const { return MulDiv(value, dpi_, 96); }
    BOOL OnInitDialog() override;
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* control, UINT kind);
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    DECLARE_MESSAGE_MAP()
};
