// Caviardeur pour Windows XP : interface Win32 classique, bilingue français / anglais.
//
// Le document est converti en PDF (Office 2007 pour Word, Excel, PowerPoint), les zones choisies
// sont supprimées du fichier par MuPDF (pas seulement masquées), puis le PDF est nettoyé et vérifié.
// Tout reste sur le poste.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine.h"
#include "office.h"
#include "resource.h"
#include "text.h"

static const wchar_t *APP_NAME = L"Caviardeur";
static const wchar_t *APP_VERSION = L"2.1";
static const wchar_t *APP_COPYRIGHT = L"Copyright © 2026 Cole";
static const wchar_t *REG_KEY = L"Software\\Caviardeur";

// ---------------------------------------------------------------- état de l'application

struct Notice
{
	int icon;               // IDI_MSG_INFO, IDI_MSG_WARNING, IDI_MSG_ERROR ; 0 = masqué
	std::wstring title, body;
};

struct Cached
{
	HBITMAP bmp;
	int w, h;
	float zoom;
};

static struct
{
	HINSTANCE inst;
	HWND main, toolbar, status, panel, view;
	HFONT font, bold, big;
	int dpi;
	HIMAGELIST images;
	HICON icons[4];         // information, avertissement, erreur, question (32 px)

	// panneau (les cadres de groupe sont dessinés par le panneau lui-même)
	HWND docLabel, docNotice, btnBrowse;
	HWND lblTerms, terms, chkSplit, lblData, chkEmail, chkTel, chkIban, chkNir, btnSearch, searchNotice;
	HWND lblZones, lblCount, btnUndo, btnClear;
	HWND chkRaster, lblRaster, btnApply, reportNotice;
	std::vector<std::pair<RECT, std::wstring> > groups;
	int panelScroll, panelHeight;

	// document
	Engine *engine;
	std::wstring work, src, pdf;
	std::vector<Zone> zones;
	std::vector<Cached> cache;

	// vue des pages
	float zoom;
	bool fit;
	int scrollX, scrollY, contentW, contentH;
	std::vector<RECT> layout;   // position de chaque page dans le contenu (pixels)
	bool drawing;
	int drawPage;
	POINT drawStart, drawEnd;
	int menuPage;

	// autotest
	bool autotest;
	std::wstring log;
} g;

static int S(int v) { return MulDiv(v, g.dpi, 96); }

static int Msg(const std::wstring &text, UINT type, int icon)
{
	if (g.autotest)
	{
		g.log += L"[message] " + text + L"\n";
		return (type & MB_YESNO) ? IDYES : IDOK;
	}
	MSGBOXPARAMSW mp;
	ZeroMemory(&mp, sizeof(mp));
	mp.cbSize = sizeof(mp);
	mp.hwndOwner = g.main;
	mp.hInstance = g.inst;
	mp.lpszText = text.c_str();
	mp.lpszCaption = APP_NAME;
	mp.dwStyle = type | MB_USERICON;
	mp.lpszIcon = MAKEINTRESOURCEW(icon);
	return MessageBoxIndirectW(&mp);
}

static void Status(const std::wstring &text, int part = 0)
{
	SendMessageW(g.status, SB_SETTEXTW, part, (LPARAM)text.c_str());
	if (part == 0)
		UpdateWindow(g.status);
}

static std::wstring GetText(HWND h)
{
	int n = GetWindowTextLengthW(h);
	std::wstring s(n + 1, L'\0');
	GetWindowTextW(h, &s[0], n + 1);
	s.resize(n);
	return s;
}

static std::wstring Lower(std::wstring s)
{
	for (size_t i = 0; i < s.size(); i++)
		if (s[i] >= L'A' && s[i] <= L'Z')
			s[i] = s[i] + 32;
	return s;
}

static std::wstring Extension(const std::wstring &p)
{
	size_t dot = p.find_last_of(L'.'), slash = p.find_last_of(L"\\/");
	if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
		return L"";
	return Lower(p.substr(dot));
}

static std::wstring BaseName(const std::wstring &p)
{
	size_t slash = p.find_last_of(L"\\/");
	return slash == std::wstring::npos ? p : p.substr(slash + 1);
}

static std::wstring IntStr(int v)
{
	wchar_t b[32];
	wsprintfW(b, L"%d", v);
	return b;
}

static bool IsImage(const std::wstring &e)
{
	return e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".tif" || e == L".tiff" || e == L".bmp" || e == L".gif";
}

// ---------------------------------------------------------------- langue (mémorisée dans le registre)

static void LoadLanguage()
{
	HKEY k;
	wchar_t v[8] = L"";
	DWORD size = sizeof(v), type = 0;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS)
	{
		if (RegQueryValueExW(k, L"Language", NULL, &type, (BYTE *)v, &size) != ERROR_SUCCESS || type != REG_SZ)
			v[0] = 0;
		RegCloseKey(k);
	}
	if (lstrcmpiW(v, L"en") == 0)
		g_english = true;
	else if (lstrcmpiW(v, L"fr") == 0)
		g_english = false;
	else   // premier lancement : langue de Windows
		g_english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_FRENCH;
}

static void SaveLanguage()
{
	HKEY k;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS)
		return;
	const wchar_t *v = g_english ? L"en" : L"fr";
	RegSetValueExW(k, L"Language", 0, REG_SZ, (const BYTE *)v, (DWORD)((lstrlenW(v) + 1) * sizeof(wchar_t)));
	RegCloseKey(k);
}

// ---------------------------------------------------------------- encadrés de message (panneau)

