// Moteur de caviardage fondé sur MuPDF.
//
// Règle MuPDF : un fz_try saute (longjmp) en cas d'erreur. Aucun objet C++ avec destructeur
// n'est donc créé à l'intérieur d'un bloc fz_try : on y appelle MuPDF, et le traitement C++
// (chaînes, vecteurs, expressions régulières) se fait en dehors.

#include "engine.h"
#include "text.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include <map>
#include <regex>

// ---------------------------------------------------------------- index de texte d'une page

struct CharBox
{
	fz_rect rect;   // vide pour les séparateurs ajoutés (fin de ligne)
	int line;
};

struct PageText
{
	std::wstring text;              // texte normalisé (minuscules, sans accents)
	std::vector<CharBox> boxes;     // une boîte par caractère de text
};

static void IndexBlocks(fz_stext_block *block, PageText &pt, int &line_no)
{
	for (; block; block = block->next)
	{
		if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down)
			IndexBlocks(block->u.s.down->first_block, pt, line_no);
		if (block->type != FZ_STEXT_BLOCK_TEXT)
			continue;
		for (fz_stext_line *line = block->u.t.first_line; line; line = line->next)
		{
			for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
			{
				std::wstring f = Fold((unsigned int)ch->c);
				fz_rect r = fz_rect_from_quad(ch->quad);
				for (size_t i = 0; i < f.size(); i++)
				{
					pt.text += f[i];
					CharBox b = { r, line_no };
					pt.boxes.push_back(b);
				}
			}
			pt.text += L'\n';
			CharBox sep = { fz_empty_rect, -1 };
			pt.boxes.push_back(sep);
			line_no++;
		}
	}
}

static fz_stext_page *LoadStext(fz_context *ctx, fz_document *doc, int pno, std::wstring &err)
{
	fz_stext_page *st = NULL;
	fz_page *page = NULL;
	fz_var(st);
	fz_var(page);
	fz_try(ctx)
	{
		fz_stext_options opts;
		fz_init_stext_options(ctx, &opts);
		page = fz_load_page(ctx, doc, pno);
		st = fz_new_stext_page_from_page(ctx, page, &opts);
	}
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
	{
		err = Wide(fz_caught_message(ctx));
		st = NULL;
	}
	return st;
}

static bool IndexPage(fz_context *ctx, fz_document *doc, int pno, PageText &pt, std::wstring &err)
{
	fz_stext_page *st = LoadStext(ctx, doc, pno, err);
	if (!st)
		return false;
	int line_no = 0;
	IndexBlocks(st->first_block, pt, line_no);
	fz_drop_stext_page(ctx, st);
	return true;
}

// ---------------------------------------------------------------- expressions de recherche

static std::wstring EscapeRegex(const std::wstring &s)
{
	static const std::wstring special = L"\\^$.|?*+()[]{}/-";
	std::wstring out;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (special.find(s[i]) != std::wstring::npos)
			out += L'\\';
		out += s[i];
	}
	return out;
}

// Les espaces du terme acceptent espaces multiples et retours à la ligne.
static std::wstring TermRegex(const std::wstring &term)
{
	std::wstring folded = FoldString(Trim(term)), out, word;
	for (size_t i = 0; i <= folded.size(); i++)
	{
		if (i == folded.size() || folded[i] == L' ' || folded[i] == L'\t')
		{
			if (!word.empty())
			{
				if (!out.empty())
					out += L"\\s+";
				out += EscapeRegex(word);
				word.clear();
			}
		}
		else
			word += folded[i];
	}
	return out;
}

struct Query
{
	std::wregex rx;
	int group;            // 0 = toute la correspondance, 1 = premier groupe (motifs avec préfixe)
	std::wstring label;
};

