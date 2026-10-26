// Moteur de caviardage : MuPDF (rendu, recherche, suppression réelle, nettoyage, vérification).
#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct PageInfo
{
	float w, h;      // taille affichée, en points (rotation comprise)
	bool has_text;   // faux pour une page scannée sans texte
};

struct Zone
{
	int page;
	float x0, y0, x1, y1;   // coordonnées de page, en points, origine en haut à gauche
	std::wstring label;
};

struct Report
{
	bool ok;
	bool has_text;
	bool metadata_empty;
	std::vector<std::pair<std::wstring, int> > terms_left;
	std::vector<std::pair<std::wstring, int> > snippets_left;
};

enum
{
	PAT_EMAIL = 1,
	PAT_TEL = 2,
	PAT_IBAN = 4,
	PAT_NIR = 8
};

class Engine
{
public:
	Engine();
	~Engine();

	bool Ok() const { return ctx != 0; }

	// Ouvre le PDF de travail pour l'affichage et la recherche.
	bool Open(const std::wstring &pdf, std::wstring &err);
	void Close();
	const std::vector<PageInfo> &Pages() const { return pages; }

	// Rendu d'une page en image 24 bits (à libérer avec DeleteObject).
	HBITMAP Render(int page, float zoom, int *w, int *h);

	// Recherche de termes et de types de données ; renvoie une zone par ligne trouvée.
	bool Search(const std::vector<std::wstring> &terms, int patterns, bool split_words,
		std::vector<Zone> &hits, std::wstring &err);

	// Caviarde, nettoie, enregistre dans out (jamais en incrémental) puis vérifie le résultat.
	bool Apply(const std::vector<Zone> &zones, bool rasterize, const std::vector<std::wstring> &check_terms,
		const std::wstring &out, Report &report, std::wstring &err);

	// Image (JPG, PNG, TIFF, BMP…) -> PDF d'une page.
	bool ImageToPdf(const std::wstring &image, const std::wstring &pdf, std::wstring &err);

private:
	struct fz_context *ctx;
	struct fz_document *doc;
	std::wstring path;
	std::vector<PageInfo> pages;
	bool Verify(const std::wstring &out, const std::vector<std::wstring> &terms,
		const std::vector<std::wstring> &snippets, Report &report, std::wstring &err);
};