static Notice *NoticeOf(HWND h) { return (Notice *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static int NoticeHeight(HWND h, int width)
{
	Notice *n = NoticeOf(h);
	if (!n || !n->icon)
		return 0;
	HDC dc = GetDC(h);
	RECT r = { 0, 0, width - S(52), 0 };
	std::wstring all = n->title + L"\n" + n->body;
	HFONT old = (HFONT)SelectObject(dc, g.font);
	DrawTextW(dc, all.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(h, dc);
	return std::max<int>(r.bottom + S(14), S(46));
}

static void PaintNotice(HWND h, HDC dc)
{
	RECT r;
	GetClientRect(h, &r);
	FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
	DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT);
	Notice *n = NoticeOf(h);
	if (n && n->icon)
	{
		int idx = n->icon == IDI_MSG_INFO ? 0 : n->icon == IDI_MSG_WARNING ? 1 : 2;
		DrawIconEx(dc, S(7), S(7), g.icons[idx], S(32), S(32), 0, NULL, DI_NORMAL);
		SetBkMode(dc, TRANSPARENT);
		RECT t = { S(46), S(7), r.right - S(6), r.bottom - S(4) };
		HFONT old = (HFONT)SelectObject(dc, g.bold);
		RECT tt = t;
		DrawTextW(dc, n->title.c_str(), -1, &tt, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
		DrawTextW(dc, n->title.c_str(), -1, &t, DT_WORDBREAK | DT_NOPREFIX);
		SelectObject(dc, g.font);
		t.top += tt.bottom - tt.top;
		DrawTextW(dc, n->body.c_str(), -1, &t, DT_WORDBREAK | DT_NOPREFIX);
		SelectObject(dc, old);
	}
}

static LRESULT CALLBACK NoticeProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_NCDESTROY:
		delete NoticeOf(h);
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		PaintNotice(h, dc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_PRINTCLIENT:
		PaintNotice(h, (HDC)wp);
		return 0;
	}
	return DefWindowProcW(h, m, wp, lp);
}

static void LayoutPanel();

static void ShowNotice(HWND h, int icon, const std::wstring &title, const std::wstring &body)
{
	Notice *n = NoticeOf(h);
	n->icon = icon;
	n->title = title;
	n->body = body;
	if (g.autotest && icon)
		g.log += L"[encadré] " + title + L" | " + body + L"\n";
	LayoutPanel();
	InvalidateRect(h, NULL, TRUE);
}

static void HideNotice(HWND h) { ShowNotice(h, 0, L"", L""); }

// ---------------------------------------------------------------- panneau de gauche

static int TextHeight(const std::wstring &text, int width)
{
	HDC dc = GetDC(g.panel);
	HFONT old = (HFONT)SelectObject(dc, g.font);
	RECT r = { 0, 0, width, 0 };
	DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(dc, old);
	ReleaseDC(g.panel, dc);
	return r.bottom;
}

static void Place(HWND h, int x, int y, int w, int hh)
{
	// SWP_NOCOPYBITS : sans composition graphique (XP), copier l'ancienne image laisse des « fantômes ».
	SetWindowPos(h, NULL, x, y - g.panelScroll, w, hh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

static void AddGroup(const std::wstring &title, int x, int y, int w, int h)
{
	RECT r = { x, y, x + w, y + h };
	g.groups.push_back(std::make_pair(r, title));
}

// Cadres gravés avec leur titre, comme les « group box » de Windows 2000.
static void PaintPanel(HDC dc)
{
	RECT cr;
	GetClientRect(g.panel, &cr);
	FillRect(dc, &cr, GetSysColorBrush(COLOR_BTNFACE));
	HFONT old = (HFONT)SelectObject(dc, g.font);
	SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
	SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
	for (size_t i = 0; i < g.groups.size(); i++)
	{
		RECT r = g.groups[i].first;
		OffsetRect(&r, 0, -g.panelScroll);
		RECT frame = r;
		frame.top += S(7);
		DrawEdge(dc, &frame, EDGE_ETCHED, BF_RECT);
		RECT t = { r.left + S(8), r.top, r.right - S(8), r.top + S(14) };
		SetBkMode(dc, OPAQUE);   // le titre efface le trait du cadre derrière lui
		DrawTextW(dc, (L" " + g.groups[i].second + L" ").c_str(), -1, &t, DT_SINGLELINE | DT_NOPREFIX);
	}
	SelectObject(dc, old);
}

static void LayoutPanel()
{
	if (!g.panel || !g.reportNotice)
		return;   // contrôles pas encore créés
	g.groups.clear();
	RECT pr;
	GetClientRect(g.panel, &pr);
	int W = pr.right, m = S(8), gi = S(10), iw = W - 2 * m - 2 * gi, x = m + gi;
	int line = S(16), btn = S(23), y = m;

	// Document
	int top = y;
	y += S(18);
	// Toujours au moins deux lignes : l'ouverture d'un document ne décale pas le reste du panneau.
	int lh = std::max(TextHeight(GetText(g.docLabel), iw), TextHeight(L"A\nA", iw)) + S(2);
	Place(g.docLabel, x, y, iw, lh);
	y += lh + S(4);
	int nh = NoticeHeight(g.docNotice, iw);
	Place(g.docNotice, x, y, iw, nh);
	ShowWindow(g.docNotice, nh ? SW_SHOWNA : SW_HIDE);
	if (nh)
		y += nh + S(6);
	Place(g.btnBrowse, x, y, S(90), btn);
	y += btn + gi;
	AddGroup(TR(L"Document", L"Document"), m, top, W - 2 * m, y - top);
	y += m;

	// Recherche automatique
	top = y;
	y += S(18);
	Place(g.lblTerms, x, y, iw, line);
	y += line;
	Place(g.terms, x, y, iw, S(54));
	y += S(58);
	Place(g.chkSplit, x, y, iw, line + S(2));
	y += line + S(6);
	Place(g.lblData, x, y, iw, line);
	y += line;
	HWND chks[] = { g.chkEmail, g.chkTel, g.chkIban, g.chkNir };
	for (int i = 0; i < 4; i++)
	{
		Place(chks[i], x, y, iw, line + S(2));
		y += line + S(2);
	}
	y += S(4);
	Place(g.btnSearch, x + iw - S(140), y, S(140), btn);
	y += btn + S(6);
	nh = NoticeHeight(g.searchNotice, iw);
	Place(g.searchNotice, x, y, iw, nh);
	ShowWindow(g.searchNotice, nh ? SW_SHOWNA : SW_HIDE);
	if (nh)
		y += nh + S(4);
	y += gi - S(4);
	AddGroup(TR(L"Recherche automatique", L"Automatic search"), m, top, W - 2 * m, y - top);
	y += m;

	// Zones
	top = y;
	y += S(18);
	lh = TextHeight(GetText(g.lblZones), iw);
	Place(g.lblZones, x, y, iw, lh);
	y += lh + S(4);
	Place(g.lblCount, x, y, iw, line);
	y += line + S(4);
	Place(g.btnUndo, x, y, S(80), btn);
	Place(g.btnClear, x + S(86), y, S(80), btn);
	y += btn + gi;
	AddGroup(TR(L"Zones à caviarder", L"Areas to redact"), m, top, W - 2 * m, y - top);
	y += m;

	// Enregistrement
	top = y;
	y += S(18);
	Place(g.chkRaster, x, y, iw, line + S(2));
	y += line + S(4);
	lh = TextHeight(GetText(g.lblRaster), iw);
	Place(g.lblRaster, x, y, iw, lh);
	y += lh + S(6);
	Place(g.btnApply, x, y, iw, S(27));
	y += S(27) + S(6);
	nh = NoticeHeight(g.reportNotice, iw);
	Place(g.reportNotice, x, y, iw, nh);
	ShowWindow(g.reportNotice, nh ? SW_SHOWNA : SW_HIDE);
	if (nh)
		y += nh + S(4);
	y += gi - S(4);
	AddGroup(TR(L"Enregistrement", L"Save"), m, top, W - 2 * m, y - top);
	y += m;

	g.panelHeight = y;
	SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, y, (UINT)pr.bottom, g.panelScroll };
	SetScrollInfo(g.panel, SB_VERT, &si, TRUE);
	// Repeindre tout le panneau et chacun de ses contrôles, immédiatement.
	RedrawWindow(g.panel, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void ScrollPanel(int pos)
{
	RECT pr;
	GetClientRect(g.panel, &pr);
	pos = std::max(0, std::min(pos, g.panelHeight - (int)pr.bottom));
	if (pos == g.panelScroll)
		return;
	g.panelScroll = pos;
	LayoutPanel();
}

static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_COMMAND:
		return SendMessageW(g.main, m, wp, lp);   // les boutons du panneau sont traités par la fenêtre principale
	case WM_ERASEBKGND:
		return 1;   // tout est peint dans WM_PAINT
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		PaintPanel(dc);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_PRINTCLIENT:
		PaintPanel((HDC)wp);
		return 0;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
		// Même fond que le panneau pour les étiquettes et les cases à cocher.
		SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	case WM_SIZE:
		LayoutPanel();
		return 0;
	case WM_VSCROLL:
	{
		SCROLLINFO si = { sizeof(si), SIF_ALL };
		GetScrollInfo(h, SB_VERT, &si);
		int pos = si.nPos;
		switch (LOWORD(wp))
		{
		case SB_LINEUP: pos -= S(20); break;
		case SB_LINEDOWN: pos += S(20); break;
		case SB_PAGEUP: pos -= si.nPage; break;
		case SB_PAGEDOWN: pos += si.nPage; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
		}
		ScrollPanel(pos);
		return 0;
	}
	case WM_MOUSEWHEEL:
		ScrollPanel(g.panelScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(40));
		return 0;
	}
	return DefWindowProcW(h, m, wp, lp);
}

static HWND Control(HWND parent, const wchar_t *cls, DWORD style, int id, DWORD ex = 0)
{
	HWND h = CreateWindowExW(ex, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
		(HMENU)(INT_PTR)id, g.inst, NULL);
	SendMessageW(h, WM_SETFONT, (WPARAM)g.font, FALSE);
	return h;
}

static HWND NoticeControl()
{
	HWND h = CreateWindowExW(0, L"CavNotice", L"", WS_CHILD, 0, 0, 10, 10, g.panel, NULL, g.inst, NULL);
	Notice *n = new Notice();
	n->icon = 0;
	SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)n);
	return h;
}

static void CreatePanel()
{
	g.panel = CreateWindowExW(WS_EX_CONTROLPARENT, L"CavPanel", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
		0, 0, 10, 10, g.main, NULL, g.inst, NULL);
	HWND p = g.panel;
	g.docLabel = Control(p, L"STATIC", SS_LEFT | SS_NOPREFIX, 0);
	g.docNotice = NoticeControl();
	g.btnBrowse = Control(p, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, IDC_BROWSE);
	g.lblTerms = Control(p, L"STATIC", SS_LEFT, 0);
	g.terms = Control(p, L"EDIT", ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP,
		IDC_TERMS, WS_EX_CLIENTEDGE);
	g.chkSplit = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SPLIT);
	g.lblData = Control(p, L"STATIC", SS_LEFT, 0);
	g.chkEmail = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_EMAIL);
	g.chkTel = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_TEL);
	g.chkIban = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_IBAN);
	g.chkNir = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_NIR);
	g.btnSearch = Control(p, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, IDC_SEARCHBTN);
	g.searchNotice = NoticeControl();
	g.lblZones = Control(p, L"STATIC", SS_LEFT, 0);
	g.lblCount = Control(p, L"STATIC", SS_LEFT, 0);
	g.btnUndo = Control(p, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, IDC_UNDOBTN);
	g.btnClear = Control(p, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, IDC_CLEARBTN);
	g.chkRaster = Control(p, L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_RASTER);
	g.lblRaster = Control(p, L"STATIC", SS_LEFT, 0);
	g.btnApply = Control(p, L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_APPLY);
	g.reportNotice = NoticeControl();
}

// Libellés du panneau dans la langue courante.
static void PanelTexts()
{
	SetWindowTextW(g.btnBrowse, TRC(L"&Parcourir...", L"&Browse..."));
	SetWindowTextW(g.lblTerms, TRC(L"&Termes à rechercher (un par ligne) :", L"&Terms to find (one per line):"));
	SetWindowTextW(g.chkSplit, TRC(L"Chercher aussi chaque &mot séparément", L"Also search each &word separately"));
	SetWindowTextW(g.lblData, TRC(L"Données à repérer :", L"Data to detect:"));
	SetWindowTextW(g.chkEmail, TRC(L"Adresses &e-mail", L"&E-mail addresses"));
	SetWindowTextW(g.chkTel, TRC(L"Numéros de télé&phone", L"&Phone numbers"));
	SetWindowTextW(g.chkIban, TRC(L"&IBAN", L"&IBAN"));
	SetWindowTextW(g.chkNir, TRC(L"N° de &sécurité sociale", L"French &social security no."));
	SetWindowTextW(g.btnSearch, TRC(L"&Rechercher et marquer", L"&Find and mark"));
	SetWindowTextW(g.lblZones, TRC(L"Tracez des rectangles sur les pages. Clic sur une zone : la retirer. Clic droit : page entière.",
		L"Draw rectangles on the pages. Click an area to remove it. Right-click: whole page."));
	SetWindowTextW(g.btnUndo, TRC(L"&Annuler", L"&Undo"));
	SetWindowTextW(g.btnClear, TRC(L"Tout &retirer", L"&Remove all"));
	SetWindowTextW(g.chkRaster, TRC(L"Aplatir les pages en &image", L"Flatten pages to &images"));
	SetWindowTextW(g.lblRaster, TRC(L"Sécurité maximale, mais texte non sélectionnable et fichier plus lourd.",
		L"Maximum security, but text is no longer selectable and the file is larger."));
	SetWindowTextW(g.btnApply, TRC(L"&Caviarder et enregistrer le PDF...", L"&Redact and save the PDF..."));
}

