"""Recherche, caviardage définitif et vérification des PDF (PyMuPDF)."""
import os
import re
import unicodedata

import pymupdf

# Les séparateurs acceptent \s : un numéro peut être coupé par un retour à la ligne.
PATTERNS = {
    "email": (r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+", "E-mail"),
    "tel": (r"(?<!\d)(?:\+33\s{0,2}\(?0?\)?\s{0,2}|0)[1-9](?:[\s.-]{0,3}\d{2}){4}(?!\d)", "Téléphone"),
    "iban": (r"\b[a-z]{2}\d{2}(?:\s{0,2}[a-z0-9]{4}){3,7}(?:\s{0,2}[a-z0-9]{1,3})?\b", "IBAN"),
    "nir": (r"(?<!\d)[12]\s{0,2}\d{2}\s{0,2}(?:0[1-9]|1[0-2]|[2-9]\d)\s{0,2}(?:\d{2}|2a|2b)\s{0,2}\d{3}\s{0,2}\d{3}(?:\s{0,2}\d{2})?(?!\d)",
            "N° de sécurité sociale"),
}


def _fold(c):
    """Minuscule, sans accent ; peut renvoyer plusieurs caractères (ex. ligature « ﬁ »)."""
    if c in "   ":
        return " "
    base = "".join(x for x in unicodedata.normalize("NFKD", c) if not unicodedata.combining(x))
    return (base or c).lower()


def page_index(page):
    """Texte normalisé de la page + rectangle et numéro de ligne de chaque caractère."""
    out, boxes = [], []
    line_no = 0
    for block in page.get_text("rawdict")["blocks"]:
        if block.get("type") != 0:
            continue
        for line in block["lines"]:
            for span in line["spans"]:
                for ch in span["chars"]:
                    for f in _fold(ch["c"]):
                        out.append(f)
                        boxes.append((pymupdf.Rect(ch["bbox"]), line_no))
            out.append("\n")
            boxes.append((None, None))
            line_no += 1
    return "".join(out), boxes


def _match_rects(boxes, start, end):
    """Un rectangle par ligne couverte par la correspondance."""
    per_line = {}
    for rect, ln in boxes[start:end]:
        if rect is None or rect.is_empty:
            continue
        per_line[ln] = per_line[ln] | rect if ln in per_line else pymupdf.Rect(rect)
    return [r + (-1, -1, 1, 1) for r in per_line.values()]


def term_regex(term):
    folded = "".join(_fold(c) for c in term.strip())
    parts = [re.escape(p) for p in folded.split()]
    # Les espaces du terme acceptent espaces multiples et retours à la ligne.
    return r"\s+".join(parts)


def search(pdf_path, terms, pattern_keys, split_words=False):
    queries = [(term_regex(t), t) for t in terms if t.strip()]
    if split_words:
        # Chaque mot d'au moins 3 lettres, en mot entier (ex. un nom tronqué dans une cellule Excel).
        for t in terms:
            words = t.split()
            if len(words) > 1:
                queries += [(r"(?<!\w)" + term_regex(w) + r"(?!\w)", t) for w in words if len(w) >= 3]
    queries += [(PATTERNS[k][0], PATTERNS[k][1]) for k in pattern_keys if k in PATTERNS]
    hits = []
    with pymupdf.open(pdf_path) as doc:
        for pno, page in enumerate(doc):
            text, boxes = page_index(page)
            for rx, label in queries:
                for m in re.finditer(rx, text):
                    for r in _match_rects(boxes, m.start(), m.end()):
                        hits.append({"page": pno, "rect": [r.x0, r.y0, r.x1, r.y1],
                                     "label": label, "text": m.group(0)})
    return hits


def normalize(pdf_path):
    """Supprime la rotation des pages pour que coordonnées affichées et réelles coïncident."""
    doc = pymupdf.open(pdf_path)
    if doc.needs_pass:
        doc.close()
        raise ValueError("Ce PDF est protégé par un mot de passe. Retirez la protection puis réessayez.")
    changed = False
    for page in doc:
        if page.rotation:
            page.remove_rotation()
            changed = True
    info = {"pages": [{"w": p.rect.width, "h": p.rect.height,
                       "has_text": bool(p.get_text("text").strip())} for p in doc]}
    if changed:
        doc.save(pdf_path + ".tmp", garbage=3, deflate=True)
        doc.close()
        os.replace(pdf_path + ".tmp", pdf_path)
    else:
        doc.close()
    return info


def render_png(pdf_path, pno, zoom):
    with pymupdf.open(pdf_path) as doc:
        return doc[pno].get_pixmap(matrix=pymupdf.Matrix(zoom, zoom), alpha=False).tobytes("png")


def apply(pdf_path, out_path, zones, rasterize=False, dpi=200, check_terms=()):
    doc = pymupdf.open(pdf_path)

    # Texte présent sous chaque zone, pour la vérification finale.
    hidden = set()
    for z in zones:
        snippet = " ".join(doc[z["page"]].get_text("text", clip=pymupdf.Rect(z["rect"])).split())
        if len(snippet) >= 3:
            hidden.add(snippet)

    for z in zones:
        doc[z["page"]].add_redact_annot(pymupdf.Rect(z["rect"]), fill=(0, 0, 0))
    for page in doc:
        page.apply_redactions(images=pymupdf.PDF_REDACT_IMAGE_PIXELS,
                              graphics=pymupdf.PDF_REDACT_LINE_ART_REMOVE_IF_TOUCHED,
                              text=pymupdf.PDF_REDACT_TEXT_REMOVE)

    # Nettoyage : métadonnées, pièces jointes, JavaScript, liens, annotations, texte invisible...
    doc.scrub(attached_files=True, clean_pages=True, embedded_files=True, hidden_text=True,
              javascript=True, metadata=True, redactions=True, remove_links=True,
              reset_fields=True, reset_responses=True, thumbnails=True, xml_metadata=True)
    for page in doc:
        for annot in list(page.annots() or []):
            page.delete_annot(annot)
        for w in list(page.widgets() or []):
            page.delete_widget(w)
    doc.set_toc([])

    if rasterize:
        flat = pymupdf.open()
        for page in doc:
            pix = page.get_pixmap(dpi=dpi, alpha=False)
            new = flat.new_page(width=page.rect.width, height=page.rect.height)
            new.insert_image(new.rect, stream=pix.tobytes("jpeg", jpg_quality=90))
        doc.close()
        doc = flat
    doc.set_metadata({})
    doc.del_xml_metadata()
    # Sauvegarde complète (jamais incrémentale) : les objets supprimés disparaissent du fichier.
    doc.save(out_path, garbage=4, deflate=True, clean=True, incremental=False)
    doc.close()

    return verify(out_path, check_terms, hidden)


def verify(out_path, check_terms, hidden):
    report = {"ok": True, "terms_left": [], "snippets_left": [], "has_text": False}
    with pymupdf.open(out_path) as doc:
        full = "\n".join(page_index(p)[0] for p in doc)
        report["has_text"] = bool(full.strip())
        report["metadata_empty"] = not any(v for k, v in (doc.metadata or {}).items()
                                           if k not in ("format", "encryption"))
    flat = " ".join(full.split())
    for t in check_terms:
        if t.strip():
            n = len(re.findall(term_regex(t), full))
            if n:
                report["terms_left"].append({"term": t, "count": n})
                report["ok"] = False
    for s in hidden:
        folded = " ".join("".join(_fold(c) for c in s).split())
        n = flat.count(folded)
        if n:
            report["snippets_left"].append({"text": s, "count": n})
    return report
