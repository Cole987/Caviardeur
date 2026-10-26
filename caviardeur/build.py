"""Construit dist/Caviardeur.exe (un seul fichier, sans Python requis sur le poste cible).

Usage : python build.py

Le dossier « icones » (icônes Windows 2000/98 de Microsoft) est intégré à l'exe :
cet exe est réservé à un usage personnel et ne doit pas être diffusé.
"""
import os
import sys

import PyInstaller.__main__

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "icones")
EXE_ICON = os.path.join(ICONS, "w2k_write_document.ico")
if not os.path.isfile(EXE_ICON):
    sys.exit(f"Dossier d'icônes incomplet : {EXE_ICON} introuvable.")

PyInstaller.__main__.run([
    os.path.join(HERE, "caviardeur.py"),
    "--name", "Caviardeur",
    "--onefile",
    "--windowed",
    "--icon", EXE_ICON,
    "--add-data", f"{ICONS}{os.pathsep}icones",
    "--hidden-import", "win32com.client",
    "--hidden-import", "pythoncom",
    # Analyse de mise en page par IA de PyMuPDF : inutile ici, et ses fichiers de données
    # ne sont pas embarqués (plantage au démarrage). Sans eux, PyMuPDF s'en passe.
    "--exclude-module", "pymupdf.layout",
    "--exclude-module", "pymupdf_layout",
    "--exclude-module", "pymupdf4llm",
    "--exclude-module", "onnxruntime",
    "--distpath", os.path.join(HERE, "dist"),
    "--workpath", os.path.join(HERE, "build"),
    "--specpath", os.path.join(HERE, "build"),
    "--noconfirm",
    "--clean",
])