// ---------------------------------------------------------------- vue des pages

static const int GAP = 16;

static void ClearCache()
{
	for (size_t i = 0; i < g.cache.size(); i++)
		if (g.cache[i].bmp)
			DeleteObject(g.cache[i].bmp);
	g.cache.assign(g.engine->Pages().size(), Cached());
	for (size_t i = 0; i < g.cache.size(); i++)
		g.cache[i].bmp = NULL;
}

static void UpdateScrollbars()
{
	RECT r;
	GetClientRect(g.view, &r);
	g.scrollX = std::max(0, std::min(g.scrollX, g.contentW - (int)r.right));
	g.scrollY = std::max(0, std::min(g.scrollY, g.contentH - (int)r.bottom));
	SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, g.contentH, (UINT)r.bottom, g.scrollY };
	SetScrollInfo(g.view, SB_VERT, &si, TRUE);
	SCROLLINFO sh = { sizeof(sh), SIF_RANGE | SIF_PAGE | SIF_POS, 0, g.contentW, (UINT)r.right, g.scrollX };
	SetScrollInfo(g.view, SB_HORZ, &sh, TRUE);
}

static void LayoutPages()
{
	const std::vector<PageInfo> &pages = g.engine->Pages();
	g.layout.clear();
	RECT r;
	GetClientRect(g.view, &r);
	if (pages.empty())
	{
		g.contentW = g.contentH = 0;
		UpdateScrollbars();
		return;
	}
	float maxw = 0;
	for (size_t i = 0; i < pages.size(); i++)
		maxw = std::max(maxw, pages[i].w);
	if (g.fit)
		g.zoom = std::max(0.1f, std::min(4.0f, (r.right - 2 * S(GAP) - GetSystemMetrics(SM_CXVSCROLL)) / maxw));
	int y = S(GAP), widest = (int)(maxw * g.zoom + 0.5f);
	g.contentW = widest + 2 * S(GAP);
	int area = std::max<int>(g.contentW, r.right);
	for (size_t i = 0; i < pages.size(); i++)
	{
		int pw = (int)(pages[i].w * g.zoom + 0.5f), ph = (int)(pages[i].h * g.zoom + 0.5f);
		int x = (area - pw) / 2;
		RECT pr = { x, y, x + pw, y + ph };
		g.layout.push_back(pr);
		y += ph + S(GAP);
	}
	g.contentH = y;
	UpdateScrollbars();
	InvalidateRect(g.view, NULL, FALSE);
}

static void SetZoom(float z, bool fit)
{
	if (g.engine->Pages().empty())
		return;
	RECT r;
	GetClientRect(g.view, &r);
	float center = g.contentH ? (g.scrollY + r.bottom / 2.0f) / g.contentH : 0;
	g.fit = fit;
	if (!fit)
		g.zoom = std::max(0.2f, std::min(5.0f, z));
	LayoutPages();
	g.scrollY = (int)(center * g.contentH - r.bottom / 2.0f);
	UpdateScrollbars();
	InvalidateRect(g.view, NULL, FALSE);
}

static RECT ZoneRect(const Zone &z)
{
	const RECT &p = g.layout[z.page];
	RECT r = { p.left + (int)(z.x0 * g.zoom) - g.scrollX, p.top + (int)(z.y0 * g.zoom) - g.scrollY,
		p.left + (int)(z.x1 * g.zoom + 0.5f) - g.scrollX, p.top + (int)(z.y1 * g.zoom + 0.5f) - g.scrollY };
	return r;
}

static int PageAt(POINT pt)
{
	for (size_t i = 0; i < g.layout.size(); i++)
	{
		RECT r = g.layout[i];
		OffsetRect(&r, -g.scrollX, -g.scrollY);
		if (PtInRect(&r, pt))
			return (int)i;
	}
	return -1;
}

static void UpdateCounts()
{
	int n = (int)g.zones.size();
	SetWindowTextW(g.lblCount, (IntStr(n) + TR(L" zone(s) à caviarder", L" area(s) to redact")).c_str());
	Status(L" " + IntStr(n) + TR(L" zone(s)", L" area(s)"), 2);
	bool any = n > 0, doc = !g.engine->Pages().empty();
	EnableWindow(g.btnUndo, any);
	EnableWindow(g.btnClear, any);
	SendMessageW(g.toolbar, TB_ENABLEBUTTON, ID_UNDO, any);
	SendMessageW(g.toolbar, TB_ENABLEBUTTON, ID_CLEAR, any);
	HMENU menu = GetMenu(g.main);
	EnableMenuItem(menu, ID_UNDO, any ? MF_ENABLED : MF_GRAYED);
	EnableMenuItem(menu, ID_CLEAR, any ? MF_ENABLED : MF_GRAYED);
	int ids[] = { ID_SAVE, ID_SEARCH, ID_ZOOM_IN, ID_ZOOM_OUT, ID_FIT };
	for (int i = 0; i < 5; i++)
	{
		SendMessageW(g.toolbar, TB_ENABLEBUTTON, ids[i], doc);
		EnableMenuItem(menu, ids[i], doc ? MF_ENABLED : MF_GRAYED);
	}
	EnableWindow(g.btnSearch, doc);
	EnableWindow(g.btnApply, doc);
	InvalidateRect(g.view, NULL, FALSE);
}

static void AddZones(const std::vector<Zone> &list)
{
	std::set<std::vector<int> > seen;
	for (size_t i = 0; i < g.zones.size(); i++)
	{
		const Zone &z = g.zones[i];
		int k[] = { z.page, (int)z.x0, (int)z.y0, (int)z.x1, (int)z.y1 };
		seen.insert(std::vector<int>(k, k + 5));
	}
	for (size_t i = 0; i < list.size(); i++)
	{
		const Zone &z = list[i];
		int k[] = { z.page, (int)z.x0, (int)z.y0, (int)z.x1, (int)z.y1 };
		if (seen.insert(std::vector<int>(k, k + 5)).second)
			g.zones.push_back(z);
	}
	UpdateCounts();
}

static void PaintView(HDC dc, const RECT &client)
{
	HDC mem = CreateCompatibleDC(dc);
	HBITMAP buf = CreateCompatibleBitmap(dc, client.right, client.bottom), oldBuf = (HBITMAP)SelectObject(mem, buf);
	FillRect(mem, &client, GetSysColorBrush(COLOR_APPWORKSPACE));

	int current = 0;
	HDC src = CreateCompatibleDC(dc);
	for (size_t i = 0; i < g.layout.size(); i++)
	{
		RECT r = g.layout[i];
		OffsetRect(&r, -g.scrollX, -g.scrollY);
		if (r.top <= client.bottom / 2)
			current = (int)i;
		Cached &c = g.cache[i];
		bool visible = r.bottom >= -client.bottom && r.top <= 2 * client.bottom;
		if (!visible)
		{
			// Seules les pages proches de l'écran restent en mémoire.
			if (c.bmp && (r.bottom < -4 * client.bottom || r.top > 5 * client.bottom))
			{
				DeleteObject(c.bmp);
				c.bmp = NULL;
			}
			continue;
		}
		if (r.bottom < 0 || r.top > client.bottom)
			continue;
		if (!c.bmp || c.zoom != g.zoom)
		{
			if (c.bmp)
				DeleteObject(c.bmp);
			c.bmp = g.engine->Render((int)i, g.zoom, &c.w, &c.h);
			c.zoom = g.zoom;
		}
		RECT shadow = r;
		OffsetRect(&shadow, S(4), S(4));
		FillRect(mem, &shadow, GetSysColorBrush(COLOR_3DDKSHADOW));
		FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
		if (c.bmp)
		{
			HBITMAP old = (HBITMAP)SelectObject(src, c.bmp);
			StretchBlt(mem, r.left, r.top, r.right - r.left, r.bottom - r.top, src, 0, 0, c.w, c.h, SRCCOPY);
			SelectObject(src, old);
		}
		FrameRect(mem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
	}

	// Zones : noir translucide bordé de rouge, pour voir ce qui sera supprimé.
	HBITMAP black = CreateCompatibleBitmap(dc, 1, 1), oldB = (HBITMAP)SelectObject(src, black);
	SetPixel(src, 0, 0, RGB(0, 0, 0));
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 150, 0 };
	HBRUSH red = CreateSolidBrush(RGB(192, 0, 0));
	for (size_t i = 0; i < g.zones.size(); i++)
	{
		RECT z = ZoneRect(g.zones[i]);
		if (z.bottom < 0 || z.top > client.bottom || z.right <= z.left || z.bottom <= z.top)
			continue;
		AlphaBlend(mem, z.left, z.top, z.right - z.left, z.bottom - z.top, src, 0, 0, 1, 1, bf);
		FrameRect(mem, &z, red);
	}
	DeleteObject(red);
	SelectObject(src, oldB);
	DeleteObject(black);
	DeleteDC(src);

	if (g.drawing)
	{
		RECT d = { std::min(g.drawStart.x, g.drawEnd.x), std::min(g.drawStart.y, g.drawEnd.y),
			std::max(g.drawStart.x, g.drawEnd.x), std::max(g.drawStart.y, g.drawEnd.y) };
		DrawFocusRect(mem, &d);   // rectangle pointillé de sélection, comme dans l'Explorateur
	}

	BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
	SelectObject(mem, oldBuf);
	DeleteObject(buf);
	DeleteDC(mem);
	if (!g.layout.empty())
		Status(TR(L" Page ", L" Page ") + IntStr(current + 1) + TR(L" sur ", L" of ") + IntStr((int)g.layout.size()), 1);
}

static void ScrollView(int dx, int dy)
{
	g.scrollX += dx;
	g.scrollY += dy;
	UpdateScrollbars();
	InvalidateRect(g.view, NULL, FALSE);
}