// std::regex (ECMAScript) ne connaît pas (?<!…) : le « pas de chiffre avant » se fait avec un
// préfixe (?:^|[^0-9]) et la donnée elle-même est capturée dans le groupe 1.
static void AddPatterns(int patterns, std::vector<Query> &q)
{
	const std::regex_constants::syntax_option_type f = std::regex_constants::ECMAScript;
	if (patterns & PAT_EMAIL)
	{
		Query e = { std::wregex(L"[a-z0-9_.+-]+@[a-z0-9_-]+(?:\\.[a-z0-9_-]+)+", f), 0, TR(L"E-mail", L"E-mail") };
		q.push_back(e);
	}
	if (patterns & PAT_TEL)
	{
		Query e = { std::wregex(L"(?:^|[^0-9])((?:\\+33\\s{0,2}\\(?0?\\)?\\s{0,2}|0)[1-9](?:[\\s.-]{0,3}[0-9]{2}){4})(?![0-9])", f),
			1, TR(L"Téléphone", L"Phone number") };
		q.push_back(e);
	}
	if (patterns & PAT_IBAN)
	{
		Query e = { std::wregex(L"\\b([a-z]{2}[0-9]{2}(?:\\s{0,2}[a-z0-9]{4}){3,7}(?:\\s{0,2}[a-z0-9]{1,3})?)\\b", f),
			1, L"IBAN" };
		q.push_back(e);
	}
	if (patterns & PAT_NIR)
	{
		Query e = { std::wregex(L"(?:^|[^0-9])([12]\\s{0,2}[0-9]{2}\\s{0,2}(?:0[1-9]|1[0-2]|[2-9][0-9])\\s{0,2}(?:[0-9]{2}|2a|2b)"
			L"\\s{0,2}[0-9]{3}\\s{0,2}[0-9]{3}(?:\\s{0,2}[0-9]{2})?)(?![0-9])", f),
			1, TR(L"N° de sécurité sociale", L"French social security no.") };
		q.push_back(e);
	}
}

// Une zone par ligne couverte par la correspondance [start, end).
static void MatchZones(const PageText &pt, int pno, size_t start, size_t end, const std::wstring &label,
	std::vector<Zone> &out)
{
	std::map<int, fz_rect> per_line;
	for (size_t i = start; i < end && i < pt.boxes.size(); i++)
	{
		const CharBox &b = pt.boxes[i];
		if (b.line < 0 || fz_is_empty_rect(b.rect))
			continue;
		std::map<int, fz_rect>::iterator it = per_line.find(b.line);
		if (it == per_line.end())
			per_line[b.line] = b.rect;
		else
			it->second = fz_union_rect(it->second, b.rect);
	}
	for (std::map<int, fz_rect>::iterator it = per_line.begin(); it != per_line.end(); ++it)
	{
		Zone z = { pno, it->second.x0 - 1, it->second.y0 - 1, it->second.x1 + 1, it->second.y1 + 1, label };
		out.push_back(z);
	}
}

static int CountMatches(const std::wstring &text, const std::wstring &rx)
{
	if (rx.empty())
		return 0;
	std::wregex r(rx, std::regex_constants::ECMAScript);
	return (int)std::distance(std::wsregex_iterator(text.begin(), text.end(), r), std::wsregex_iterator());
}

// ---------------------------------------------------------------- moteur

Engine::Engine() : ctx(0), doc(0)
{
	ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (ctx)
	{
		fz_try(ctx)
			fz_register_document_handlers(ctx);
		fz_catch(ctx)
		{
			fz_drop_context(ctx);
			ctx = 0;
		}
	}
}

Engine::~Engine()
{
	Close();
	if (ctx)
		fz_drop_context(ctx);
}

void Engine::Close()
{
	if (doc)
		fz_drop_document(ctx, doc);
	doc = 0;
	pages.clear();
}

