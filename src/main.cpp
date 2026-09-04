// main.cpp - PDF 页面提取工具 Win32 GUI + 命令行入口
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>

#include "pdfextract.h"
#include "xlsx.h"

#pragma GCC diagnostic ignored "-Wunknown-pragmas"

// ---------------- 编码工具 ----------------
static std::wstring U8(const char *utf8)
{
    if (!utf8 || !*utf8)
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    return w;
}
static std::string W2U8(const std::wstring &w)
{
    if (w.empty())
        return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring ToW(const std::string &utf8) { return U8(utf8.c_str()); }

static std::wstring TrimW(const std::wstring &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n'))
        a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n'))
        b--;
    return s.substr(a, b - a);
}
static bool IsDigitsW(const std::wstring &s)
{
    if (s.empty())
        return false;
    for (wchar_t c : s)
        if (c < L'0' || c > L'9')
            return false;
    return true;
}

// ---------------- 全局状态 ----------------
static HINSTANCE g_inst;
static HWND g_hwnd, g_list, g_status;
static HWND g_btnOpen, g_lblFile;
static HWND g_btnTpl, g_btnImport, g_btnDel, g_btnClear, g_btnAdd;
static HWND g_edStart, g_edEnd, g_lblCount, g_lblHint;
static HWND g_btnExtract;
static HFONT g_font, g_fontBold;

static std::wstring g_pdfPath;
static int g_pageCount = 0;

#define IDC_LIST 1001
#define IDC_OPEN 1002
#define IDC_TPL 1003
#define IDC_IMPORT 1004
#define IDC_DEL 1005
#define IDC_CLEAR 1006
#define IDC_ADD 1007
#define IDC_START 1008
#define IDC_END 1009
#define IDC_EXTRACT 1010
#define IDC_GRP1 1011
#define IDC_GRP2 1012
#define IDC_GRP3 1013
#define IDC_LBL_S 1014
#define IDC_LBL_E 1015

static void SetStatus(const std::wstring &msg)
{
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
}

static std::wstring RangeDesc(const std::wstring &s, const std::wstring &e)
{
    if (e.empty())
        return L"第 " + s + L" 页";
    if (e == s)
        return L"第 " + s + L" 页";
    return L"第 " + s + L" ~ " + e + L" 页";
}

static int ListCount() { return ListView_GetItemCount(g_list); }

static void UpdateCountLabel()
{
    wchar_t buf[64];
    swprintf(buf, 64, L"共 %d 组", ListCount());
    SetWindowTextW(g_lblCount, buf);
}

static bool AddRangeRow(const std::wstring &s, const std::wstring &e)
{
    int i = ListCount();
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = i;
    wchar_t idx[16];
    swprintf(idx, 16, L"%d", i + 1);
    it.pszText = idx;
    int row = ListView_InsertItem(g_list, &it);
    if (row < 0)
        return false;
    ListView_SetItemText(g_list, row, 1, (LPWSTR)s.c_str());
    ListView_SetItemText(g_list, row, 2, (LPWSTR)e.c_str());
    std::wstring d = RangeDesc(s, e);
    ListView_SetItemText(g_list, row, 3, (LPWSTR)d.c_str());
    return true;
}

static void RefreshRowNumbers()
{
    int n = ListCount();
    for (int i = 0; i < n; i++)
    {
        wchar_t idx[16];
        swprintf(idx, 16, L"%d", i + 1);
        ListView_SetItemText(g_list, i, 0, idx);
    }
    UpdateCountLabel();
}

static void ClearRows()
{
    ListView_DeleteAllItems(g_list);
    UpdateCountLabel();
}

// ---------------- PDF 加载 ----------------
static bool EndsWithI(const std::wstring &s, const std::wstring &suf)
{
    if (s.size() < suf.size())
        return false;
    return _wcsicmp(s.substr(s.size() - suf.size()).c_str(), suf.c_str()) == 0;
}

static void LoadPdf(const std::wstring &path)
{
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f)
    {
        MessageBoxW(g_hwnd, L"无法打开该文件。", L"错误", MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0)
    {
        fclose(f);
        MessageBoxW(g_hwnd, L"文件为空。", L"错误", MB_ICONERROR);
        return;
    }
    std::vector<unsigned char> buf(sz);
    size_t rd = fread(buf.data(), 1, sz, f);
    fclose(f);
    buf.resize(rd);

    int count = 0;
    std::string err;
    if (!pdfx::GetPageCountFromMemory(buf.data(), buf.size(), count, err))
    {
        MessageBoxW(g_hwnd, ToW(err).c_str(), L"PDF 读取失败", MB_ICONERROR);
        return;
    }
    g_pdfPath = path;
    g_pageCount = count;
    // 只显示文件名
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    wchar_t info[512];
    swprintf(info, 512, L"已加载：%s（共 %d 页）", name.c_str(), count);
    SetWindowTextW(g_lblFile, info);
    SetStatus(L"PDF 加载成功，请设置要提取的页面范围。");
}

// ---------------- 剪贴板批量粘贴 ----------------
static std::wstring GetClipboardText(HWND hwnd)
{
    if (!OpenClipboard(hwnd))
        return L"";
    std::wstring text;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h)
    {
        const wchar_t *p = (const wchar_t *)GlobalLock(h);
        if (p)
            text = p;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return text;
}

// 解析批量文本：每行 "起始页\t结束页"（结束页可空）
static bool ParseBatchText(const std::wstring &text,
                           std::vector<std::pair<std::wstring, std::wstring>> &out)
{
    std::wstring t = text;
    for (auto &c : t)
        if (c == L'\r')
            c = L'\n';
    size_t pos = 0;
    while (pos <= t.size())
    {
        size_t nl = t.find(L'\n', pos);
        std::wstring line = (nl == std::wstring::npos) ? t.substr(pos) : t.substr(pos, nl - pos);
        if (nl == std::wstring::npos)
            pos = t.size() + 1;
        else
            pos = nl + 1;
        line = TrimW(line);
        if (line.empty())
            continue;
        // 按 tab 分列
        std::vector<std::wstring> cols;
        size_t p2 = 0;
        while (p2 <= line.size())
        {
            size_t tb = line.find(L'\t', p2);
            std::wstring c2 = (tb == std::wstring::npos) ? line.substr(p2) : line.substr(p2, tb - p2);
            if (tb == std::wstring::npos)
                p2 = line.size() + 1;
            else
                p2 = tb + 1;
            cols.push_back(TrimW(c2));
        }
        if (cols.empty() || cols[0].empty())
            continue;
        if (!IsDigitsW(cols[0]))
            continue;
        std::wstring e = (cols.size() > 1) ? cols[1] : L"";
        if (!e.empty() && !IsDigitsW(e))
            e = L"";
        out.push_back({cols[0], e});
    }
    return !out.empty();
}

static bool TryBatchPaste(HWND hwnd, const std::wstring &text)
{
    if (text.find(L'\n') == std::wstring::npos && text.find(L'\t') == std::wstring::npos)
        return false; // 非批量内容，走默认粘贴
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    if (!ParseBatchText(text, rows))
        return false;
    for (auto &r : rows)
        AddRangeRow(r.first, r.second);
    RefreshRowNumbers();
    wchar_t msg[128];
    swprintf(msg, 128, L"已从剪贴板批量导入 %d 组页面范围。", (int)rows.size());
    SetStatus(msg);
    return true;
}

// 编辑框子类化：拦截 WM_PASTE
static LRESULT CALLBACK EditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR ref)
{
    if (msg == WM_PASTE)
    {
        std::wstring text = GetClipboardText(hwnd);
        if (TryBatchPaste(hwnd, text))
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------------- 文件对话框 ----------------
static bool OpenFileDialogW(HWND parent, const wchar_t *title, const wchar_t *filter,
                            const wchar_t *defExt, std::wstring &out, bool save)
{
    wchar_t buf[1024] = {0};
    if (save && !out.empty())
    {
        wcsncpy(buf, out.c_str(), 1023);
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = title;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    ofn.lpstrDefExt = defExt;
    bool ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok)
        out = buf;
    return ok;
}

// ---------------- 提取 ----------------
struct UiRange
{
    std::wstring s, e;
    int item;
};

static bool CollectRanges(std::vector<UiRange> &out, std::wstring &errMsg, int &errItem)
{
    int n = ListCount();
    if (n == 0)
    {
        errMsg = L"请先添加至少一组页面范围。";
        errItem = -1;
        return false;
    }
    for (int i = 0; i < n; i++)
    {
        wchar_t bs[64] = {0}, be[64] = {0};
        ListView_GetItemText(g_list, i, 1, bs, 64);
        ListView_GetItemText(g_list, i, 2, be, 64);
        std::wstring s = TrimW(bs), e = TrimW(be);
        if (s.empty() && e.empty())
            continue;
        if (!IsDigitsW(s))
        {
            errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：起始页无效（须为正整数）。";
            errItem = i;
            return false;
        }
        if (!e.empty() && !IsDigitsW(e))
        {
            errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：结束页无效（须为正整数）。";
            errItem = i;
            return false;
        }
        int si = _wtoi(s.c_str()), ei = e.empty() ? si : _wtoi(e.c_str());
        if (si < 1)
        {
            errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：起始页须 ≥ 1。";
            errItem = i;
            return false;
        }
        if (!e.empty() && ei < si)
        {
            errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：结束页不能小于起始页。";
            errItem = i;
            return false;
        }
        if (g_pageCount > 0)
        {
            if (si > g_pageCount)
            {
                errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：起始页 " + s + L" 超出总页数 " + std::to_wstring(g_pageCount) + L"。";
                errItem = i;
                return false;
            }
            if (!e.empty() && ei > g_pageCount)
            {
                errMsg = L"第 " + std::to_wstring(i + 1) + L" 组：结束页 " + e + L" 超出总页数 " + std::to_wstring(g_pageCount) + L"。";
                errItem = i;
                return false;
            }
        }
        out.push_back({s, e, i});
    }
    if (out.empty())
    {
        errMsg = L"请先添加至少一组页面范围。";
        errItem = -1;
        return false;
    }
    return true;
}

static void DoExtract()
{
    if (g_pdfPath.empty())
    {
        MessageBoxW(g_hwnd, L"请先选择 PDF 文件。", L"提示", MB_ICONWARNING);
        return;
    }
    std::vector<UiRange> urs;
    std::wstring err;
    int errItem = -1;
    if (!CollectRanges(urs, err, errItem))
    {
        if (errItem >= 0)
        {
            ListView_SetItemState(g_list, errItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_list, errItem, FALSE);
        }
        MessageBoxW(g_hwnd, err.c_str(), L"无法提取", MB_ICONWARNING);
        return;
    }
    // 默认输出名
    std::wstring base = g_pdfPath;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos)
        base = base.substr(0, dot);
    std::wstring outPath = base + L"_提取结果.pdf";

    if (!OpenFileDialogW(g_hwnd, L"保存提取结果", L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0",
                         L"pdf", outPath, true))
        return;

    std::vector<pdfx::PageRange> ranges;
    int total = 0;
    for (auto &u : urs)
    {
        pdfx::PageRange r;
        r.start = _wtoi(u.s.c_str());
        r.end = u.e.empty() ? r.start : _wtoi(u.e.c_str());
        total += r.end - r.start + 1;
        ranges.push_back(r);
    }
    SetStatus(L"正在提取，请稍候…");
    std::string serr;
    std::string inU8 = W2U8(g_pdfPath), outU8 = W2U8(outPath);
    if (!pdfx::ExtractPages(inU8, outU8, ranges, serr))
    {
        SetStatus(L"提取失败");
        MessageBoxW(g_hwnd, ToW(serr).c_str(), L"提取失败", MB_ICONERROR);
        return;
    }
    wchar_t msg[256];
    swprintf(msg, 256, L"提取成功！共 %d 页，已保存到：%s", total, outPath.c_str());
    SetStatus(msg);
    MessageBoxW(g_hwnd, msg, L"完成", MB_ICONINFORMATION);
}

// ---------------- 模板与导入 ----------------
static void DoTemplate()
{
    std::wstring outPath = L"PDF页面提取-导入模板.xlsx";
    if (!OpenFileDialogW(g_hwnd, L"保存导入模板",
                         L"Excel 工作簿 (*.xlsx)\0*.xlsx\0", L"xlsx", outPath, true))
        return;
    std::string err;
    if (!pdfx::WriteTemplate(W2U8(outPath), err))
    {
        MessageBoxW(g_hwnd, ToW(err).c_str(), L"保存失败", MB_ICONERROR);
        return;
    }
    SetStatus(L"模板已保存：" + outPath);
    MessageBoxW(g_hwnd, (L"模板已保存到：\n" + outPath + L"\n\n请编辑后通过「导入 Excel」上传。").c_str(), L"模板已保存", MB_ICONINFORMATION);
}

static void DoImport()
{
    std::wstring inPath;
    if (!OpenFileDialogW(g_hwnd, L"选择 Excel 文件",
                         L"Excel 文件 (*.xlsx;*.xls)\0*.xlsx;*.xls\0所有文件 (*.*)\0*.*\0", L"xlsx", inPath, false))
        return;
    std::vector<pdfx::XlsxRow> rows;
    std::string err;
    if (!pdfx::ReadRanges(W2U8(inPath), rows, err))
    {
        MessageBoxW(g_hwnd, ToW(err).c_str(), L"导入失败", MB_ICONERROR);
        return;
    }
    ClearRows();
    for (auto &r : rows)
        AddRangeRow(ToW(r.start), ToW(r.end));
    RefreshRowNumbers();
    wchar_t msg[160];
    swprintf(msg, 160, L"已从 Excel 导入 %d 组页面范围。", (int)rows.size());
    SetStatus(msg);
}

// ---------------- 布局 ----------------
static HWND g_grp1, g_grp2, g_grp3;
static HWND g_lblS, g_lblE;

static void Layout()
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom - 24; // 状态栏高约 24
    int pad = 12, inner = 14;

    // 组1
    int g1h = 78;
    MoveWindow(g_grp1, pad, pad, W - 2 * pad, g1h, TRUE);
    MoveWindow(g_btnOpen, pad + inner, pad + 24, 130, 28, TRUE);
    MoveWindow(g_lblFile, pad + inner + 142, pad + 30, W - 2 * pad - 2 * inner - 150, 20, TRUE);

    // 组3 底部
    int g3h = 70;
    int g3y = H - pad - g3h;
    MoveWindow(g_grp3, pad, g3y, W - 2 * pad, g3h, TRUE);
    MoveWindow(g_btnExtract, pad + inner, g3y + 22, 180, 30, TRUE);

    // 组2 中间
    int g2y = pad + g1h + 8;
    int g2h = g3y - 8 - g2y;
    MoveWindow(g_grp2, pad, g2y, W - 2 * pad, g2h, TRUE);
    int ty = g2y + 22;
    int x = pad + inner;
    MoveWindow(g_btnTpl, x, ty, 140, 26, TRUE);
    x += 148;
    MoveWindow(g_btnImport, x, ty, 110, 26, TRUE);
    x += 118;
    MoveWindow(g_btnDel, x, ty, 100, 26, TRUE);
    x += 108;
    MoveWindow(g_btnClear, x, ty, 80, 26, TRUE);
    x += 88;
    MoveWindow(g_lblCount, W - pad - inner - 90, ty + 3, 90, 20, TRUE);

    int listY = ty + 34;
    int inputH = 36, hintH = 34;
    int listH = g2h - (listY - g2y) - inputH - hintH - 14;
    if (listH < 60)
        listH = 60;
    MoveWindow(g_list, pad + inner, listY, W - 2 * pad - 2 * inner, listH, TRUE);

    int iy = listY + listH + 8;
    x = pad + inner;
    MoveWindow(g_lblS, x, iy + 6, 52, 20, TRUE);
    x += 56;
    MoveWindow(g_edStart, x, iy, 90, 24, TRUE);
    x += 98;
    MoveWindow(g_lblE, x, iy + 6, 52, 20, TRUE);
    x += 56;
    MoveWindow(g_edEnd, x, iy, 90, 24, TRUE);
    x += 98;
    MoveWindow(g_btnAdd, x, iy - 1, 90, 26, TRUE);

    MoveWindow(g_lblHint, pad + inner, iy + inputH - 2, W - 2 * pad - 2 * inner, hintH, TRUE);

    MoveWindow(g_status, 0, rc.bottom - 24, W, 24, TRUE);
}