static int ScrollPos(HWND h, int bar, WPARAM wp, int line)
{
	SCROLLINFO si = { sizeof(si), SIF_ALL };
	GetScrollInfo(h, bar, &si);
	int pos = si.nPos;
	switch (LOWORD(wp))
	{
	case SB_LINEUP: pos -= line; break;
	case SB_LINEDOWN: pos += line; break;
	case SB_PAGEUP: pos -= si.nPage; break;
	case SB_PAGEDOWN: pos += si.nPage; break;
	case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
	case SB_TOP: pos = 0; break;
	case SB_BOTTOM: pos = si.nMax; break;
	}
	return pos;
}

static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT r;
		GetClientRect(h, &r);
		PaintView(dc, r);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_PRINTCLIENT:
	{
		RECT r;
		GetClientRect(h, &r);
		PaintView((HDC)wp, r);
		return 0;
	}
	case WM_SIZE:
		LayoutPages();
		return 0;
	case WM_VSCROLL:
		g.scrollY = ScrollPos(h, SB_VERT, wp, S(40));
		ScrollView(0, 0);
		return 0;
	case WM_HSCROLL:
		g.scrollX = ScrollPos(h, SB_HORZ, wp, S(40));
		ScrollView(0, 0);
		return 0;
	case WM_MOUSEWHEEL:
	{
		int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
		if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
			SetZoom(g.zoom * (steps > 0 ? 1.15f : 1 / 1.15f), false);
		else
			ScrollView(0, -steps * S(60));
		return 0;
	}
	case WM_KEYDOWN:
	{
		RECT r;
		GetClientRect(h, &r);
		switch (wp)
		{
		case VK_UP: ScrollView(0, -S(40)); break;
		case VK_DOWN: ScrollView(0, S(40)); break;
		case VK_LEFT: ScrollView(-S(40), 0); break;
		case VK_RIGHT: ScrollView(S(40), 0); break;
		case VK_PRIOR: ScrollView(0, -r.bottom); break;
		case VK_NEXT: ScrollView(0, r.bottom); break;
		case VK_HOME: ScrollView(0, -g.contentH); break;
		case VK_END: ScrollView(0, g.contentH); break;
		}
		return 0;
	}
	case WM_LBUTTONDOWN:
	{
		SetFocus(h);
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
		for (size_t i = g.zones.size(); i-- > 0;)
		{
			RECT z = ZoneRect(g.zones[i]);
			if (PtInRect(&z, pt))
			{
				g.zones.erase(g.zones.begin() + i);
				UpdateCounts();
				return 0;
			}
		}
		int p = PageAt(pt);
		if (p < 0)
			return 0;
		g.drawing = true;
		g.drawPage = p;
		g.drawStart = g.drawEnd = pt;
		SetCapture(h);
		return 0;
	}
	case WM_MOUSEMOVE:
		if (g.drawing)
		{
			RECT pr = g.layout[g.drawPage];
			OffsetRect(&pr, -g.scrollX, -g.scrollY);
			g.drawEnd.x = std::max<int>(pr.left, std::min<int>(pr.right, (short)LOWORD(lp)));
			g.drawEnd.y = std::max<int>(pr.top, std::min<int>(pr.bottom, (short)HIWORD(lp)));
			InvalidateRect(h, NULL, FALSE);
		}
		{
			POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
			bool onZone = false;
			for (size_t i = 0; i < g.zones.size() && !onZone; i++)
			{
				RECT z = ZoneRect(g.zones[i]);
				onZone = PtInRect(&z, pt) != 0;
			}
			SetCursor(LoadCursor(NULL, onZone && !g.drawing ? IDC_HAND : PageAt(pt) >= 0 || g.drawing ? IDC_CROSS : IDC_ARROW));
		}
		return 0;
	case WM_LBUTTONUP:
		if (g.drawing)
		{
			ReleaseCapture();
			g.drawing = false;
			RECT pr = g.layout[g.drawPage];
			OffsetRect(&pr, -g.scrollX, -g.scrollY);
			int x0 = std::min(g.drawStart.x, g.drawEnd.x), x1 = std::max(g.drawStart.x, g.drawEnd.x);
			int y0 = std::min(g.drawStart.y, g.drawEnd.y), y1 = std::max(g.drawStart.y, g.drawEnd.y);
			if (x1 - x0 > 2 && y1 - y0 > 2)
			{
				Zone z = { g.drawPage, (x0 - pr.left) / g.zoom, (y0 - pr.top) / g.zoom,
					(x1 - pr.left) / g.zoom, (y1 - pr.top) / g.zoom, TR(L"Zone manuelle", L"Manual area") };
				AddZones(std::vector<Zone>(1, z));
			}
			InvalidateRect(h, NULL, FALSE);
		}
		return 0;
	case WM_SETCURSOR:
		if (LOWORD(lp) == HTCLIENT)
			return TRUE;   // le curseur est choisi dans WM_MOUSEMOVE
		break;
	case WM_CONTEXTMENU:
	{
		POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }, cpt = pt;
		ScreenToClient(h, &cpt);
		int p = PageAt(cpt);
		if (p < 0)
			return 0;
		g.menuPage = p;
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING, ID_PAGE_FULL, (TR(L"Caviarder toute la page ", L"Redact the whole page ") + IntStr(p + 1)).c_str());
		AppendMenuW(menu, MF_STRING, ID_PAGE_CLEAR, (TR(L"Retirer les zones de la page ", L"Remove the areas on page ") + IntStr(p + 1)).c_str());
		TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.main, NULL);
		DestroyMenu(menu);
		return 0;
	}
	}
	return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------- opérations

static void CleanWork()
{
	const wchar_t *files[] = { L"source", L"document.pdf", L"caviarde.pdf", 0 };
	for (int i = 0; files[i]; i++)
	{
		WIN32_FIND_DATAW fd;
		HANDLE f = FindFirstFileW((g.work + files[i] + L"*").c_str(), &fd);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		do
			DeleteFileW((g.work + fd.cFileName).c_str());
		while (FindNextFileW(f, &fd));
		FindClose(f);
	}
}

static void SetBusy(bool on, const std::wstring &text = L"")
{
	SetCursor(LoadCursor(NULL, on ? IDC_WAIT : IDC_ARROW));
	Status(on ? L" " + text : TR(L" Prêt", L" Ready"));
}

// Nom du document, nombre de pages et avertissement « scan », dans la langue courante.
static void DocInfo()
{
	const std::vector<PageInfo> &pages = g.engine->Pages();
	if (pages.empty())
	{
		SetWindowTextW(g.docLabel, TRC(L"Aucun document ouvert.", L"No document open."));
		HideNotice(g.docNotice);
		SetWindowTextW(g.main, APP_NAME);
		return;
	}
	bool pdf = Extension(g.src) == L".pdf";
	std::wstring info = BaseName(g.src) + L"\n" + IntStr((int)pages.size()) + TR(L" page(s)", L" page(s)") +
		(pdf ? L"" : TR(L" (converti en PDF)", L" (converted to PDF)"));
	SetWindowTextW(g.docLabel, info.c_str());
	std::wstring scans;
	int nscan = 0;
	for (size_t i = 0; i < pages.size(); i++)
		if (!pages[i].has_text && ++nscan <= 15)
			scans += (scans.empty() ? L"" : L", ") + IntStr((int)i + 1);
	if (nscan)
		ShowNotice(g.docNotice, IDI_MSG_WARNING, TR(L"Aucun texte détecté", L"No text found"),
			TR(L"Page(s) ", L"Page(s) ") + scans + (nscan > 15 ? L"..." : L"") +
			TR(L" : document scanné ? La recherche automatique n'y fonctionne pas, tracez les zones à la main.",
				L": scanned document? Automatic search cannot work there; draw the areas by hand."));
	else
		HideNotice(g.docNotice);
	SetWindowTextW(g.main, (std::wstring(APP_NAME) + L" - [" + BaseName(g.src) + L"]").c_str());
}

static bool OpenFile(const std::wstring &path)
{
	if (!g.zones.empty() && Msg(TR(L"Abandonner le document en cours et ses zones ?", L"Discard the current document and its areas?"),
			MB_YESNO, IDI_MSG_QUESTION) != IDYES)
		return false;
	std::wstring ext = Extension(path);
	OfficeKind kind = OfficeKindForExtension(ext);
	if (ext != L".pdf" && !IsImage(ext) && kind == OFFICE_NONE)
	{
		Msg(TR(L"Format non pris en charge : ", L"Unsupported format: ") + (ext.empty() ? TR(L"inconnu", L"unknown") : ext),
			MB_OK, IDI_MSG_WARNING);
		return false;
	}

	// Fermer le document précédent et vider le dossier de travail.
	g.engine->Close();
	ClearCache();
	g.zones.clear();
	g.layout.clear();
	CleanWork();

	std::wstring copy = g.work + L"source" + ext, pdf = g.work + L"document.pdf", err;
	SetBusy(true, kind != OFFICE_NONE ? TR(L"Conversion en PDF avec Office... (quelques secondes)",
		L"Converting to PDF with Office... (a few seconds)") : TR(L"Ouverture...", L"Opening..."));
	bool ok = CopyFileW(path.c_str(), copy.c_str(), FALSE) != 0;   // l'original n'est jamais modifié
	if (!ok)
		err = TR(L"Impossible de lire le fichier :\n", L"Cannot read the file:\n") + path;
	else if (ext == L".pdf")
		ok = MoveFileW(copy.c_str(), pdf.c_str()) != 0;
	else if (IsImage(ext))
		ok = g.engine->ImageToPdf(copy, pdf, err);
	else
		ok = OfficeToPdf(kind, copy, pdf, err);
	DeleteFileW(copy.c_str());
	if (ok)
		ok = g.engine->Open(pdf, err);
	SetBusy(false);
	if (!ok)
	{
		Msg(err, MB_OK, IDI_MSG_ERROR);
		DocInfo();
		UpdateCounts();
		InvalidateRect(g.view, NULL, FALSE);
		return false;
	}

	g.src = path;
	g.pdf = pdf;
	ClearCache();
	DocInfo();
	HideNotice(g.searchNotice);
	HideNotice(g.reportNotice);
	g.scrollX = g.scrollY = 0;
	g.fit = true;
	LayoutPages();
	UpdateCounts();
	if (g.autotest)
		g.log += L"[ouvert] " + IntStr((int)g.engine->Pages().size()) + L" page(s)\n";
	return true;
}

