// Conversion de documents Office en PDF par automatisation COM (Office 2007 SP2 ou plus récent).
#pragma once
#include <string>

enum OfficeKind
{
	OFFICE_NONE,
	OFFICE_WORD,
	OFFICE_EXCEL,
	OFFICE_POWERPOINT
};

OfficeKind OfficeKindForExtension(const std::wstring &ext);

// src et pdf doivent être des chemins absolus. COM doit être initialisé (CoInitialize).
bool OfficeToPdf(OfficeKind kind, const std::wstring &src, const std::wstring &pdf, std::wstring &err);
