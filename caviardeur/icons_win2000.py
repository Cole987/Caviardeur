"""Icônes de l'application (Windows 2000/98), lues dans le dossier « icones ».

Dans l'exe, le dossier est intégré (build.py) : version à usage personnel uniquement,
ces icônes appartenant à Microsoft. Aucune icône n'est dessinée : si un fichier manque,
l'élément s'affiche sans icône (texte seul).
"""
import os
import sys

from PySide6.QtGui import QIcon

FILES = {
    # barre d'outils et menus
    "open": "w2k_open_folder.ico",
    "save": "w98_diskettes_copy.ico",
    "search": "w2k_find.ico",
    "delete": "w2k_recycle_bin_empty.ico",
    "zoom_in": "w98_magnifying_glass.ico",
    "zoom_out": "w98_magnifying_glass.ico",
    "fit": "w2k_default_application.ico",
    # boîtes de message
    "info": "w2k_info.ico",
    "warning": "w2k_warning.ico",
    "error": "w2k_stop.ico",
    "question": "w98_msg_question.ico",
    # fenêtre
    "app": "w2k_write_document.ico",
}


def icons_dir():
    if getattr(sys, "frozen", False):
        return os.path.join(sys._MEIPASS, "icones")   # dossier intégré à l'exe
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "icones")


_cache = {}


def get(name):
    """QIcon du fichier associé à `name`, ou icône vide si le fichier est absent."""
    if name not in _cache:
        filename = FILES.get(name)
        path = os.path.join(icons_dir(), filename) if filename else ""
        _cache[name] = QIcon(path) if path and os.path.isfile(path) else QIcon()
    return _cache[name]


def missing():
    """Fichiers attendus mais absents du dossier « icones »."""
    return sorted({f for f in FILES.values() if not os.path.isfile(os.path.join(icons_dir(), f))})