// Filtre de boîte de dialogue : « libellé\0motif\0 » répété, terminé par un \0 supplémentaire.
static std::wstring Filter(const std::vector<std::pair<std::wstring, std::wstring> > &items)
{
	std::wstring f;
	for (size_t i = 0; i < items.size(); i++)
	{
		f += items[i].first;
		f.push_back(L'\0');
		f += items[i].second;
		f.push_back(L'\0');
	}
	f.push_back(L'\0');
	return f;
}

static void ChooseFile()
{
	wchar_t file[MAX_PATH * 4] = L"";
	std::vector<std::pair<std::wstring, std::wstring> > items;
	items.push_back(std::make_pair(TR(L"Documents (PDF, Word, Excel, PowerPoint, images)", L"Documents (PDF, Word, Excel, PowerPoint, images)"),
		std::wstring(L"*.pdf;*.doc;*.docx;*.docm;*.dot;*.dotx;*.rtf;*.odt;*.txt;*.xls;*.xlsx;*.xlsm;*.xlsb;*.ods;*.csv;"
			L"*.ppt;*.pptx;*.pptm;*.pps;*.ppsx;*.odp;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp;*.gif")));
	items.push_back(std::make_pair(TR(L"Tous les fichiers (*.*)", L"All files (*.*)"), std::wstring(L"*.*")));
	std::wstring filter = Filter(items);
	std::wstring title = TR(L"Ouvrir un document", L"Open a document");
	OPENFILENAMEW of;
	ZeroMemory(&of, sizeof(of));
	of.lStructSize = sizeof(of);
	of.hwndOwner = g.main;
	of.lpstrFilter = filter.c_str();
	of.lpstrFile = file;
	of.nMaxFile = sizeof(file) / sizeof(file[0]);
	of.lpstrTitle = title.c_str();
	of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
	if (GetOpenFileNameW(&of))
		OpenFile(file);
}

static std::vector<std::wstring> Terms() { return SplitLines(GetText(g.terms)); }

static void Search()
{
	if (g.engine->Pages().empty())
		return;
	std::vector<std::wstring> terms = Terms();
	int pats = 0;
	if (SendMessageW(g.chkEmail, BM_GETCHECK, 0, 0) == BST_CHECKED) pats |= PAT_EMAIL;
	if (SendMessageW(g.chkTel, BM_GETCHECK, 0, 0) == BST_CHECKED) pats |= PAT_TEL;
	if (SendMessageW(g.chkIban, BM_GETCHECK, 0, 0) == BST_CHECKED) pats |= PAT_IBAN;
	if (SendMessageW(g.chkNir, BM_GETCHECK, 0, 0) == BST_CHECKED) pats |= PAT_NIR;
	if (terms.empty() && !pats)
	{
		ShowNotice(g.searchNotice, IDI_MSG_WARNING, TR(L"Rien à rechercher", L"Nothing to search"),
			TR(L"Saisissez au moins un terme ou cochez un type de donnée.", L"Enter at least one term or tick a data type."));
		return;
	}
	std::vector<Zone> hits;
	std::wstring err;
	SetBusy(true, TR(L"Recherche en cours...", L"Searching..."));
	bool ok = g.engine->Search(terms, pats, SendMessageW(g.chkSplit, BM_GETCHECK, 0, 0) == BST_CHECKED, hits, err);
	SetBusy(false);
	if (!ok)
	{
		ShowNotice(g.searchNotice, IDI_MSG_ERROR, TR(L"Recherche impossible", L"Search failed"), err);
		return;
	}
	AddZones(hits);
	std::vector<std::wstring> order;
	std::map<std::wstring, int> by;
	for (size_t i = 0; i < hits.size(); i++)
		if (by[hits[i].label]++ == 0)
			order.push_back(hits[i].label);
	std::wstring body;
	for (size_t i = 0; i < order.size(); i++)
		body += order[i] + TR(L" : ", L": ") + IntStr(by[order[i]]) + L"\n";
	std::wstring missing;
	for (size_t i = 0; i < terms.size(); i++)
		if (!by.count(terms[i]))
			missing += (missing.empty() ? L"" : L", ") + terms[i];
	if (!missing.empty())
		body += TR(L"\nIntrouvable : ", L"\nNot found: ") + missing +
			TR(L". Vérifiez à l'oeil, notamment les mots coupés en fin de ligne ou présents dans des images.",
				L". Check by eye, especially words split at the end of a line or inside images.");
	ShowNotice(g.searchNotice, hits.empty() || !missing.empty() ? IDI_MSG_WARNING : IDI_MSG_INFO,
		IntStr((int)hits.size()) + TR(L" occurrence(s) marquée(s)", L" match(es) marked"), body);
}

static bool SameFile(const std::wstring &a, const std::wstring &b)
{
	wchar_t fa[MAX_PATH * 4], fb[MAX_PATH * 4];
	if (!GetFullPathNameW(a.c_str(), MAX_PATH * 4, fa, NULL) || !GetFullPathNameW(b.c_str(), MAX_PATH * 4, fb, NULL))
		return false;
	return lstrcmpiW(fa, fb) == 0;
}

