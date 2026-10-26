#include "office.h"
#include "text.h"

#include <windows.h>
#include <ole2.h>

#include <vector>

OfficeKind OfficeKindForExtension(const std::wstring &e)
{
	static const wchar_t *word[] = { L".doc", L".docx", L".docm", L".dot", L".dotx", L".rtf", L".odt", L".txt", L".wpd", 0 };
	static const wchar_t *excel[] = { L".xls", L".xlsx", L".xlsm", L".xlsb", L".ods", L".csv", 0 };
	static const wchar_t *ppt[] = { L".ppt", L".pptx", L".pptm", L".pps", L".ppsx", L".odp", 0 };
	for (int i = 0; word[i]; i++)
		if (e == word[i])
			return OFFICE_WORD;
	for (int i = 0; excel[i]; i++)
		if (e == excel[i])
			return OFFICE_EXCEL;
	for (int i = 0; ppt[i]; i++)
		if (e == ppt[i])
			return OFFICE_POWERPOINT;
	return OFFICE_NONE;
}

// ---------------------------------------------------------------- petits outils VARIANT / IDispatch

namespace {

struct Arg
{
	const wchar_t *name;
	VARIANT value;
};

Arg Str(const wchar_t *name, const std::wstring &s)
{
	Arg a;
	a.name = name;
	VariantInit(&a.value);
	a.value.vt = VT_BSTR;
	a.value.bstrVal = SysAllocString(s.c_str());
	return a;
}

Arg Int(const wchar_t *name, long v)
{
	Arg a;
	a.name = name;
	VariantInit(&a.value);
	a.value.vt = VT_I4;
	a.value.lVal = v;
	return a;
}

Arg Bool(const wchar_t *name, bool v)
{
	Arg a;
	a.name = name;
	VariantInit(&a.value);
	a.value.vt = VT_BOOL;
	a.value.boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
	return a;
}

struct Failure
{
	std::wstring message;
};

// Appelle obj.name(args nommés) ; lève Failure avec un message lisible en cas d'erreur.
VARIANT Call(IDispatch *obj, const wchar_t *name, WORD flags, std::vector<Arg> args = std::vector<Arg>())
{
	// Pour une affectation de propriété, seul le nom de la propriété est recherché.
	std::vector<LPOLESTR> names;
	names.push_back((LPOLESTR)name);
	if (flags != DISPATCH_PROPERTYPUT)
		for (size_t i = 0; i < args.size(); i++)
			names.push_back((LPOLESTR)args[i].name);
	std::vector<DISPID> ids(names.size());
	HRESULT hr = obj->GetIDsOfNames(IID_NULL, &names[0], (UINT)names.size(), LOCALE_USER_DEFAULT, &ids[0]);

	VARIANT result;
	VariantInit(&result);
	EXCEPINFO ex;
	ZeroMemory(&ex, sizeof(ex));
	if (SUCCEEDED(hr))
	{
		// Les arguments nommés sont passés dans l'ordre inverse, comme le veut IDispatch.
		std::vector<VARIANT> vals;
		std::vector<DISPID> named;
		for (size_t i = args.size(); i-- > 0;)
		{
			vals.push_back(args[i].value);
			if (flags != DISPATCH_PROPERTYPUT)
				named.push_back(ids[i + 1]);
		}
		DISPID put = DISPID_PROPERTYPUT;
		DISPPARAMS dp;
		dp.cArgs = (UINT)vals.size();
		dp.rgvarg = vals.empty() ? NULL : &vals[0];
		dp.cNamedArgs = (UINT)named.size();
		dp.rgdispidNamedArgs = named.empty() ? NULL : &named[0];
		if (flags == DISPATCH_PROPERTYPUT)
		{
			dp.cNamedArgs = 1;
			dp.rgdispidNamedArgs = &put;
		}
		hr = obj->Invoke(ids[0], IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, &result, &ex, NULL);
	}
	for (size_t i = 0; i < args.size(); i++)
		VariantClear(&args[i].value);
	if (FAILED(hr))
	{
		Failure f;
		f.message = TR(L"« ", L"\"") + name + TR(L" » a échoué", L"\" failed");
		if (hr == DISP_E_UNKNOWNNAME || hr == DISP_E_MEMBERNOTFOUND)
			f.message += TR(L" (fonction absente de cette version d'Office)", L" (not available in this version of Office)");
		if (ex.bstrDescription)
		{
			f.message += L" : ";
			f.message += ex.bstrDescription;
		}
		SysFreeString(ex.bstrDescription);
		SysFreeString(ex.bstrSource);
		SysFreeString(ex.bstrHelpFile);
		throw f;
	}
	return result;
}

IDispatch *Object(VARIANT v)
{
	if (v.vt != VT_DISPATCH || !v.pdispVal)
	{
		VariantClear(&v);
		Failure f;
		f.message = TR(L"Réponse inattendue d'Office.", L"Unexpected response from Office.");
		throw f;
	}
	return v.pdispVal;   // la référence du VARIANT est transférée à l'appelant
}

void Release(IDispatch *&p)
{
	if (p)
		p->Release();
	p = NULL;
}

IDispatch *Start(const wchar_t *progid, const wchar_t *product)
{
	CLSID clsid;
	IDispatch *app = NULL;
	if (FAILED(CLSIDFromProgID(progid, &clsid)) ||
		FAILED(CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void **)&app)))
	{
		Failure f;
		f.message = std::wstring(product) + TR(L" n'est pas installé sur ce poste.", L" is not installed on this computer.");
		throw f;
	}
	return app;
}

