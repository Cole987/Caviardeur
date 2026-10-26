"""Conversion de documents bureautiques en PDF, en local.

Ordre de préférence : Microsoft Office (via COM), puis LibreOffice s'il est installé.
Les images sont converties directement par PyMuPDF.
"""
import os
import shutil
import subprocess
import threading

import pymupdf

WORD_EXT = {".doc", ".docx", ".docm", ".dot", ".dotx", ".rtf", ".odt", ".txt"}
EXCEL_EXT = {".xls", ".xlsx", ".xlsm", ".xlsb", ".ods", ".csv"}
POWERPOINT_EXT = {".ppt", ".pptx", ".pptm", ".pps", ".ppsx", ".odp"}
IMAGE_EXT = {".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".gif"}
SUPPORTED_EXT = {".pdf"} | WORD_EXT | EXCEL_EXT | POWERPOINT_EXT | IMAGE_EXT

# Office ne supporte pas bien les appels COM concurrents.
_office_lock = threading.Lock()


class ConversionError(Exception):
    pass


def to_pdf(src, out_pdf):
    ext = os.path.splitext(src)[1].lower()
    if ext not in SUPPORTED_EXT:
        raise ConversionError(f"Format non pris en charge : {ext}")
    if ext == ".pdf":
        shutil.copyfile(src, out_pdf)
        return
    if ext in IMAGE_EXT:
        _image_to_pdf(src, out_pdf)
        return

    errors = []
    try:
        with _office_lock:
            _office_to_pdf(src, out_pdf, ext)
        if os.path.exists(out_pdf):
            return
    except Exception as e:  # Office absent ou fichier refusé
        errors.append(f"Office : {e}")
    try:
        _libreoffice_to_pdf(src, out_pdf)
        if os.path.exists(out_pdf):
            return
    except Exception as e:
        errors.append(f"LibreOffice : {e}")
    raise ConversionError("Conversion impossible. " + " | ".join(errors))


def _image_to_pdf(src, out_pdf):
    with pymupdf.open(src) as img:
        pdf_bytes = img.convert_to_pdf()
    with open(out_pdf, "wb") as f:
        f.write(pdf_bytes)


def _office_to_pdf(src, out_pdf, ext):
    import pythoncom
    import win32com.client

    src = os.path.abspath(src)
    out_pdf = os.path.abspath(out_pdf)
    pythoncom.CoInitialize()
    try:
        if ext in WORD_EXT:
            # DispatchEx : instance dédiée, pour ne jamais toucher aux documents ouverts par l'utilisateur.
            app = win32com.client.DispatchEx("Word.Application")
            app.Visible = False
            app.DisplayAlerts = 0
            try:
                doc = app.Documents.Open(src, ConfirmConversions=False, ReadOnly=True,
                                         AddToRecentFiles=False, PasswordDocument="x",
                                         Visible=False)
                # Item=0 : contenu sans marques de révision ni commentaires.
                doc.ExportAsFixedFormat(OutputFileName=out_pdf, ExportFormat=17,
                                        OpenAfterExport=False, OptimizeFor=0, Item=0,
                                        IncludeDocProps=False, CreateBookmarks=0,
                                        DocStructureTags=False)
                doc.Close(SaveChanges=0)
            finally:
                app.Quit(SaveChanges=0)
        elif ext in EXCEL_EXT:
            app = win32com.client.DispatchEx("Excel.Application")
            app.Visible = False
            app.DisplayAlerts = False
            try:
                wb = app.Workbooks.Open(src, UpdateLinks=0, ReadOnly=True, Password="x",
                                        AddToMru=False)
                wb.ExportAsFixedFormat(Type=0, Filename=out_pdf, Quality=0,
                                       IncludeDocProperties=False, IgnorePrintAreas=False,
                                       OpenAfterPublish=False)
                wb.Close(SaveChanges=False)
            finally:
                app.Quit()
        elif ext in POWERPOINT_EXT:
            # PowerPoint n'accepte qu'une instance : on ne la ferme que si rien d'autre n'y est ouvert.
            app = win32com.client.DispatchEx("PowerPoint.Application")
            pres = app.Presentations.Open(src, ReadOnly=True, Untitled=False, WithWindow=False)
            try:
                pres.SaveAs(out_pdf, 32)  # ppSaveAsPDF
            finally:
                pres.Close()
                if app.Presentations.Count == 0:
                    app.Quit()
    finally:
        pythoncom.CoUninitialize()


def _find_soffice():
    for cand in (shutil.which("soffice"),
                 r"C:\Program Files\LibreOffice\program\soffice.exe",
                 r"C:\Program Files (x86)\LibreOffice\program\soffice.exe"):
        if cand and os.path.exists(cand):
            return cand
    return None


def _libreoffice_to_pdf(src, out_pdf):
    soffice = _find_soffice()
    if not soffice:
        raise ConversionError("LibreOffice non installé")
    outdir = os.path.dirname(os.path.abspath(out_pdf))
    subprocess.run([soffice, "--headless", "--convert-to", "pdf", "--outdir", outdir, src],
                   check=True, timeout=300, capture_output=True)
    produced = os.path.join(outdir, os.path.splitext(os.path.basename(src))[0] + ".pdf")
    if produced != os.path.abspath(out_pdf):
        os.replace(produced, out_pdf)