static void Apply(const std::wstring &forced_dest = L"")
{
	if (g.engine->Pages().empty())
		return;
	if (g.zones.empty())
	{
		ShowNotice(g.reportNotice, IDI_MSG_WARNING, TR(L"Aucune zone", L"No areas"),
			TR(L"Tracez des zones ou lancez une recherche avant d'enregistrer.", L"Draw areas or run a search before saving."));
		return;
	}
	std::wstring out = g.work + L"caviarde.pdf", err;
	Report rep;
	bool raster = SendMessageW(g.chkRaster, BM_GETCHECK, 0, 0) == BST_CHECKED;
	SetBusy(true, TR(L"Caviardage et vérification...", L"Redacting and verifying..."));
	bool ok = g.engine->Apply(g.zones, raster, Terms(), out, rep, err);
	SetBusy(false);
	if (!ok)
	{
		ShowNotice(g.reportNotice, IDI_MSG_ERROR, TR(L"Échec du caviardage", L"Redaction failed"), err);
		Msg(err, MB_OK, IDI_MSG_ERROR);
		return;
	}

	std::wstring body;
	if (rep.ok)
	{
		body = IntStr((int)g.zones.size()) + TR(L" zone(s) supprimée(s) du fichier.\n", L" area(s) removed from the file.\n");
		body += rep.has_text ? TR(L"Aucun des termes recherchés ne subsiste dans le PDF.\n", L"None of the searched terms remain in the PDF.\n")
			: TR(L"Le PDF ne contient plus aucun texte extractible.\n", L"The PDF no longer contains any extractable text.\n");
		body += rep.metadata_empty ? TR(L"Métadonnées effacées.", L"Metadata removed.")
			: TR(L"Attention : métadonnées non vides.", L"Warning: metadata is not empty.");
	}
	else
	{
		for (size_t i = 0; i < rep.terms_left.size(); i++)
			body += rep.terms_left[i].first + TR(L" : ", L": ") + IntStr(rep.terms_left[i].second) + L"\n";
		body += TR(L"Ajoutez des zones à la main ou cochez « Aplatir les pages en image ».",
			L"Add areas by hand or tick \"Flatten pages to images\".");
	}
	if (!rep.snippets_left.empty())
	{
		body += TR(L"\n\nCes textes caviardés apparaissent encore ailleurs (hors zones) :\n",
			L"\n\nThese redacted texts still appear elsewhere (outside the areas):\n");
		for (size_t i = 0; i < rep.snippets_left.size() && i < 10; i++)
			body += L"« " + rep.snippets_left[i].first.substr(0, 60) + L" » : " + IntStr(rep.snippets_left[i].second) + L"\n";
	}
	ShowNotice(g.reportNotice, rep.ok ? IDI_MSG_INFO : IDI_MSG_ERROR,
		rep.ok ? TR(L"Caviardage vérifié.", L"Redaction verified.")
			: TR(L"Attention : des termes recherchés subsistent.", L"Warning: some searched terms remain."), body);
	if (g.autotest)
		g.log += std::wstring(L"[rapport] ok=") + (rep.ok ? L"1" : L"0") + L" texte=" + (rep.has_text ? L"1" : L"0") +
			L" métadonnées_vides=" + (rep.metadata_empty ? L"1" : L"0") + L"\n";

	if (!rep.ok && Msg(TR(L"Des termes recherchés subsistent dans le PDF.\n\nEnregistrer quand même, pour contrôle ?",
				L"Some searched terms remain in the PDF.\n\nSave anyway, for review?"),
			MB_YESNO | MB_DEFBUTTON2, IDI_MSG_WARNING) != IDYES)
		return;

	std::wstring dest = forced_dest;
	if (dest.empty())
	{
		std::wstring base = g.src.substr(0, g.src.size() - Extension(g.src).size()) + TR(L"_caviarde.pdf", L"_redacted.pdf");
		std::vector<std::pair<std::wstring, std::wstring> > items;
		items.push_back(std::make_pair(TR(L"Document PDF (*.pdf)", L"PDF document (*.pdf)"), std::wstring(L"*.pdf")));
		std::wstring filter = Filter(items);
		std::wstring title = TR(L"Enregistrer le PDF caviardé", L"Save the redacted PDF");
		wchar_t file[MAX_PATH * 4];
		lstrcpynW(file, base.c_str(), MAX_PATH * 4);
		OPENFILENAMEW of;
		ZeroMemory(&of, sizeof(of));
		of.lStructSize = sizeof(of);
		of.hwndOwner = g.main;
		of.lpstrFilter = filter.c_str();
		of.lpstrFile = file;
		of.nMaxFile = MAX_PATH * 4;
		of.lpstrTitle = title.c_str();
		of.lpstrDefExt = L"pdf";
		of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
		if (!GetSaveFileNameW(&of))
			return;
		dest = file;
	}
	if (SameFile(dest, g.src))
	{
		Msg(TR(L"Choisissez un autre nom : le document original ne doit pas être écrasé.",
			L"Choose another name: the original document must not be overwritten."), MB_OK, IDI_MSG_WARNING);
		return;
	}
	if (!CopyFileW(out.c_str(), dest.c_str(), FALSE))
	{
		Msg(TR(L"Enregistrement impossible :\n", L"Cannot save:\n") + dest, MB_OK, IDI_MSG_ERROR);
		return;
	}
	Status(TR(L" Enregistré : ", L" Saved: ") + dest);
	if (g.autotest)
	{
		g.log += L"[enregistré] " + dest + L"\n";
		return;
	}
	if (Msg(TR(L"Le PDF caviardé a été enregistré :\n", L"The redacted PDF has been saved:\n") + dest +
			TR(L"\n\nRelisez-le avant toute diffusion.\n\nOuvrir le PDF maintenant ?",
				L"\n\nReview it before sharing it.\n\nOpen the PDF now?"),
			MB_YESNO, IDI_MSG_INFO) == IDYES)
		ShellExecuteW(g.main, L"open", dest.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- fenêtre « À propos »

static const wchar_t *THIRD_PARTY =
	L"MuPDF 1.28.5\r\n"
	L"Copyright © 2006-2026 Artifex Software, Inc.\r\n"
	L"GNU Affero General Public License v3 (AGPL-3.0) - https://mupdf.com\r\n\r\n"
	L"jbig2dec\r\n"
	L"Copyright © 2001-2023 Artifex Software, Inc. - AGPL-3.0\r\n\r\n"
	L"FreeType 2.14.3\r\n"
	L"Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). "
	L"All rights reserved. FreeType License (FTL).\r\n\r\n"
	L"libjpeg 10\r\n"
	L"This software is based in part on the work of the Independent JPEG Group.\r\n\r\n"
	L"OpenJPEG 2.5.4\r\n"
	L"Copyright © 2002-2014 Université catholique de Louvain (UCL), Professor Benoit Macq, "
	L"and other contributors. BSD 2-Clause License.\r\n\r\n"
	L"Little CMS 2\r\n"
	L"Copyright © 1998-2026 Marti Maria Saguer - MIT License.\r\n\r\n"
	L"zlib 1.3.2\r\n"
	L"Copyright © 1995-2026 Jean-loup Gailly and Mark Adler - zlib License.\r\n\r\n"
	L"GCC runtime (libstdc++, libgcc)\r\n"
	L"Copyright © Free Software Foundation, Inc. - GPL-3.0 with the GCC Runtime Library Exception.\r\n\r\n"
	L"MinGW-w64 runtime\r\n"
	L"MinGW-w64 project - Zope Public License 2.1 and public domain.\r\n\r\n"
	L"Windows 2000 / Windows 98 icons\r\n"
	L"Copyright © Microsoft Corporation. All rights reserved.";

static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
			DestroyWindow(h);
		return 0;
	case WM_CLOSE:
		DestroyWindow(h);
		return 0;
	case WM_CTLCOLORSTATIC:
		// Le texte des mentions (zone en lecture seule) reste sur fond blanc.
		if ((HWND)lp == GetDlgItem(h, 100))
			return (LRESULT)GetStockObject(WHITE_BRUSH);
		SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT r;
		GetClientRect(h, &r);
		FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
		HICON icon = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(32), S(32), 0);
		DrawIconEx(dc, S(16), S(16), icon, S(32), S(32), 0, NULL, DI_NORMAL);
		DestroyIcon(icon);
		RECT sep = { S(16), S(122), r.right - S(16), S(124) };   // trait gravé sous l'en-tête
		DrawEdge(dc, &sep, EDGE_ETCHED, BF_TOP);
		EndPaint(h, &ps);
		return 0;
	}
	}
	return DefWindowProcW(h, m, wp, lp);
}

static HWND CreateAbout()
{
	int w = S(470), h = S(430);
	RECT mr;
	GetWindowRect(g.main, &mr);
	RECT wr = { 0, 0, w, h };
	AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
	int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
	std::wstring title = TR(L"À propos du Caviardeur", L"About Caviardeur");
	HWND a = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"CavAbout", title.c_str(),
		WS_POPUP | WS_CAPTION | WS_SYSMENU, mr.left + ((mr.right - mr.left) - ww) / 2, mr.top + ((mr.bottom - mr.top) - wh) / 2,
		ww, wh, g.main, NULL, g.inst, NULL);

	HWND t = Control(a, L"STATIC", SS_LEFT | SS_NOPREFIX, 0);
	SetWindowTextW(t, APP_NAME);
	SendMessageW(t, WM_SETFONT, (WPARAM)g.big, FALSE);
	MoveWindow(t, S(60), S(14), w - S(76), S(22), FALSE);

	std::wstring head = TR(L"Version ", L"Version ") + APP_VERSION + TR(L" pour Windows XP et versions ultérieures",
		L" for Windows XP and later") + L"\n" + APP_COPYRIGHT + TR(L". Tous droits réservés.", L". All rights reserved.") + L"\n\n" +
		TR(L"Caviardage définitif de documents PDF, Word, Excel, PowerPoint et images : le contenu caviardé est "
			L"supprimé du fichier, pas seulement masqué. Aucun fichier n'est envoyé sur Internet.",
			L"Permanent redaction of PDF, Word, Excel, PowerPoint and image documents: redacted content is "
			L"removed from the file, not just hidden. No file is ever sent over the Internet.");
	t = Control(a, L"STATIC", SS_LEFT | SS_NOPREFIX, 0);
	SetWindowTextW(t, head.c_str());
	MoveWindow(t, S(60), S(38), w - S(76), S(80), FALSE);

	t = Control(a, L"STATIC", SS_LEFT | SS_NOPREFIX, 0);
	SetWindowTextW(t, TRC(L"Composants tiers et mentions légales :", L"Third-party components and legal notices:"));
	MoveWindow(t, S(16), S(132), w - S(32), S(16), FALSE);

	HWND e = Control(a, L"EDIT", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 100, WS_EX_CLIENTEDGE);
	SetWindowTextW(e, THIRD_PARTY);
	MoveWindow(e, S(16), S(150), w - S(32), S(190), FALSE);

	t = Control(a, L"STATIC", SS_LEFT | SS_NOPREFIX, 0);
	SetWindowTextW(t, TRC(L"Usage personnel uniquement : cet exécutable intègre des icônes de Microsoft et MuPDF (AGPL). "
		L"Ne pas le diffuser.", L"Personal use only: this executable includes Microsoft icons and MuPDF (AGPL). Do not distribute it."));
	MoveWindow(t, S(16), S(348), w - S(32), S(32), FALSE);

	HWND ok = Control(a, L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
	SetWindowTextW(ok, L"OK");
	MoveWindow(ok, w - S(16) - S(75), h - S(14) - S(23), S(75), S(23), FALSE);
	return a;
}