bool Engine::Open(const std::wstring &pdf, std::wstring &err)
{
	Close();
	std::string p = Utf8(pdf);
	fz_document *d = NULL;
	int needs_pass = 0, n = 0;
	fz_var(d);
	fz_var(needs_pass);
	fz_var(n);
	fz_try(ctx)
	{
		d = fz_open_document(ctx, p.c_str());
		needs_pass = fz_needs_password(ctx, d);
		if (!needs_pass)
			n = fz_count_pages(ctx, d);
	}
	fz_catch(ctx)
	{
		err = TR(L"Impossible d'ouvrir le document : ", L"Cannot open the document: ") + Wide(fz_caught_message(ctx));
		return false;
	}
	if (needs_pass)
	{
		fz_drop_document(ctx, d);
		err = TR(L"Ce PDF est protégé par un mot de passe. Retirez la protection puis réessayez.",
			L"This PDF is password-protected. Remove the protection and try again.");
		return false;
	}
	doc = d;
	path = pdf;
	for (int i = 0; i < n; i++)
	{
		fz_rect r = fz_empty_rect;
		fz_var(r);
		fz_try(ctx)
		{
			fz_page *pg = fz_load_page(ctx, doc, i);
			r = fz_bound_page(ctx, pg);
			fz_drop_page(ctx, pg);
		}
		fz_catch(ctx)
			r = fz_make_rect(0, 0, 595, 842);
		PageText pt;
		std::wstring ignored;
		IndexPage(ctx, doc, i, pt, ignored);
		bool has_text = false;
		for (size_t k = 0; k < pt.text.size() && !has_text; k++)
			has_text = pt.text[k] > L' ';
		PageInfo info = { r.x1 - r.x0, r.y1 - r.y0, has_text };
		pages.push_back(info);
	}
	if (pages.empty())
	{
		err = TR(L"Le document ne contient aucune page.", L"The document has no pages.");
		Close();
		return false;
	}
	return true;
}

