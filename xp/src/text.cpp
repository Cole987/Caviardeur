#include "text.h"

#include <windows.h>

bool g_english = false;

std::string Utf8(const std::wstring &w)
{
	if (w.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

std::wstring Wide(const std::string &s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

// Lettres latines accentuées (U+00C0 à U+017F) ramenées à leur lettre de base, en minuscule.
static const char *LATIN1 =
	"aaaaaaaceeeeiiii"   // C0-CF  (Æ traité à part)
	"dnooooo*ouuuuyts"   // D0-DF  (× = *, Þ = t, ß traité à part)
	"aaaaaaaceeeeiiii"   // E0-EF
	"dnooooo/ouuuuyty";  // F0-FF  (÷ = /, þ = t)
static const char *LATIN_EXT_A =
	"aaaaaaccccccccdd"   // 100-10F
	"ddeeeeeeeeeegggg"   // 110-11F
	"gggghhhhiiiiiiii"   // 120-12F
	"iiiijjkkklllllll"   // 130-13F
	"lllnnnnnnnnnoooo"   // 140-14F (Œ œ traités à part)
	"oooorrrrrrssssss"   // 150-15F
	"ssttttttuuuuuuuu"   // 160-16F
	"uuuuwwyyyzzzzzzs";  // 170-17F

std::wstring Fold(unsigned int c)
{
	if (c < 0x80)
		return std::wstring(1, (wchar_t)(c >= 'A' && c <= 'Z' ? c + 32 : c));
	switch (c)
	{
	case 0x00A0: case 0x202F: case 0x2009: case 0x2007: case 0x2002: case 0x2003:
		return L" ";
	case 0x00C6: case 0x00E6: return L"ae";
	case 0x00DF: return L"ss";
	case 0x0152: case 0x0153: return L"oe";
	case 0xFB00: return L"ff";
	case 0xFB01: return L"fi";
	case 0xFB02: return L"fl";
	case 0xFB03: return L"ffi";
	case 0xFB04: return L"ffl";
	case 0x2018: case 0x2019: case 0x02BC: return L"'";
	case 0x201C: case 0x201D: case 0x00AB: case 0x00BB: return L"\"";
	case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return L"-";
	}
	if (c >= 0xC0 && c <= 0xFF)
		return std::wstring(1, (wchar_t)LATIN1[c - 0xC0]);
	if (c >= 0x100 && c <= 0x17F)
		return std::wstring(1, (wchar_t)LATIN_EXT_A[c - 0x100]);
	if (c > 0xFFFF)
		return L"?";
	return std::wstring(1, (wchar_t)c);
}

std::wstring FoldString(const std::wstring &s)
{
	std::wstring out;
	for (size_t i = 0; i < s.size(); i++)
		out += Fold((unsigned short)s[i]);
	return out;
}

static bool IsSpace(wchar_t c)
{
	return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

std::wstring CollapseSpaces(const std::wstring &s)
{
	std::wstring out;
	bool space = false;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (IsSpace(s[i]))
			space = !out.empty();
		else
		{
			if (space)
				out += L' ';
			space = false;
			out += s[i];
		}
	}
	return out;
}

std::wstring Trim(const std::wstring &s)
{
	size_t a = 0, b = s.size();
	while (a < b && IsSpace(s[a]))
		a++;
	while (b > a && IsSpace(s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

std::vector<std::wstring> SplitLines(const std::wstring &s)
{
	std::vector<std::wstring> lines;
	std::wstring cur;
	for (size_t i = 0; i <= s.size(); i++)
	{
		if (i == s.size() || s[i] == L'\n')
		{
			std::wstring t = Trim(cur);
			if (!t.empty())
				lines.push_back(t);
			cur.clear();
		}
		else
			cur += s[i];
	}
	return lines;
}