static void About()
{
	HWND a = CreateAbout();
	EnableWindow(g.main, FALSE);   // fenêtre modale
	ShowWindow(a, SW_SHOW);
	SetFocus(GetDlgItem(a, IDOK));
	MSG msg;
	while (IsWindow(a) && GetMessageW(&msg, NULL, 0, 0) > 0)
		if (!IsDialogMessageW(a, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	EnableWindow(g.main, TRUE);
	SetActiveWindow(g.main);
}

// ---------------------------------------------------------------- fenêtre principale

static void FillToolbar()
{
	while (SendMessageW(g.toolbar, TB_BUTTONCOUNT, 0, 0) > 0)
		SendMessageW(g.toolbar, TB_DELETEBUTTON, 0, 0);
	// Libellés littéraux : la barre d'outils garde l'adresse des chaînes.
	struct { int img, cmd; const wchar_t *text; } items[] = {
		{ 0, ID_OPEN, TRC(L"Ouvrir", L"Open") }, { 1, ID_SAVE, TRC(L"Enregistrer", L"Save") }, { -1, 0, 0 },
		{ I_IMAGENONE, ID_UNDO, TRC(L"Annuler", L"Undo") }, { 2, ID_CLEAR, TRC(L"Tout retirer", L"Remove all") }, { -1, 0, 0 },
		{ 3, ID_SEARCH, TRC(L"Rechercher", L"Find") }, { -1, 0, 0 },
		{ 4, ID_ZOOM_OUT, L"Zoom -" }, { 4, ID_ZOOM_IN, L"Zoom +" }, { 5, ID_FIT, TRC(L"Largeur", L"Fit width") },
	};
	const int n = sizeof(items) / sizeof(items[0]);
	TBBUTTON b[n];
	ZeroMemory(b, sizeof(b));
	for (int i = 0; i < n; i++)
	{
		if (items[i].img == -1)
		{
			b[i].fsStyle = BTNS_SEP;
			continue;
		}
		b[i].iBitmap = items[i].img;
		b[i].idCommand = items[i].cmd;
		b[i].fsState = TBSTATE_ENABLED;
		b[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		b[i].iString = (INT_PTR)items[i].text;
	}
	SendMessageW(g.toolbar, TB_ADDBUTTONSW, n, (LPARAM)b);
	SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
}

static void CreateToolbar()
{
	g.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_TOP | CCS_NODIVIDER,
		0, 0, 0, 0, g.main, NULL, g.inst, NULL);
	SendMessageW(g.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessageW(g.toolbar, WM_SETFONT, (WPARAM)g.font, FALSE);
	int size = S(16);
	g.images = ImageList_Create(size, size, ILC_COLOR24 | ILC_MASK, 6, 0);
	int ids[] = { IDI_OPEN, IDI_SAVE, IDI_DELETE, IDI_SEARCH, IDI_ZOOM, IDI_FIT };
	for (int i = 0; i < 6; i++)
	{
		HICON ic = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(ids[i]), IMAGE_ICON, size, size, 0);
		ImageList_AddIcon(g.images, ic);
		DestroyIcon(ic);
	}
	SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, (LPARAM)g.images);
	FillToolbar();
}

static HMENU CreateMenus()
{
	HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(),
		lang = CreatePopupMenu(), help = CreatePopupMenu();
	AppendMenuW(file, MF_STRING, ID_OPEN, TRC(L"&Ouvrir...\tCtrl+O", L"&Open...\tCtrl+O"));
	AppendMenuW(file, MF_STRING, ID_SAVE, TRC(L"&Caviarder et enregistrer...\tCtrl+S", L"&Redact and save...\tCtrl+S"));
	AppendMenuW(file, MF_SEPARATOR, 0, NULL);
	AppendMenuW(file, MF_STRING, ID_QUIT, TRC(L"&Quitter\tAlt+F4", L"E&xit\tAlt+F4"));
	AppendMenuW(edit, MF_STRING, ID_UNDO, TRC(L"&Annuler la dernière zone\tCtrl+Z", L"&Undo last area\tCtrl+Z"));
	AppendMenuW(edit, MF_STRING, ID_CLEAR, TRC(L"&Retirer toutes les zones", L"&Remove all areas"));
	AppendMenuW(edit, MF_SEPARATOR, 0, NULL);
	AppendMenuW(edit, MF_STRING, ID_SEARCH, TRC(L"Rec&hercher et marquer\tF3", L"&Find and mark\tF3"));
	AppendMenuW(view, MF_STRING, ID_ZOOM_IN, TRC(L"Zoom &avant\tCtrl++", L"Zoom &in\tCtrl++"));
	AppendMenuW(view, MF_STRING, ID_ZOOM_OUT, TRC(L"Zoom a&rrière\tCtrl+-", L"Zoom &out\tCtrl+-"));
	AppendMenuW(view, MF_STRING, ID_FIT, TRC(L"A&juster à la largeur\tCtrl+0", L"Fit &width\tCtrl+0"));
	AppendMenuW(view, MF_SEPARATOR, 0, NULL);
	AppendMenuW(lang, MF_STRING | (g_english ? 0 : MF_CHECKED), ID_LANG_FR, L"&Français");
	AppendMenuW(lang, MF_STRING | (g_english ? MF_CHECKED : 0), ID_LANG_EN, L"&English");
	AppendMenuW(view, MF_POPUP, (UINT_PTR)lang, L"&Langue / Language");
	AppendMenuW(help, MF_STRING, ID_ABOUT, TRC(L"À &propos du Caviardeur...", L"&About Caviardeur..."));
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, TRC(L"&Fichier", L"&File"));
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, TRC(L"&Edition", L"&Edit"));
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, TRC(L"&Affichage", L"&View"));
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&?");
	return bar;
}

static void LayoutMain()
{
	RECT r;
	GetClientRect(g.main, &r);
	SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
	SendMessageW(g.status, WM_SIZE, 0, 0);
	RECT tr, sr;
	GetWindowRect(g.toolbar, &tr);
	GetWindowRect(g.status, &sr);
	int top = tr.bottom - tr.top, bottom = r.bottom - (sr.bottom - sr.top), pw = S(290);
	int parts[] = { r.right - S(420), r.right - S(310), r.right - S(220), -1 };
	SendMessageW(g.status, SB_SETPARTS, 4, (LPARAM)parts);
	MoveWindow(g.panel, 0, top, pw, bottom - top, TRUE);
	MoveWindow(g.view, pw + S(2), top, r.right - pw - S(2), bottom - top, TRUE);
}

// Applique la langue courante à toute l'interface.
static void ApplyLanguage()
{
	HMENU old = GetMenu(g.main);
	SetMenu(g.main, CreateMenus());
	if (old)
		DestroyMenu(old);
	FillToolbar();
	PanelTexts();
	DocInfo();
	HideNotice(g.searchNotice);   // les comptes rendus précédents étaient dans l'autre langue
	HideNotice(g.reportNotice);
	Status(TR(L" Prêt", L" Ready"));
	Status(TR(L" Local : aucun envoi sur Internet", L" Local: nothing sent over the Internet"), 3);
	LayoutMain();
	LayoutPanel();
	UpdateCounts();
	InvalidateRect(g.view, NULL, FALSE);
}

static void SetLanguage(bool english)
{
	if (english == g_english)
		return;
	g_english = english;
	if (!g.autotest)   // l'autotest ne modifie pas le registre
		SaveLanguage();
	ApplyLanguage();
}

static void Command(int id)
{
	switch (id)
	{
	case ID_OPEN: case IDC_BROWSE: ChooseFile(); break;
	case ID_SAVE: case IDC_APPLY: Apply(); break;
	case ID_QUIT: PostMessageW(g.main, WM_CLOSE, 0, 0); break;
	case ID_UNDO: case IDC_UNDOBTN:
		if (!g.zones.empty())
		{
			g.zones.pop_back();
			UpdateCounts();
		}
		break;
	case ID_CLEAR: case IDC_CLEARBTN:
		if (!g.zones.empty() && Msg(TR(L"Retirer toutes les zones ?", L"Remove all areas?"), MB_YESNO, IDI_MSG_QUESTION) == IDYES)
		{
			g.zones.clear();
			UpdateCounts();
		}
		break;
	case ID_SEARCH: case IDC_SEARCHBTN: Search(); break;
	case ID_ZOOM_IN: SetZoom(g.zoom * 1.2f, false); break;
	case ID_ZOOM_OUT: SetZoom(g.zoom / 1.2f, false); break;
	case ID_FIT: SetZoom(g.zoom, true); break;
	case ID_LANG_FR: SetLanguage(false); break;
	case ID_LANG_EN: SetLanguage(true); break;
	case ID_ABOUT: About(); break;
	case ID_PAGE_FULL:
		if (g.menuPage >= 0 && g.menuPage < (int)g.engine->Pages().size())
		{
			const PageInfo &p = g.engine->Pages()[g.menuPage];
			Zone z = { g.menuPage, 0, 0, p.w, p.h, TR(L"Page entière", L"Whole page") };
			AddZones(std::vector<Zone>(1, z));
		}
		break;
	case ID_PAGE_CLEAR:
		for (size_t i = g.zones.size(); i-- > 0;)
			if (g.zones[i].page == g.menuPage)
				g.zones.erase(g.zones.begin() + i);
		UpdateCounts();
		break;
	}
}

static std::wstring MenuTip(int id)
{
	switch (id)
	{
	case ID_OPEN: return TR(L"Ouvrir un document à caviarder", L"Open a document to redact");
	case ID_SAVE: return TR(L"Supprimer définitivement les zones et enregistrer le PDF", L"Permanently remove the areas and save the PDF");
	case ID_UNDO: return TR(L"Retirer la dernière zone ajoutée", L"Remove the last area added");
	case ID_CLEAR: return TR(L"Retirer toutes les zones de toutes les pages", L"Remove all areas on all pages");
	case ID_SEARCH: return TR(L"Marquer les termes et données recherchés", L"Mark the searched terms and data");
	case ID_ZOOM_IN: return TR(L"Agrandir l'affichage", L"Enlarge the view");
	case ID_ZOOM_OUT: return TR(L"Réduire l'affichage", L"Reduce the view");
	case ID_FIT: return TR(L"Afficher les pages sur toute la largeur", L"Show pages across the full width");
	case ID_LANG_FR: return L"Interface en français";
	case ID_LANG_EN: return L"English interface";
	case ID_QUIT: return TR(L"Fermer le Caviardeur", L"Close Caviardeur");
	case ID_ABOUT: return TR(L"Version, copyright et mentions légales", L"Version, copyright and legal notices");
	}
	return TR(L"Prêt", L"Ready");
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_CREATE:
		g.main = h;
		CreateToolbar();
		g.status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, NULL, g.inst, NULL);
		SendMessageW(g.status, WM_SETFONT, (WPARAM)g.font, FALSE);
		CreatePanel();
		PanelTexts();
		g.view = CreateWindowExW(WS_EX_CLIENTEDGE, L"CavPages", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP,
			0, 0, 10, 10, h, NULL, g.inst, NULL);
		DragAcceptFiles(h, TRUE);
		return 0;
	case WM_SIZE:
		LayoutMain();
		return 0;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x = S(640);
		((MINMAXINFO *)lp)->ptMinTrackSize.y = S(480);
		return 0;
	case WM_COMMAND:
		Command(LOWORD(wp));
		return 0;
	case WM_MENUSELECT:
		Status(L" " + MenuTip(LOWORD(wp)));
		return 0;
	case WM_DROPFILES:
	{
		wchar_t file[MAX_PATH * 4];
		if (DragQueryFileW((HDROP)wp, 0, file, MAX_PATH * 4))
			OpenFile(file);
		DragFinish((HDROP)wp);
		return 0;
	}
	case WM_CLOSE:
		DestroyWindow(h);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, m, wp, lp);
}