HBITMAP Engine::Render(int pno, float zoom, int *w, int *h)
{
	if (!doc || pno < 0 || pno >= (int)pages.size())
		return NULL;
	fz_pixmap *pix = NULL;
	fz_page *page = NULL;
	fz_var(pix);
	fz_var(page);
	fz_try(ctx)
	{
		page = fz_load_page(ctx, doc, pno);
		pix = fz_new_pixmap_from_page(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
	}
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		return NULL;

	int pw = fz_pixmap_width(ctx, pix), ph = fz_pixmap_height(ctx, pix), stride = (int)fz_pixmap_stride(ctx, pix);
	unsigned char *src = fz_pixmap_samples(ctx, pix);
	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = pw;
	bmi.bmiHeader.biHeight = -ph;   // de haut en bas
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;
	void *bits = NULL;
	HBITMAP bmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (bmp && bits)
	{
		int dst_stride = (pw * 3 + 3) & ~3;
		for (int y = 0; y < ph; y++)
		{
			unsigned char *s = src + y * stride, *d = (unsigned char *)bits + y * dst_stride;
			for (int x = 0; x < pw; x++, s += 3, d += 3)
			{
				d[0] = s[2];   // RVB -> BVR
				d[1] = s[1];
				d[2] = s[0];
			}
		}
	}
	fz_drop_pixmap(ctx, pix);
	*w = pw;
	*h = ph;
	return bmp;
}

bool Engine::Search(const std::vector<std::wstring> &terms, int patterns, bool split_words,
	std::vector<Zone> &hits, std::wstring &err)
{
	std::vector<Query> queries;
	try
	{
		for (size_t i = 0; i < terms.size(); i++)
		{
			std::wstring rx = TermRegex(terms[i]);
			if (rx.empty())
				continue;
			Query q = { std::wregex(rx, std::regex_constants::ECMAScript), 0, terms[i] };
			queries.push_back(q);
			if (!split_words)
				continue;
			// Chaque mot d'au moins 3 lettres, en mot entier (ex. un nom tronqué dans une cellule).
			std::vector<std::wstring> words;
			std::wstring word, folded = FoldString(terms[i]) + L" ";
			for (size_t k = 0; k < folded.size(); k++)
			{
				if (folded[k] == L' ' || folded[k] == L'\t')
				{
					if (word.size() >= 3)
						words.push_back(word);
					word.clear();
				}
				else
					word += folded[k];
			}
			if (words.size() > 1)
				for (size_t k = 0; k < words.size(); k++)
				{
					Query w = { std::wregex(L"\\b" + EscapeRegex(words[k]) + L"\\b", std::regex_constants::ECMAScript), 0, terms[i] };
					queries.push_back(w);
				}
		}
		AddPatterns(patterns, queries);
	}
	catch (const std::regex_error &)
	{
		err = TR(L"Terme de recherche invalide.", L"Invalid search term.");
		return false;
	}

	for (int pno = 0; pno < (int)pages.size(); pno++)
	{
		PageText pt;
		if (!IndexPage(ctx, doc, pno, pt, err))
			return false;
		for (size_t qi = 0; qi < queries.size(); qi++)
		{
			const Query &q = queries[qi];
			for (std::wsregex_iterator it(pt.text.begin(), pt.text.end(), q.rx), end; it != end; ++it)
			{
				const std::wsmatch &m = *it;
				size_t s = (size_t)m.position(q.group), e = s + (size_t)m.length(q.group);
				MatchZones(pt, pno, s, e, q.label, hits);
			}
		}
	}
	return true;
}

static const char *ROOT_KEYS[] = {
	"Metadata", "Names", "OpenAction", "AA", "AcroForm", "Outlines", "PieceInfo", "StructTreeRoot",
	"MarkInfo", "SpiderInfo", "Threads", "Perms", "Legal", "URI", "Collection", "Dests", "PageLabels", 0
};
static const char *PAGE_KEYS[] = { "Annots", "AA", "Metadata", "PieceInfo", "Thumb", "B", "StructParents", 0 };

bool Engine::Apply(const std::vector<Zone> &zones, bool rasterize, const std::vector<std::wstring> &check_terms,
	const std::wstring &out, Report &report, std::wstring &err)
{
	if (!doc)
	{
		err = TR(L"Aucun document ouvert.", L"No document is open.");
		return false;
	}

	// Texte présent sous chaque zone, pour la vérification finale.
	std::vector<std::wstring> snippets;
	{
		std::map<int, std::vector<fz_rect> > by_page;
		for (size_t i = 0; i < zones.size(); i++)
			by_page[zones[i].page].push_back(fz_make_rect(zones[i].x0, zones[i].y0, zones[i].x1, zones[i].y1));
		for (std::map<int, std::vector<fz_rect> >::iterator it = by_page.begin(); it != by_page.end(); ++it)
		{
			fz_stext_page *st = LoadStext(ctx, doc, it->first, err);
			if (!st)
				continue;
			for (size_t k = 0; k < it->second.size(); k++)
			{
				char *txt = NULL;
				fz_var(txt);
				fz_try(ctx)
					txt = fz_copy_rectangle(ctx, st, it->second[k], 0);
				fz_catch(ctx)
					txt = NULL;
				if (txt)
				{
					std::wstring s = CollapseSpaces(Wide(txt));
					fz_free(ctx, txt);
					if (s.size() >= 3)
						snippets.push_back(s);
				}
			}
			fz_drop_stext_page(ctx, st);
		}
	}

	std::string src = Utf8(path), dst = Utf8(out);
	pdf_document *pdf = NULL, *flat = NULL;
	pdf_page *page = NULL;
	fz_pixmap *pix = NULL;
	fz_buffer *jpg = NULL, *contents = NULL;
	fz_image *img = NULL;
	pdf_obj *res = NULL, *xobj = NULL, *pg = NULL;
	fz_var(pdf); fz_var(flat); fz_var(page); fz_var(pix); fz_var(jpg); fz_var(contents);
	fz_var(img); fz_var(res); fz_var(xobj); fz_var(pg);
	const Zone *zarr = zones.empty() ? NULL : &zones[0];
	int nz = (int)zones.size();
	fz_try(ctx)
	{
		pdf = pdf_open_document(ctx, src.c_str());
		int n = pdf_count_pages(ctx, pdf);

		// 1. Annotations de caviardage puis suppression réelle, page par page.
		for (int p = 0; p < n; p++)
		{
			int count = 0;
			for (int i = 0; i < nz; i++)
				if (zarr[i].page == p)
					count++;
			if (!count)
				continue;
			page = pdf_load_page(ctx, pdf, p);
			for (int i = 0; i < nz; i++)
			{
				if (zarr[i].page != p)
					continue;
				pdf_annot *a = pdf_create_annot(ctx, page, PDF_ANNOT_REDACT);
				pdf_set_annot_rect(ctx, a, fz_make_rect(zarr[i].x0, zarr[i].y0, zarr[i].x1, zarr[i].y1));
				pdf_drop_annot(ctx, a);
			}
			pdf_redact_options ro;
			ro.black_boxes = 1;
			ro.image_method = PDF_REDACT_IMAGE_PIXELS;
			ro.line_art = PDF_REDACT_LINE_ART_REMOVE_IF_TOUCHED;
			ro.text = PDF_REDACT_TEXT_REMOVE;
			pdf_redact_page(ctx, pdf, page, &ro);
			fz_drop_page(ctx, &page->super);
			page = NULL;
		}

		// 2. Nettoyage : métadonnées, annotations, liens, formulaires, JavaScript, pièces jointes…
		pdf_obj *trailer = pdf_trailer(ctx, pdf);
		pdf_dict_del(ctx, trailer, PDF_NAME(Info));
		pdf_obj *root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
		for (int k = 0; ROOT_KEYS[k]; k++)
			pdf_dict_dels(ctx, root, ROOT_KEYS[k]);
		for (int p = 0; p < n; p++)
		{
			pdf_obj *po = pdf_lookup_page_obj(ctx, pdf, p);
			for (int k = 0; PAGE_KEYS[k]; k++)
				pdf_dict_dels(ctx, po, PAGE_KEYS[k]);
		}

		pdf_write_options wo = pdf_default_write_options;
		wo.do_incremental = 0;   // sauvegarde complète : les objets supprimés disparaissent du fichier
		wo.do_garbage = 3;
		wo.do_compress = 1;
		wo.do_compress_images = 1;
		wo.do_compress_fonts = 1;
		wo.do_clean = 1;
		wo.do_sanitize = 1;

		if (!rasterize)
			pdf_save_document(ctx, pdf, dst.c_str(), &wo);
		else
		{
			// 3. Option « aplatir » : chaque page devient une image, rien ne subsiste dessous.
			flat = pdf_create_document(ctx);
			for (int p = 0; p < n; p++)
			{
				page = pdf_load_page(ctx, pdf, p);
				fz_rect b = pdf_bound_page(ctx, page, FZ_CROP_BOX);
				float zoom = 200.0f / 72.0f;
				pix = fz_new_pixmap_from_page(ctx, &page->super, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
				fz_drop_page(ctx, &page->super);
				page = NULL;
				jpg = fz_new_buffer_from_pixmap_as_jpeg(ctx, pix, fz_default_color_params, 90, 0);
				fz_drop_pixmap(ctx, pix);
				pix = NULL;
				img = fz_new_image_from_buffer(ctx, jpg);
				fz_drop_buffer(ctx, jpg);
				jpg = NULL;
				float w = b.x1 - b.x0, h = b.y1 - b.y0;
				res = pdf_new_dict(ctx, flat, 1);
				xobj = pdf_dict_put_dict(ctx, res, PDF_NAME(XObject), 1);
				pdf_dict_puts_drop(ctx, xobj, "Im0", pdf_add_image(ctx, flat, img));
				fz_drop_image(ctx, img);
				img = NULL;
				contents = fz_new_buffer(ctx, 64);
				fz_append_printf(ctx, contents, "q %g 0 0 %g 0 0 cm /Im0 Do Q", w, h);
				pg = pdf_add_page(ctx, flat, fz_make_rect(0, 0, w, h), 0, res, contents);
				pdf_insert_page(ctx, flat, -1, pg);
				pdf_drop_obj(ctx, pg);
				pg = NULL;
				pdf_drop_obj(ctx, res);
				res = NULL;
				fz_drop_buffer(ctx, contents);
				contents = NULL;
			}
			pdf_dict_del(ctx, pdf_trailer(ctx, flat), PDF_NAME(Info));
			pdf_save_document(ctx, flat, dst.c_str(), &wo);
		}
	}
	fz_always(ctx)
	{
		if (page)
			fz_drop_page(ctx, &page->super);
		fz_drop_pixmap(ctx, pix);
		fz_drop_buffer(ctx, jpg);
		fz_drop_buffer(ctx, contents);
		fz_drop_image(ctx, img);
		pdf_drop_obj(ctx, res);
		pdf_drop_obj(ctx, pg);
		pdf_drop_document(ctx, flat);
		pdf_drop_document(ctx, pdf);
	}
	fz_catch(ctx)
	{
		err = TR(L"Échec du caviardage : ", L"Redaction failed: ") + Wide(fz_caught_message(ctx));
		return false;
	}
	return Verify(out, check_terms, snippets, report, err);
}

bool Engine::Verify(const std::wstring &out, const std::vector<std::wstring> &terms,
	const std::vector<std::wstring> &snippets, Report &report, std::wstring &err)
{
	report.ok = true;
	report.has_text = false;
	report.metadata_empty = true;
	report.terms_left.clear();
	report.snippets_left.clear();

	std::string p = Utf8(out);
	fz_document *d = NULL;
	int n = 0, info = 0, meta = 0;
	fz_var(d);
	fz_var(n);
	fz_var(info);
	fz_var(meta);
	fz_try(ctx)
	{
		d = fz_open_document(ctx, p.c_str());
		n = fz_count_pages(ctx, d);
		pdf_document *pd = pdf_specifics(ctx, d);
		if (pd)
		{
			pdf_obj *trailer = pdf_trailer(ctx, pd);
			pdf_obj *inf = pdf_dict_get(ctx, trailer, PDF_NAME(Info));
			info = pdf_is_dict(ctx, inf) && pdf_dict_len(ctx, inf) > 0;
			meta = pdf_dict_get(ctx, pdf_dict_get(ctx, trailer, PDF_NAME(Root)), PDF_NAME(Metadata)) != NULL;
		}
	}
	fz_catch(ctx)
	{
		err = TR(L"Vérification impossible : ", L"Verification failed: ") + Wide(fz_caught_message(ctx));
		return false;
	}
	report.metadata_empty = !info && !meta;

	std::wstring full;
	for (int i = 0; i < n; i++)
	{
		PageText pt;
		if (IndexPage(ctx, d, i, pt, err))
			full += pt.text + L"\n";
	}
	fz_drop_document(ctx, d);

	for (size_t i = 0; i < full.size() && !report.has_text; i++)
		report.has_text = full[i] > L' ';
	for (size_t i = 0; i < terms.size(); i++)
	{
		int c = CountMatches(full, TermRegex(terms[i]));
		if (c)
		{
			report.terms_left.push_back(std::make_pair(terms[i], c));
			report.ok = false;
		}
	}
	std::wstring flat = CollapseSpaces(full);
	for (size_t i = 0; i < snippets.size(); i++)
	{
		std::wstring needle = CollapseSpaces(FoldString(snippets[i]));
		int c = 0;
		for (size_t pos = flat.find(needle); pos != std::wstring::npos; pos = flat.find(needle, pos + 1))
			c++;
		if (c)
			report.snippets_left.push_back(std::make_pair(snippets[i], c));
	}
	return true;
}

bool Engine::ImageToPdf(const std::wstring &image, const std::wstring &pdfpath, std::wstring &err)
{
	std::string src = Utf8(image), dst = Utf8(pdfpath);
	pdf_document *pdf = NULL;
	fz_image *img = NULL;
	fz_buffer *contents = NULL;
	pdf_obj *res = NULL, *pg = NULL;
	fz_var(pdf); fz_var(img); fz_var(contents); fz_var(res); fz_var(pg);
	fz_try(ctx)
	{
		img = fz_new_image_from_file(ctx, src.c_str());
		int xres = img->xres > 0 ? img->xres : 96, yres = img->yres > 0 ? img->yres : 96;
		float w = img->w * 72.0f / xres, h = img->h * 72.0f / yres;
		pdf = pdf_create_document(ctx);
		res = pdf_new_dict(ctx, pdf, 1);
		pdf_obj *xobj = pdf_dict_put_dict(ctx, res, PDF_NAME(XObject), 1);
		pdf_dict_puts_drop(ctx, xobj, "Im0", pdf_add_image(ctx, pdf, img));
		contents = fz_new_buffer(ctx, 64);
		fz_append_printf(ctx, contents, "q %g 0 0 %g 0 0 cm /Im0 Do Q", w, h);
		pg = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, w, h), 0, res, contents);
		pdf_insert_page(ctx, pdf, -1, pg);
		pdf_save_document(ctx, pdf, dst.c_str(), &pdf_default_write_options);
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, pg);
		pdf_drop_obj(ctx, res);
		fz_drop_buffer(ctx, contents);
		fz_drop_image(ctx, img);
		pdf_drop_document(ctx, pdf);
	}
	fz_catch(ctx)
	{
		err = TR(L"Image illisible : ", L"Unreadable image: ") + Wide(fz_caught_message(ctx));
		return false;
	}
	return true;
}
