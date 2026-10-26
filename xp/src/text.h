// Utilitaires de texte : conversions UTF-8 / UTF-16 et normalisation pour la recherche.
#pragma once
#include <string>
#include <vector>

std::string Utf8(const std::wstring &w);
std::wstring Wide(const std::string &s);

// Minuscule sans accent, ligatures éclatées (« ﬁ » -> « fi », « œ » -> « oe »).
// Peut renvoyer plusieurs caractères pour un seul caractère source.
std::wstring Fold(unsigned int codepoint);
std::wstring FoldString(const std::wstring &s);

// Espaces multiples et retours à la ligne ramenés à une seule espace.
std::wstring CollapseSpaces(const std::wstring &s);

std::vector<std::wstring> SplitLines(const std::wstring &s);
std::wstring Trim(const std::wstring &s);

// Langue de l'interface : français par défaut, anglais si g_english.
// TRC renvoie un littéral (adresse stable, utilisable par la barre d'outils), TR une chaîne.
extern bool g_english;
inline const wchar_t *TRC(const wchar_t *fr, const wchar_t *en) { return g_english ? en : fr; }
inline std::wstring TR(const wchar_t *fr, const wchar_t *en) { return std::wstring(TRC(fr, en)); }