std::wstring NoPdf()
{
	return TR(L"\n\nOffice 2007 ne sait exporter en PDF qu'avec le Service Pack 2, ou avec le complément gratuit "
		L"de Microsoft « Enregistrer au format PDF ou XPS ».",
		L"\n\nOffice 2007 can only export to PDF with Service Pack 2, or with Microsoft's free "
		L"\"Save as PDF or XPS\" add-in.");
}

void Word(const std::wstring &src, const std::wstring &pdf)
{
	IDispatch *app = Start(L"Word.Application", L"Microsoft Word"), *docs = NULL, *doc = NULL;
	try
	{
		std::vector<Arg> a;
		a.push_back(Bool(L"Visible", false));
		Call(app, L"Visible", DISPATCH_PROPERTYPUT, a);
		a.clear();
		a.push_back(Int(L"DisplayAlerts", 0));
		Call(app, L"DisplayAlerts", DISPATCH_PROPERTYPUT, a);
		docs = Object(Call(app, L"Documents", DISPATCH_PROPERTYGET));
		a.clear();
		a.push_back(Str(L"FileName", src));
		a.push_back(Bool(L"ConfirmConversions", false));
		a.push_back(Bool(L"ReadOnly", true));
		a.push_back(Bool(L"AddToRecentFiles", false));
		a.push_back(Str(L"PasswordDocument", L"x"));   // un document protégé échoue au lieu d'ouvrir une fenêtre
		a.push_back(Bool(L"Visible", false));
		doc = Object(Call(docs, L"Open", DISPATCH_METHOD, a));
		a.clear();
		a.push_back(Str(L"OutputFileName", pdf));
		a.push_back(Int(L"ExportFormat", 17));        // wdExportFormatPDF
		a.push_back(Bool(L"OpenAfterExport", false));
		a.push_back(Int(L"OptimizeFor", 0));
		a.push_back(Int(L"Item", 0));                 // contenu seul, sans révisions ni commentaires
		a.push_back(Bool(L"IncludeDocProps", false));
		a.push_back(Int(L"CreateBookmarks", 0));
		a.push_back(Bool(L"DocStructureTags", false));
		try
		{
			Call(doc, L"ExportAsFixedFormat", DISPATCH_METHOD, a);
		}
		catch (Failure &f)
		{
			f.message += NoPdf();
			throw;
		}
		a.clear();
		a.push_back(Int(L"SaveChanges", 0));
		Call(doc, L"Close", DISPATCH_METHOD, a);
		Release(doc);
	}
	catch (...)
	{
		if (doc)
		{
			std::vector<Arg> a;
			a.push_back(Int(L"SaveChanges", 0));
			try { Call(doc, L"Close", DISPATCH_METHOD, a); } catch (...) {}
		}
		Release(doc);
		Release(docs);
		try { std::vector<Arg> a; a.push_back(Int(L"SaveChanges", 0)); Call(app, L"Quit", DISPATCH_METHOD, a); } catch (...) {}
		Release(app);
		throw;
	}
	Release(docs);
	try { std::vector<Arg> a; a.push_back(Int(L"SaveChanges", 0)); Call(app, L"Quit", DISPATCH_METHOD, a); } catch (...) {}
	Release(app);
}