static bool Accelerator(const MSG &msg)
{
	if (msg.message != WM_KEYDOWN)
		return false;
	bool ctrl = GetKeyState(VK_CONTROL) < 0, inEdit = GetFocus() == g.terms;
	int cmd = 0;
	switch (msg.wParam)
	{
	case 'O': if (ctrl) cmd = ID_OPEN; break;
	case 'S': if (ctrl) cmd = ID_SAVE; break;
	case 'Z': if (ctrl && !inEdit) cmd = ID_UNDO; break;   // dans la zone de saisie, Ctrl+Z annule la frappe
	case VK_F3: cmd = ID_SEARCH; break;
	case VK_ADD: case VK_OEM_PLUS: if (ctrl) cmd = ID_ZOOM_IN; break;
	case VK_SUBTRACT: case VK_OEM_MINUS: if (ctrl) cmd = ID_ZOOM_OUT; break;
	case '0': case VK_NUMPAD0: if (ctrl) cmd = ID_FIT; break;
	}
	if (!cmd)
		return false;
	Command(cmd);
	return true;
}

// ---------------------------------------------------------------- autotest (diagnostic)

static void Pump(DWORD ms)
{
	DWORD until = GetTickCount() + ms;
	MSG msg;
	while (GetTickCount() < until)
	{
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		Sleep(20);
	}
}

static void SaveBitmap(HWND h, const std::wstring &path)
{
	RECT r;
	GetWindowRect(h, &r);
	int w = r.right - r.left, hh = r.bottom - r.top;
	HDC wdc = GetDC(NULL), mem = CreateCompatibleDC(wdc);
	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = hh;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	void *bits = NULL;
	HBITMAP bmp = CreateDIBSection(wdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0), old = (HBITMAP)SelectObject(mem, bmp);
	PrintWindow(h, mem, 0);
	SelectObject(mem, old);
	int stride = (w * 3 + 3) & ~3;
	BITMAPFILEHEADER fh = { 0x4D42, (DWORD)(sizeof(fh) + sizeof(BITMAPINFOHEADER) + stride * hh), 0, 0,
		(DWORD)(sizeof(fh) + sizeof(BITMAPINFOHEADER)) };
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	DWORD n;
	WriteFile(f, &fh, sizeof(fh), &n, NULL);
	WriteFile(f, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER), &n, NULL);
	WriteFile(f, bits, stride * hh, &n, NULL);
	CloseHandle(f);
	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(NULL, wdc);
}

static void WriteLog(const std::wstring &path)
{
	std::string s = "\xEF\xBB\xBF" + Utf8(g.log);
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	DWORD n;
	WriteFile(f, s.c_str(), (DWORD)s.size(), &n, NULL);
	CloseHandle(f);
}

static bool HasArg(int argc, wchar_t **argv, const wchar_t *a)
{
	for (int i = 1; i < argc; i++)
		if (lstrcmpiW(argv[i], a) == 0)
			return true;
	return false;
}

// Caviardeur.exe /autotest <document> <pdf de sortie> <rapport.txt> "<terme1|terme2>" [capture.bmp] [/aplatir] [/en|/fr]
static void AutoTest(int argc, wchar_t **argv)
{
	std::wstring in = argv[2], out = argv[3], report = argv[4], raw = argc > 5 ? argv[5] : L"", terms;
	for (size_t i = 0; i < raw.size(); i++)
		terms += raw[i] == L'|' ? std::wstring(L"\r\n") : std::wstring(1, raw[i]);
	SetWindowTextW(g.terms, terms.c_str());
	SendMessageW(g.chkEmail, BM_SETCHECK, BST_CHECKED, 0);
	SendMessageW(g.chkTel, BM_SETCHECK, BST_CHECKED, 0);
	SendMessageW(g.chkIban, BM_SETCHECK, BST_CHECKED, 0);
	if (HasArg(argc, argv, L"/aplatir"))
		SendMessageW(g.chkRaster, BM_SETCHECK, BST_CHECKED, 0);
	if (OpenFile(in))
	{
		Search();
		g.log += L"[zones] " + IntStr((int)g.zones.size()) + L"\n";
		if (HasArg(argc, argv, L"/bascule"))   // changement de langue à chaud, document ouvert
		{
			SetLanguage(!g_english);
			g.log += std::wstring(L"[langue] ") + (g_english ? L"en" : L"fr") + L"\n";
		}
		if (argc > 6 && argv[6][0] != L'/')
		{
			RedrawWindow(g.main, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
			Pump(500);
			SaveBitmap(g.main, argv[6]);
		}
		Apply(out);
	}
	WriteLog(report);
}

// Caviardeur.exe /apropos <capture.bmp> [/en|/fr] : capture de la fenêtre « À propos ».
static void AboutTest(const std::wstring &bmp)
{
	HWND a = CreateAbout();
	SetWindowPos(a, NULL, -S(3000), -S(3000), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	ShowWindow(a, SW_SHOWNOACTIVATE);
	RedrawWindow(a, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	Pump(300);
	SaveBitmap(a, bmp);
	DestroyWindow(a);
}

// ---------------------------------------------------------------- point d'entrée

static bool RegisterClasses()
{
	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.hInstance = g.inst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	wc.lpfnWndProc = MainProc;
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"CavMain";
	wc.hIcon = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
	wc.hIconSm = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
	if (!RegisterClassExW(&wc))
		return false;
	wc.hIcon = wc.hIconSm = NULL;

	wc.lpfnWndProc = PanelProc;
	wc.lpszClassName = L"CavPanel";
	RegisterClassExW(&wc);

	wc.lpfnWndProc = NoticeProc;
	wc.lpszClassName = L"CavNotice";
	RegisterClassExW(&wc);

	wc.lpfnWndProc = AboutProc;
	wc.lpszClassName = L"CavAbout";
	RegisterClassExW(&wc);

	wc.lpfnWndProc = ViewProc;
	wc.hbrBackground = NULL;
	wc.hCursor = NULL;
	wc.style = CS_DBLCLKS;
	wc.lpszClassName = L"CavPages";
	return RegisterClassExW(&wc) != 0;
}

static std::wstring MakeWorkDir()
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	std::wstring base = std::wstring(tmp) + L"Caviardeur\\";
	CreateDirectoryW(base.c_str(), NULL);
	std::wstring dir = base + IntStr((int)GetCurrentProcessId()) + L"\\";
	CreateDirectoryW(dir.c_str(), NULL);
	return dir;
}

static HFONT Font(int pt, int weight)
{
	return CreateFontW(-MulDiv(pt, g.dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show)
{
	g.inst = inst;
	g.panelScroll = 0;
	g.zoom = 1;
	g.fit = true;
	g.menuPage = -1;
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	g.autotest = argc >= 5 && lstrcmpiW(argv[1], L"/autotest") == 0;
	bool aboutTest = argc >= 3 && lstrcmpiW(argv[1], L"/apropos") == 0;

	LoadLanguage();
	if (HasArg(argc, argv, L"/en"))   // forcer une langue (tests), sans la mémoriser
		g_english = true;
	else if (HasArg(argc, argv, L"/fr"))
		g_english = false;

	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);
	CoInitialize(NULL);

	HDC dc = GetDC(NULL);
	g.dpi = GetDeviceCaps(dc, LOGPIXELSY);
	ReleaseDC(NULL, dc);
	g.font = Font(8, FW_NORMAL);
	g.bold = Font(8, FW_BOLD);
	g.big = Font(12, FW_BOLD);
	int msgIcons[] = { IDI_MSG_INFO, IDI_MSG_WARNING, IDI_MSG_ERROR, IDI_MSG_QUESTION };
	for (int i = 0; i < 4; i++)
		g.icons[i] = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(msgIcons[i]), IMAGE_ICON, S(32), S(32), 0);

	g.engine = new Engine();
	if (!g.engine->Ok())
	{
		MessageBoxW(NULL, TRC(L"Impossible d'initialiser le moteur PDF (mémoire insuffisante ?).",
			L"Cannot start the PDF engine (not enough memory?)."), APP_NAME, MB_ICONERROR);
		return 1;
	}
	g.work = MakeWorkDir();

	if (!RegisterClasses())
		return 1;
	HWND h = CreateWindowExW(0, L"CavMain", APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, S(1000), S(720), NULL, CreateMenus(), inst, NULL);
	if (!h)
		return 1;
	if (g.autotest || aboutTest)
	{
		SetWindowPos(h, NULL, -S(4000), -S(4000), S(1000), S(720), SWP_NOZORDER | SWP_NOACTIVATE);   // hors écran
		ShowWindow(h, SW_SHOWNOACTIVATE);
	}
	else
		ShowWindow(h, show);
	UpdateWindow(h);
	DocInfo();
	LayoutPanel();
	UpdateCounts();
	Status(TR(L" Prêt", L" Ready"));
	Status(TR(L" Local : aucun envoi sur Internet", L" Local: nothing sent over the Internet"), 3);

	if (g.autotest)
		AutoTest(argc, argv);
	else if (aboutTest)
		AboutTest(argv[2]);
	else
	{
		if (argc > 1)
			OpenFile(argv[1]);   // « Ouvrir avec » ou fichier glissé sur l'exe
		MSG msg;
		while (GetMessageW(&msg, NULL, 0, 0) > 0)
		{
			if (Accelerator(msg))
				continue;
			if (IsDialogMessageW(g.panel, &msg))
				continue;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	g.engine->Close();
	ClearCache();
	CleanWork();
	RemoveDirectoryW(g.work.c_str());
	delete g.engine;
	CoUninitialize();
	LocalFree(argv);
	return 0;
}