// ---------------- 窗口过程 ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        InitCommonControls();
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_fontBold = CreateFontIndirectW(&ncm.lfMessageFont);

        g_grp1 = CreateWindowW(L"BUTTON", L" 1. 选择 PDF 文件", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd, (HMENU)IDC_GRP1, g_inst, nullptr);
        g_btnOpen = CreateWindowW(L"BUTTON", L"选择 PDF 文件…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_OPEN, g_inst, nullptr);
        g_lblFile = CreateWindowW(L"STATIC", L"未加载（也可直接将 PDF 拖入本窗口）", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, g_inst, nullptr);

        g_grp2 = CreateWindowW(L"BUTTON", L" 2. 设置要提取的页面范围", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd, (HMENU)IDC_GRP2, g_inst, nullptr);
        g_btnTpl = CreateWindowW(L"BUTTON", L"下载 Excel 导入模板", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_TPL, g_inst, nullptr);
        g_btnImport = CreateWindowW(L"BUTTON", L"导入 Excel…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_IMPORT, g_inst, nullptr);
        g_btnDel = CreateWindowW(L"BUTTON", L"删除选中行", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_DEL, g_inst, nullptr);
        g_btnClear = CreateWindowW(L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_CLEAR, g_inst, nullptr);
        g_lblCount = CreateWindowW(L"STATIC", L"共 0 组", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hwnd, nullptr, g_inst, nullptr);

        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_LIST, g_inst, nullptr);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)L"#";
        col.cx = 42;
        ListView_InsertColumn(g_list, 0, &col);
        col.pszText = (LPWSTR)L"起始页";
        col.cx = 110;
        ListView_InsertColumn(g_list, 1, &col);
        col.pszText = (LPWSTR)L"结束页";
        col.cx = 110;
        ListView_InsertColumn(g_list, 2, &col);
        col.pszText = (LPWSTR)L"范围说明";
        col.cx = 220;
        ListView_InsertColumn(g_list, 3, &col);

        g_lblS = CreateWindowW(L"STATIC", L"起始页：", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_S, g_inst, nullptr);
        g_edStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 0, 0, 0, 0, hwnd, (HMENU)IDC_START, g_inst, nullptr);
        g_lblE = CreateWindowW(L"STATIC", L"结束页：", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_E, g_inst, nullptr);
        g_edEnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, 0, 0, 0, 0, hwnd, (HMENU)IDC_END, g_inst, nullptr);
        g_btnAdd = CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_ADD, g_inst, nullptr);
        g_lblHint = CreateWindowW(L"STATIC",
                                  L"提示：结束页留空 = 只提取该一页。支持从 Excel 复制两列数据后直接在「起始页」输入框中 Ctrl+V 批量粘贴。",
                                  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, g_inst, nullptr);

        g_grp3 = CreateWindowW(L"BUTTON", L" 3. 执行提取", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd, (HMENU)IDC_GRP3, g_inst, nullptr);
        g_btnExtract = CreateWindowW(L"BUTTON", L"提取页面并保存…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)IDC_EXTRACT, g_inst, nullptr);

        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, nullptr, g_inst, nullptr);

        // 统一字体
        HWND all[] = {g_grp1, g_btnOpen, g_lblFile, g_grp2, g_btnTpl, g_btnImport, g_btnDel, g_btnClear,
                      g_lblCount, g_list, g_lblS, g_edStart, g_lblE, g_edEnd, g_btnAdd, g_lblHint, g_grp3, g_btnExtract};
        for (HWND h : all)
            SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);

        SetWindowSubclass(g_edStart, EditSubclass, 1, 0);
        SetWindowSubclass(g_edEnd, EditSubclass, 2, 0);

        DragAcceptFiles(hwnd, TRUE);
        SetStatus(L"就绪。请先选择 PDF 文件。");
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = 620;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_DROPFILES:
    {
        HDROP hdrop = (HDROP)wp;
        wchar_t path[1024];
        if (DragQueryFileW(hdrop, 0, path, 1024))
        {
            if (EndsWithI(path, L".pdf"))
                LoadPdf(path);
            else if (EndsWithI(path, L".xlsx") || EndsWithI(path, L".xls"))
            {
                std::vector<pdfx::XlsxRow> rows;
                std::string err;
                if (pdfx::ReadRanges(W2U8(path), rows, err))
                {
                    ClearRows();
                    for (auto &r : rows)
                        AddRangeRow(ToW(r.start), ToW(r.end));
                    RefreshRowNumbers();
                    SetStatus(L"已从拖入的 Excel 导入页面范围。");
                }
                else
                    MessageBoxW(hwnd, ToW(err).c_str(), L"导入失败", MB_ICONERROR);
            }
            else
                MessageBoxW(hwnd, L"请拖入 PDF 或 Excel (.xlsx) 文件。", L"提示", MB_ICONWARNING);
        }
        DragFinish(hdrop);
        return 0;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wp))
        {
        case IDC_OPEN:
        {
            std::wstring p;
            if (OpenFileDialogW(hwnd, L"选择 PDF 文件", L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0", L"pdf", p, false))
                LoadPdf(p);
            return 0;
        }
        case IDC_ADD:
        {
            wchar_t bs[64] = {0}, be[64] = {0};
            GetWindowTextW(g_edStart, bs, 64);
            GetWindowTextW(g_edEnd, be, 64);
            std::wstring s = TrimW(bs), e = TrimW(be);
            if (s.empty())
            {
                MessageBoxW(hwnd, L"请输入起始页。", L"提示", MB_ICONWARNING);
                SetFocus(g_edStart);
                return 0;
            }
            if (!IsDigitsW(s))
            {
                MessageBoxW(hwnd, L"起始页须为正整数。", L"提示", MB_ICONWARNING);
                return 0;
            }
            if (!e.empty() && !IsDigitsW(e))
            {
                MessageBoxW(hwnd, L"结束页须为正整数（或留空）。", L"提示", MB_ICONWARNING);
                return 0;
            }
            if (!e.empty() && _wtoi(e.c_str()) < _wtoi(s.c_str()))
            {
                MessageBoxW(hwnd, L"结束页不能小于起始页。", L"提示", MB_ICONWARNING);
                return 0;
            }
            AddRangeRow(s, e);
            RefreshRowNumbers();
            SetWindowTextW(g_edStart, L"");
            SetWindowTextW(g_edEnd, L"");
            SetFocus(g_edStart);
            return 0;
        }
        case IDC_DEL:
        {
            int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
            if (i >= 0)
            {
                ListView_DeleteItem(g_list, i);
                RefreshRowNumbers();
            }
            else
                SetStatus(L"请先在列表中选中要删除的行。");
            return 0;
        }
        case IDC_CLEAR:
            ClearRows();
            SetStatus(L"已清空页面范围列表。");
            return 0;
        case IDC_TPL:
            DoTemplate();
            return 0;
        case IDC_IMPORT:
            DoImport();
            return 0;
        case IDC_EXTRACT:
            DoExtract();
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- 命令行模式 ----------------
static void PrintUsage()
{
    const char *usage =
        "PDF Page Extractor - command line mode\n"
        "Usage:\n"
        "  PdfPageExtractor.exe <input.pdf> <output.pdf> <ranges...>\n"
        "  ranges: page number or start-end, e.g. 3 5 9-18\n"
        "Examples:\n"
        "  PdfPageExtractor.exe in.pdf out.pdf 3 5 9-18\n"
        "  PdfPageExtractor.exe --count in.pdf\n";
    // 写到控制台（若存在）
    fwrite(usage, 1, strlen(usage), stdout);
    fflush(stdout);
    MessageBoxA(nullptr, usage, "PDF Page Extractor (CLI)", MB_OK);
}

static bool ParseCliRange(const std::string &a, pdfx::PageRange &r)
{
    size_t dash = a.find('-');
    if (dash == std::string::npos)
    {
        for (char c : a)
            if (c < '0' || c > '9')
                return false;
        r.start = r.end = atoi(a.c_str());
        return r.start >= 1;
    }
    std::string s = a.substr(0, dash), e = a.substr(dash + 1);
    if (s.empty() || e.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    for (char c : e)
        if (c < '0' || c > '9')
            return false;
    r.start = atoi(s.c_str());
    r.end = atoi(e.c_str());
    return r.start >= 1 && r.end >= r.start;
}

static int RunCli(int argc, wchar_t **argv)
{
    AttachConsole(ATTACH_PARENT_PROCESS);
    if (argc >= 3 && wcscmp(argv[1], L"--count") == 0)
    {
        std::string err;
        int count = 0;
        if (!pdfx::GetPageCount(W2U8(argv[2]), count, err))
        {
            fprintf(stderr, "Error: %s\n", err.c_str());
            return 1;
        }
        printf("%d\n", count);
        return 0;
    }
    if (argc < 4)
    {
        PrintUsage();
        return 1;
    }
    std::string in = W2U8(argv[1]), out = W2U8(argv[2]);
    std::vector<pdfx::PageRange> ranges;
    for (int i = 3; i < argc; i++)
    {
        pdfx::PageRange r;
        if (!ParseCliRange(W2U8(argv[i]), r))
        {
            fprintf(stderr, "Invalid range: %ls\n", argv[i]);
            return 1;
        }
        ranges.push_back(r);
    }
    std::string err;
    if (!pdfx::ExtractPages(in, out, ranges, err))
    {
        fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    printf("OK: extracted to %s\n", out.c_str());
    return 0;
}

// ---------------- 入口 ----------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int show)
{
    // 有命令行参数则走 CLI 模式
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1)
    {
        int r = RunCli(argc, argv);
        LocalFree(argv);
        return r;
    }
    if (argv)
        LocalFree(argv);

    g_inst = hInst;
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PdfPageExtractorWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"PDF 页面提取工具 v1.0.0",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 780, 640,
                             nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