void Excel(const std::wstring &src, const std::wstring &pdf)
{
	IDispatch *app = Start(L"Excel.Application", L"Microsoft Excel"), *books = NULL, *book = NULL;
	try
	{
		std::vector<Arg> a;
		a.push_back(Bool(L"Visible", false));
		Call(app, L"Visible", DISPATCH_PROPERTYPUT, a);
		a.clear();
		a.push_back(Bool(L"DisplayAlerts", false));
		Call(app, L"DisplayAlerts", DISPATCH_PROPERTYPUT, a);
		books = Object(Call(app, L"Workbooks", DISPATCH_PROPERTYGET));
		a.clear();
		a.push_back(Str(L"Filename", src));
		a.push_back(Int(L"UpdateLinks", 0));
		a.push_back(Bool(L"ReadOnly", true));
		a.push_back(Str(L"Password", L"x"));
		a.push_back(Bool(L"AddToMru", false));
		book = Object(Call(books, L"Open", DISPATCH_METHOD, a));
		a.clear();
		a.push_back(Int(L"Type", 0));                 // xlTypePDF
		a.push_back(Str(L"Filename", pdf));
		a.push_back(Int(L"Quality", 0));
		a.push_back(Bool(L"IncludeDocProperties", false));
		a.push_back(Bool(L"IgnorePrintAreas", false));
		a.push_back(Bool(L"OpenAfterPublish", false));
		try
		{
			Call(book, L"ExportAsFixedFormat", DISPATCH_METHOD, a);
		}
		catch (Failure &f)
		{
			f.message += NoPdf();
			throw;
		}
		a.clear();
		a.push_back(Bool(L"SaveChanges", false));
		Call(book, L"Close", DISPATCH_METHOD, a);
		Release(book);
	}
	catch (...)
	{
		if (book)
		{
			std::vector<Arg> a;
			a.push_back(Bool(L"SaveChanges", false));
			try { Call(book, L"Close", DISPATCH_METHOD, a); } catch (...) {}
		}
		Release(book);
		Release(books);
		try { Call(app, L"Quit", DISPATCH_METHOD); } catch (...) {}
		Release(app);
		throw;
	}
	Release(books);
	try { Call(app, L"Quit", DISPATCH_METHOD); } catch (...) {}
	Release(app);
}

void PowerPoint(const std::wstring &src, const std::wstring &pdf)
{
	// PowerPoint n'accepte qu'une instance : on ne la ferme que si rien d'autre n'y est ouvert.
	IDispatch *app = Start(L"PowerPoint.Application", L"Microsoft PowerPoint"), *pres = NULL, *list = NULL;
	try
	{
		list = Object(Call(app, L"Presentations", DISPATCH_PROPERTYGET));
		std::vector<Arg> a;
		a.push_back(Str(L"FileName", src));
		a.push_back(Int(L"ReadOnly", -1));            // msoTrue
		a.push_back(Int(L"Untitled", 0));
		a.push_back(Int(L"WithWindow", 0));
		pres = Object(Call(list, L"Open", DISPATCH_METHOD, a));
		a.clear();
		a.push_back(Str(L"FileName", pdf));
		a.push_back(Int(L"FileFormat", 32));          // ppSaveAsPDF
		try
		{
			Call(pres, L"SaveAs", DISPATCH_METHOD, a);
		}
		catch (Failure &f)
		{
			f.message += NoPdf();
			throw;
		}
	}
	catch (...)
	{
		if (pres)
			try { Call(pres, L"Close", DISPATCH_METHOD); } catch (...) {}
		Release(pres);
		Release(list);
		Release(app);
		throw;
	}
	try { Call(pres, L"Close", DISPATCH_METHOD); } catch (...) {}
	Release(pres);
	try
	{
		VARIANT c = Call(list, L"Count", DISPATCH_PROPERTYGET);
		VARIANT n;
		VariantInit(&n);
		if (SUCCEEDED(VariantChangeType(&n, &c, 0, VT_I4)) && n.lVal == 0)
			Call(app, L"Quit", DISPATCH_METHOD);
		VariantClear(&c);
	}
	catch (...) {}
	Release(list);
	Release(app);
}

} // namespace

bool OfficeToPdf(OfficeKind kind, const std::wstring &src, const std::wstring &pdf, std::wstring &err)
{
	try
	{
		if (kind == OFFICE_WORD)
			Word(src, pdf);
		else if (kind == OFFICE_EXCEL)
			Excel(src, pdf);
		else if (kind == OFFICE_POWERPOINT)
			PowerPoint(src, pdf);
		else
		{
			err = TR(L"Format non pris en charge.", L"Unsupported format.");
			return false;
		}
	}
	catch (const Failure &f)
	{
		err = TR(L"Conversion en PDF impossible.\n\n", L"Could not convert to PDF.\n\n") + f.message;
		return false;
	}
	if (GetFileAttributesW(pdf.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		err = TR(L"Office n'a produit aucun PDF.", L"Office did not produce a PDF.");
		return false;
	}
	return true;
}
