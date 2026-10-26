"""Caviardeur : application de bureau pour caviarder définitivement un document.

Le document est converti en PDF, les zones choisies sont supprimées du fichier
(pas seulement masquées), puis le PDF est nettoyé et vérifié. Tout reste sur le poste.
"""
import atexit
import os
import shutil
import sys
import tempfile
import time

import pymupdf
from PySide6.QtCore import QObject, QPointF, QRectF, QRunnable, Qt, QThreadPool, QTimer, Signal
from PySide6.QtGui import (QAction, QBrush, QColor, QFont, QIcon, QImage, QKeySequence, QPainter,
                           QPalette, QPen, QPixmap)
from PySide6.QtWidgets import (QApplication, QCheckBox, QFileDialog, QFrame, QGraphicsRectItem,
                               QGraphicsScene, QGraphicsSimpleTextItem, QGraphicsView, QGroupBox,
                               QHBoxLayout, QLabel, QMainWindow, QMenu, QMessageBox, QPlainTextEdit,
                               QProgressDialog, QPushButton, QScrollArea, QSplitter, QStyle, QToolBar,
                               QVBoxLayout, QWidget)

import convert
import icons_win2000 as icons
import redact
import theme_win2000 as theme

APP_NAME = "Caviardeur"
APP_VERSION = "1.1"
RENDER_ZOOM = 2.0       # résolution de l'aperçu (2 = 144 dpi)
PAGE_GAP = 28           # espace entre les pages, en points
OPEN_FILTER = ("Documents (*.pdf *.doc *.docx *.docm *.dot *.dotx *.rtf *.odt *.txt "
               "*.xls *.xlsx *.xlsm *.xlsb *.ods *.csv *.ppt *.pptx *.pptm *.pps *.ppsx *.odp "
               "*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.gif);;Tous les fichiers (*)")

# Dossier de travail court et hors AppData : Office refuse les chemins > 255 caractères.
ROOT = os.path.join(os.path.expanduser("~"), ".caviardeur")
os.makedirs(ROOT, exist_ok=True)
for old in os.listdir(ROOT):  # restes d'une session interrompue il y a plus de 12 h
    p = os.path.join(ROOT, old)
    if time.time() - os.path.getmtime(p) > 12 * 3600:
        shutil.rmtree(p, ignore_errors=True)
WORK = tempfile.mkdtemp(prefix="w", dir=ROOT)
atexit.register(shutil.rmtree, WORK, ignore_errors=True)


# ---------------------------------------------------------------- tâches de fond

class _Signals(QObject):
    done = Signal(object)
    failed = Signal(str)


class Task(QRunnable):
    def __init__(self, fn, *args, **kwargs):
        super().__init__()
        self.fn, self.args, self.kwargs = fn, args, kwargs
        self.signals = _Signals()

    def run(self):
        try:
            result = self.fn(*self.args, **self.kwargs)
        except Exception as e:
            self.signals.failed.emit(str(e) or e.__class__.__name__)
        else:
            self.signals.done.emit(result)


# ---------------------------------------------------------------- vue des pages

class PageView(QGraphicsView):
    """Pages empilées verticalement. Unité de la scène = point PDF."""
    zones_changed = Signal(int)
    page_changed = Signal(int, int)

    def __init__(self):
        super().__init__()
        self.setScene(QGraphicsScene(self))
        self.setBackgroundBrush(theme.APP_WORKSPACE)
        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setAlignment(Qt.AlignHCenter | Qt.AlignTop)
        self.setMouseTracking(True)
        self.pdf = None
        self.doc = None
        self.pages = []   # {"x", "y", "w", "h", "pix"}
        self.zones = []   # {"page", "rect", "label", "item"}
        self.drawing = None
        self.paused = False   # pas de rendu pendant un traitement de fond (PyMuPDF n'est pas thread-safe)
        self.verticalScrollBar().valueChanged.connect(self.schedule_render)

    # --- document
    def set_document(self, pdf_path, pages):
        if self.doc:
            self.doc.close()
        self.scene().clear()
        self.pages, self.zones = [], []
        self.pdf = pdf_path
        self.doc = pymupdf.open(pdf_path)
        maxw = max(p["w"] for p in pages)
        y = PAGE_GAP
        for i, p in enumerate(pages):
            x = (maxw - p["w"]) / 2
            label = QGraphicsSimpleTextItem(f"Page {i + 1} / {len(pages)}")
            label.setBrush(Qt.white)
            label.setFont(QFont("Tahoma", 8))
            label.setPos(x, y - 16)
            self.scene().addItem(label)
            shadow = self.scene().addRect(QRectF(x + 4, y + 4, p["w"], p["h"]), QPen(Qt.NoPen), QBrush(theme.DARK))
            shadow.setZValue(-2)
            bg = self.scene().addRect(QRectF(x, y, p["w"], p["h"]), QPen(Qt.black, 0), QBrush(Qt.white))
            bg.setZValue(-1)
            self.pages.append({"x": x, "y": y, "w": p["w"], "h": p["h"], "pix": None})
            y += p["h"] + PAGE_GAP
        self.scene().setSceneRect(QRectF(-PAGE_GAP, 0, maxw + 2 * PAGE_GAP, y))
        self.fit_width()
        self.zones_changed.emit(0)

    def close_document(self):
        if self.doc:
            self.doc.close()
            self.doc = None

    # --- rendu paresseux : seules les pages proches de l'écran sont en mémoire
    def schedule_render(self, *_):
        QTimer.singleShot(0, self.render_visible)

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self.schedule_render()

    def render_visible(self):
        if not self.doc or self.paused:
            return
        vis = self.mapToScene(self.viewport().rect()).boundingRect()
        near = vis.adjusted(0, -vis.height(), 0, vis.height())
        keep = vis.adjusted(0, -4 * vis.height(), 0, 4 * vis.height())
        center = vis.center().y()
        current = 0
        for i, p in enumerate(self.pages):
            r = QRectF(p["x"], p["y"], p["w"], p["h"])
            if p["y"] <= center:
                current = i
            if r.intersects(near) and p["pix"] is None:
                pix = self.doc[i].get_pixmap(matrix=pymupdf.Matrix(RENDER_ZOOM, RENDER_ZOOM), alpha=False)
                img = QImage(pix.samples, pix.width, pix.height, pix.stride, QImage.Format_RGB888).copy()
                item = self.scene().addPixmap(QPixmap.fromImage(img))
                item.setTransformationMode(Qt.SmoothTransformation)
                item.setScale(p["w"] / pix.width)
                item.setPos(p["x"], p["y"])
                p["pix"] = item
            elif not r.intersects(keep) and p["pix"] is not None:
                self.scene().removeItem(p["pix"])
                p["pix"] = None
        self.page_changed.emit(current + 1, len(self.pages))

    # --- zoom
    def zoom(self, factor):
        cur = self.transform().m11()
        factor = max(0.2 / cur, min(5 / cur, factor))
        self.scale(factor, factor)
        self.schedule_render()

    def fit_width(self):
        if not self.pages:
            return
        self.resetTransform()
        w = self.scene().sceneRect().width()
        f = (self.viewport().width() - 40) / w
        self.scale(min(f, 1.6), min(f, 1.6))
        self.schedule_render()

    def wheelEvent(self, e):
        if e.modifiers() & Qt.ControlModifier:
            self.zoom(1.15 if e.angleDelta().y() > 0 else 1 / 1.15)
        else:
            super().wheelEvent(e)

    # --- zones
    def page_at(self, pt):
        for i, p in enumerate(self.pages):
            if QRectF(p["x"], p["y"], p["w"], p["h"]).contains(pt):
                return i
        return None

    def add_zones(self, zones):
        seen = {self._key(z) for z in self.zones}
        added = 0
        for z in zones:
            if self._key(z) in seen:
                continue
            seen.add(self._key(z))
            p = self.pages[z["page"]]
            x0, y0, x1, y1 = z["rect"]
            item = QGraphicsRectItem(QRectF(p["x"] + x0, p["y"] + y0, x1 - x0, y1 - y0))
            item.setBrush(QColor(0, 0, 0, 150))
            item.setPen(QPen(QColor("#C00000"), 0))
            item.setZValue(2)
            item.setToolTip(f"{z.get('label') or 'Zone manuelle'} — cliquer pour retirer")
            item.setCursor(Qt.PointingHandCursor)
            self.scene().addItem(item)
            self.zones.append({"page": z["page"], "rect": [x0, y0, x1, y1],
                               "label": z.get("label"), "item": item})
            added += 1
        self.zones_changed.emit(len(self.zones))
        return added

    @staticmethod
    def _key(z):
        return (z["page"], *[round(v) for v in z["rect"]])

    def remove_zone(self, zone):
        self.scene().removeItem(zone["item"])
        self.zones.remove(zone)
        self.zones_changed.emit(len(self.zones))

    def undo(self):
        if self.zones:
            self.remove_zone(self.zones[-1])

    def clear_zones(self, page=None):
        for z in [z for z in self.zones if page is None or z["page"] == page]:
            self.scene().removeItem(z["item"])
            self.zones.remove(z)
        self.zones_changed.emit(len(self.zones))

    def zone_data(self):
        return [{"page": z["page"], "rect": z["rect"]} for z in self.zones]

    # --- souris : tracer une zone, cliquer une zone pour la retirer
    def mousePressEvent(self, e):
        if e.button() != Qt.LeftButton or not self.pages:
            return super().mousePressEvent(e)
        item = self.itemAt(e.position().toPoint())
        for z in self.zones:
            if z["item"] is item:
                self.remove_zone(z)
                return
        pt = self.mapToScene(e.position().toPoint())
        pno = self.page_at(pt)
        if pno is None:
            return super().mousePressEvent(e)
        rect = QGraphicsRectItem()
        rect.setPen(QPen(Qt.black, 0, Qt.DotLine))   # rectangle pointillé, comme une sélection classique
        rect.setBrush(QColor(10, 36, 106, 50))
        rect.setZValue(3)
        self.scene().addItem(rect)
        self.drawing = {"page": pno, "start": pt, "item": rect}

    def mouseMoveEvent(self, e):
        if self.drawing:
            p = self.pages[self.drawing["page"]]
            pt = self.mapToScene(e.position().toPoint())
            pt = QPointF(min(max(pt.x(), p["x"]), p["x"] + p["w"]), min(max(pt.y(), p["y"]), p["y"] + p["h"]))
            self.drawing["item"].setRect(QRectF(self.drawing["start"], pt).normalized())
        else:
            super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if not self.drawing:
            return super().mouseReleaseEvent(e)
        d, self.drawing = self.drawing, None
        r = d["item"].rect()
        self.scene().removeItem(d["item"])
        if r.width() > 2 and r.height() > 2:
            p = self.pages[d["page"]]
            self.add_zones([{"page": d["page"], "label": "Zone manuelle",
                             "rect": [r.left() - p["x"], r.top() - p["y"], r.right() - p["x"], r.bottom() - p["y"]]}])

    def contextMenuEvent(self, e):
        pno = self.page_at(self.mapToScene(e.pos()))
        if pno is None:
            return
        p = self.pages[pno]
        menu = QMenu(self)
        menu.addAction(f"Caviarder toute la page {pno + 1}",
                       lambda: self.add_zones([{"page": pno, "rect": [0, 0, p["w"], p["h"]], "label": "Page entière"}]))
        menu.addAction(f"Retirer les zones de la page {pno + 1}", lambda: self.clear_zones(pno))
        menu.exec(e.globalPos())


# ---------------------------------------------------------------- fenêtre principale

class Notice(QFrame):
    """Encadré blanc en creux avec une icône système (information, avertissement, erreur)."""
    ICONS = {"ok": QStyle.SP_MessageBoxInformation, "warn": QStyle.SP_MessageBoxWarning,
             "err": QStyle.SP_MessageBoxCritical}

    def __init__(self):
        super().__init__()
        self.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        self.setAutoFillBackground(True)
        self.setBackgroundRole(QPalette.Base)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(6, 6, 6, 6)
        self.icon = QLabel()
        self.icon.setAlignment(Qt.AlignTop)
        self.text = QLabel()
        self.text.setWordWrap(True)
        self.text.setTextFormat(Qt.RichText)
        lay.addWidget(self.icon)
        lay.addWidget(self.text, 1)
        self.hide()

    def show_msg(self, kind, html):
        icon = self.style().standardIcon(self.ICONS[kind])
        self.icon.setVisible(not icon.isNull())
        self.icon.setPixmap(icon.pixmap(32, 32))
        self.text.setText(html)
        self.show()


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.setWindowIcon(icons.get("app"))
        self.setAcceptDrops(True)
        self.pool = QThreadPool.globalInstance()
        self.tasks = set()
        self.src_path = self.pdf = None
        self.doc_dir = None

        self.view = PageView()
        self.view.zones_changed.connect(self.on_zones_changed)
        self.view.page_changed.connect(lambda cur, n: self.page_status.setText(f" Page {cur} sur {n} "))
        self.empty = QLabel("Ouvrez un document pour commencer.\n\n"
                            "Fichier > Ouvrir, ou faites glisser un fichier dans cette fenêtre.")
        self.empty.setAlignment(Qt.AlignCenter)
        self.empty.setAutoFillBackground(True)
        pal = self.empty.palette()
        pal.setColor(self.empty.backgroundRole(), theme.APP_WORKSPACE)
        pal.setColor(self.empty.foregroundRole(), Qt.white)
        self.empty.setPalette(pal)

        self._build_actions()
        self._build_menus()
        self._build_toolbar()
        side = self._build_side()
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0, 0, 0, 0)
        rl.addWidget(self.empty)
        rl.addWidget(self.view)
        self.view.hide()
        split = QSplitter()
        split.addWidget(side)
        split.addWidget(right)
        split.setStretchFactor(1, 1)
        split.setSizes([330, 950])
        self.setCentralWidget(split)

        sb = self.statusBar()
        self.zone_status = QLabel(" 0 zone(s) ")
        self.page_status = QLabel(" Aucun document ")
        local = QLabel(" Local : aucun envoi sur Internet ")
        for w in (self.page_status, self.zone_status, local):
            sb.addPermanentWidget(w)
        sb.showMessage("Prêt")
        self.set_enabled(False)

    # --- actions, menus, barre d'outils
    def _build_actions(self):
        def act(text, slot, key=None, icon=None, tip=None):
            a = QAction(text, self)
            a.triggered.connect(slot)
            if key:
                a.setShortcut(key)
            if icon:
                a.setIcon(icons.get(icon))
            a.setStatusTip(tip or "")
            return a

        self.act_open = act("&Ouvrir…", self.choose_file, QKeySequence.Open, "open",
                            "Ouvrir un document à caviarder")
        self.act_save = act("&Caviarder et enregistrer…", self.apply, QKeySequence.Save, "save",
                            "Supprimer définitivement les zones et enregistrer le PDF")
        self.act_quit = act("&Quitter", lambda: self.window().close())
        self.act_undo = act("&Annuler la dernière zone", lambda: self.view.undo(), QKeySequence.Undo,
                            None, "Retirer la dernière zone ajoutée")
        self.act_clear = act("&Retirer toutes les zones", self.clear_all, None, "delete",
                             "Retirer toutes les zones de toutes les pages")
        self.act_search = act("&Rechercher et marquer", self.search, QKeySequence("F3"), "search",
                              "Marquer les termes et données recherchés")
        self.act_zin = act("Zoom &avant", lambda: self.view.zoom(1.2), QKeySequence.ZoomIn, "zoom_in",
                           "Agrandir l'affichage")
        self.act_zout = act("Zoom a&rrière", lambda: self.view.zoom(1 / 1.2), QKeySequence.ZoomOut, "zoom_out",
                            "Réduire l'affichage")
        self.act_fit = act("A&juster à la largeur", lambda: self.view.fit_width(), QKeySequence("Ctrl+0"), "fit",
                           "Afficher les pages sur toute la largeur")
        self.act_about = act("À &propos du Caviardeur", self.about)

    def _build_menus(self):
        mb = self.menuBar()
        m = mb.addMenu("&Fichier")
        m.addActions([self.act_open, self.act_save])
        m.addSeparator()
        m.addAction(self.act_quit)
        m = mb.addMenu("&Edition")
        m.addActions([self.act_undo, self.act_clear])
        m.addSeparator()
        m.addAction(self.act_search)
        m = mb.addMenu("&Affichage")
        m.addActions([self.act_zin, self.act_zout, self.act_fit])
        m = mb.addMenu("&?")
        m.addAction(self.act_about)

    def _build_toolbar(self):
        tb = QToolBar()
        tb.setMovable(False)
        tb.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
        self.addToolBar(tb)
        tb.addAction(self.act_open)
        tb.addAction(self.act_save)
        tb.addSeparator()
        tb.addAction(self.act_undo)
        tb.addAction(self.act_clear)
        tb.addSeparator()
        tb.addAction(self.act_search)
        tb.addSeparator()
        for a in (self.act_zout, self.act_zin, self.act_fit):
            tb.addAction(a)
        # Libellé court sur la barre d'outils, libellé long dans les menus.
        # Les deux zooms partagent la même loupe : le « + » ou le « − » les distingue.
        for a, short in ((self.act_save, "Enregistrer"), (self.act_undo, "Annuler"),
                         (self.act_clear, "Tout retirer"), (self.act_search, "Rechercher"),
                         (self.act_zout, "Zoom −"), (self.act_zin, "Zoom +"), (self.act_fit, "Largeur")):
            a.setIconText(short)   # libellé court sous la barre d'outils, libellé long dans les menus

    def _build_side(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(8, 8, 8, 8)
        lay.setSpacing(8)

        g = QGroupBox("Document")
        gl = QVBoxLayout(g)
        self.doc_label = QLabel("Aucun document ouvert.")
        self.doc_label.setWordWrap(True)
        gl.addWidget(self.doc_label)
        self.doc_msg = Notice()
        gl.addWidget(self.doc_msg)
        btn_open = QPushButton("&Parcourir…")
        btn_open.clicked.connect(self.choose_file)
        gl.addWidget(btn_open, 0, Qt.AlignLeft)
        lay.addWidget(g)

        g = QGroupBox("Recherche automatique")
        gl = QVBoxLayout(g)
        lab = QLabel("&Termes à rechercher (un par ligne) :")
        gl.addWidget(lab)
        self.terms = QPlainTextEdit()
        self.terms.setPlaceholderText("Noms, adresses, montants…\nMajuscules et accents ignorés.")
        self.terms.setFixedHeight(80)
        lab.setBuddy(self.terms)
        gl.addWidget(self.terms)
        self.chk_split = QCheckBox("Chercher aussi chaque &mot séparément")
        self.chk_split.setToolTip("Ex. « Hélène Dupont » → « Hélène » et « Dupont » (mots de 3 lettres ou plus)")
        gl.addWidget(self.chk_split)
        gl.addWidget(QLabel("Données à repérer :"))
        self.chk_pat = {}
        for key, label in [("email", "Adresses &e-mail"), ("tel", "Numéros de télé&phone"),
                           ("iban", "&IBAN"), ("nir", "N° de &sécurité sociale")]:
            self.chk_pat[key] = QCheckBox(label)
            gl.addWidget(self.chk_pat[key])
        self.btn_search = QPushButton("&Rechercher et marquer")
        self.btn_search.clicked.connect(self.search)
        gl.addWidget(self.btn_search, 0, Qt.AlignRight)
        self.search_msg = Notice()
        gl.addWidget(self.search_msg)
        lay.addWidget(g)

        g = QGroupBox("Zones à caviarder")
        gl = QVBoxLayout(g)
        hint = QLabel("Tracez des rectangles sur les pages. Cliquez sur une zone pour la retirer. "
                      "Clic droit sur une page pour la caviarder entièrement. Ctrl + molette : zoom.")
        hint.setWordWrap(True)
        gl.addWidget(hint)
        self.count_label = QLabel("0 zone(s)")
        gl.addWidget(self.count_label)
        row = QHBoxLayout()
        self.btn_undo = QPushButton("&Annuler")
        self.btn_undo.clicked.connect(self.view.undo)
        self.btn_clear = QPushButton("&Tout retirer")
        self.btn_clear.clicked.connect(self.clear_all)
        row.addWidget(self.btn_undo)
        row.addWidget(self.btn_clear)
        row.addStretch(1)
        gl.addLayout(row)
        lay.addWidget(g)

        g = QGroupBox("Enregistrement")
        gl = QVBoxLayout(g)
        self.chk_raster = QCheckBox("Aplatir les pages en &image")
        gl.addWidget(self.chk_raster)
        hint = QLabel("Sécurité maximale : rien ne subsiste sous la page, mais le texte n'est plus "
                      "sélectionnable et le fichier est plus lourd.")
        hint.setWordWrap(True)
        gl.addWidget(hint)
        self.btn_apply = QPushButton("&Caviarder et enregistrer le PDF…")
        self.btn_apply.setDefault(True)   # bordure noire épaisse du bouton par défaut
        self.btn_apply.setMinimumHeight(26)
        self.btn_apply.clicked.connect(self.apply)
        gl.addWidget(self.btn_apply)
        self.report_msg = Notice()
        gl.addWidget(self.report_msg)
        lay.addWidget(g)
        lay.addStretch(1)

        scroll = QScrollArea()
        scroll.setWidget(w)
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setMinimumWidth(300)
        return scroll

    def set_enabled(self, on):
        for b in (self.btn_search, self.btn_apply, self.act_save, self.act_search):
            b.setEnabled(on)
        self.on_zones_changed(len(self.view.zones))

    def on_zones_changed(self, n):
        self.count_label.setText(f"<b>{n}</b> zone(s) à caviarder")
        self.zone_status.setText(f" {n} zone(s) ")
        for w in (self.btn_undo, self.btn_clear, self.act_undo, self.act_clear):
            w.setEnabled(n > 0)

    def about(self):
        QMessageBox.about(self, f"À propos du {APP_NAME}",
                          f"<b>{APP_NAME}</b> version {APP_VERSION}<br><br>"
                          "Caviardage définitif de documents PDF, Word, Excel et PowerPoint.<br>"
                          "Le contenu caviardé est supprimé du fichier, pas seulement masqué.<br><br>"
                          "Fonctionne entièrement sur ce poste : aucun fichier n'est envoyé sur Internet.")

    # --- tâches de fond avec fenêtre d'attente
    def run_task(self, text, fn, on_done, *args, **kwargs):
        dlg = QProgressDialog(text, None, 0, 0, self)
        dlg.setWindowTitle(APP_NAME)
        dlg.setWindowModality(Qt.WindowModal)
        dlg.setMinimumDuration(0)
        dlg.setCancelButton(None)
        dlg.show()
        self.statusBar().showMessage(text.split("\n")[0])
        self.view.paused = True
        task = Task(fn, *args, **kwargs)
        self.tasks.add(task)

        def finish(ok, payload):
            dlg.close()
            self.tasks.discard(task)
            self.view.paused = False
            self.statusBar().showMessage("Prêt")
            if ok:
                on_done(payload)
            else:
                QMessageBox.critical(self, APP_NAME, payload)
            self.view.schedule_render()

        task.signals.done.connect(lambda r: finish(True, r))
        task.signals.failed.connect(lambda m: finish(False, m))
        self.pool.start(task)

    # --- ouverture
    def choose_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "Ouvrir un document", "", OPEN_FILTER)
        if path:
            self.open_file(path)

    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e):
        urls = [u.toLocalFile() for u in e.mimeData().urls() if u.isLocalFile()]
        if urls:
            self.open_file(urls[0])

    def open_file(self, path):
        if self.view.zones and QMessageBox.question(
                self, APP_NAME, "Abandonner le document en cours et ses zones ?") != QMessageBox.Yes:
            return
        ext = os.path.splitext(path)[1].lower()
        if ext not in convert.SUPPORTED_EXT:
            QMessageBox.warning(self, APP_NAME, f"Format non pris en charge : {ext or 'inconnu'}")
            return
        self.view.close_document()
        if self.doc_dir:
            shutil.rmtree(self.doc_dir, ignore_errors=True)
        self.doc_dir = tempfile.mkdtemp(dir=WORK)
        text = "Ouverture du document…" if ext == ".pdf" else "Conversion en PDF avec Office…\n(quelques secondes)"
        self.run_task(text, self._load, lambda info: self._loaded(path, info), path, self.doc_dir)

    @staticmethod
    def _load(path, workdir):
        ext = os.path.splitext(path)[1].lower()
        src = os.path.join(workdir, "source" + ext)   # copie : l'original n'est jamais touché
        shutil.copyfile(path, src)
        pdf = os.path.join(workdir, "document.pdf")
        convert.to_pdf(src, pdf)
        os.remove(src)
        info = redact.normalize(pdf)
        info["pdf"] = pdf
        return info

    def _loaded(self, path, info):
        self.src_path, self.pdf = path, info["pdf"]
        pages = info["pages"]
        self.empty.hide()
        self.view.show()
        self.view.set_document(self.pdf, pages)
        converted = "" if path.lower().endswith(".pdf") else " (converti en PDF)"
        self.doc_label.setText(f"<b>{esc(os.path.basename(path))}</b><br>{len(pages)} page(s){converted}")
        scans = [str(i + 1) for i, p in enumerate(pages) if not p["has_text"]]
        if scans:
            more = "…" if len(scans) > 15 else ""
            self.doc_msg.show_msg("warn", f"Aucun texte détecté page(s) {', '.join(scans[:15])}{more} "
                                          "(document scanné ?). La recherche automatique n'y fonctionne pas : "
                                          "tracez les zones à la main.")
        else:
            self.doc_msg.hide()
        self.search_msg.hide()
        self.report_msg.hide()
        self.setWindowTitle(f"{APP_NAME} - [{os.path.basename(path)}]")
        self.set_enabled(True)

    # --- recherche
    def term_list(self):
        return [t.strip() for t in self.terms.toPlainText().splitlines() if t.strip()]

    def search(self):
        if not self.pdf:
            return
        terms = self.term_list()
        patterns = [k for k, c in self.chk_pat.items() if c.isChecked()]
        if not terms and not patterns:
            self.search_msg.show_msg("warn", "Saisissez au moins un terme ou cochez un type de donnée.")
            return
        self.run_task("Recherche en cours…", redact.search, lambda hits: self._searched(hits, terms),
                      self.pdf, terms, patterns, self.chk_split.isChecked())

    def _searched(self, hits, terms):
        self.view.add_zones(hits)
        by = {}
        for h in hits:
            by[h["label"]] = by.get(h["label"], 0) + 1
        html = f"<b>{len(hits)} occurrence(s) marquée(s)</b>"
        if by:
            html += "<br>" + "<br>".join(f"{esc(k)} : {v}" for k, v in by.items())
        missing = [t for t in terms if t not in by]
        if missing:
            html += (f"<br><br><b>Introuvable :</b> {esc(', '.join(missing))}. Vérifiez à l'œil, notamment "
                     "les mots coupés en fin de ligne ou présents dans des images.")
        self.search_msg.show_msg("ok" if hits and not missing else "warn", html)

    # --- export
    def clear_all(self):
        if QMessageBox.question(self, APP_NAME, "Retirer toutes les zones ?") == QMessageBox.Yes:
            self.view.clear_zones()

    def apply(self):
        if not self.pdf:
            return
        zones = self.view.zone_data()
        if not zones:
            self.report_msg.show_msg("warn", "Aucune zone à caviarder.")
            return
        out = os.path.join(self.doc_dir, "caviarde.pdf")
        terms = self.term_list()
        self.run_task("Caviardage et vérification…", redact.apply,
                      lambda rep: self._applied(rep, out, len(zones)),
                      self.pdf, out, zones, self.chk_raster.isChecked(), 200, terms)

    def _applied(self, report, out, n):
        if report["ok"]:
            html = f"<b>Caviardage vérifié.</b><br>{n} zone(s) supprimée(s) du fichier.<br>"
            html += ("Aucun des termes recherchés ne subsiste dans le PDF." if report["has_text"]
                     else "Le PDF ne contient plus aucun texte extractible.")
            html += f"<br>Métadonnées {'effacées' if report['metadata_empty'] else '<b>non vides</b>'}."
            kind = "ok"
        else:
            left = "<br>".join(f"{esc(t['term'])} : {t['count']}" for t in report["terms_left"])
            html = (f"<b>Attention : des termes recherchés subsistent.</b><br>{left}<br>"
                    "Ajoutez des zones à la main ou cochez « Aplatir les pages en image ».")
            kind = "err"
        if report["snippets_left"]:
            items = "<br>".join(f"« {esc(s['text'][:60])} » : {s['count']}" for s in report["snippets_left"][:10])
            html += f"<br><br>Ces textes caviardés apparaissent encore ailleurs (hors zones) :<br>{items}"
        self.report_msg.show_msg(kind, html)

        if not report["ok"] and QMessageBox.warning(
                self, APP_NAME, "Des termes recherchés subsistent dans le PDF.\n\n"
                                "Enregistrer quand même, pour contrôle ?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        base = os.path.splitext(self.src_path)[0] + "_caviarde.pdf"
        dest, _ = QFileDialog.getSaveFileName(self, "Enregistrer le PDF caviardé", base, "PDF (*.pdf)")
        if not dest:
            return
        if os.path.normcase(os.path.abspath(dest)) == os.path.normcase(os.path.abspath(self.src_path)):
            QMessageBox.warning(self, APP_NAME, "Choisissez un autre nom : le document original ne doit pas être écrasé.")
            return
        try:
            shutil.copyfile(out, dest)
        except OSError as e:
            QMessageBox.critical(self, APP_NAME, f"Enregistrement impossible :\n{e}")
            return
        self.statusBar().showMessage(f"Enregistré : {dest}", 10000)
        box = QMessageBox(self)
        box.setWindowTitle(APP_NAME)
        box.setIcon(QMessageBox.Information)
        box.setText(f"Le PDF caviardé a été enregistré :\n{dest}\n\nRelisez-le avant toute diffusion.")
        open_btn = box.addButton("&Ouvrir le PDF", QMessageBox.AcceptRole)
        box.addButton("Fermer", QMessageBox.RejectRole)
        box.exec()
        if box.clickedButton() is open_btn:
            os.startfile(dest)

    def closeEvent(self, e):
        self.view.close_document()
        super().closeEvent(e)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    theme.apply_theme(app)
    app.setWindowIcon(icons.get("app"))
    win = MainWindow()
    frame = theme.Win2000Frame(win)
    frame.resize(1180, 800)
    frame.show()
    # « Ouvrir avec » ou fichier glissé sur l'exe
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        QTimer.singleShot(200, lambda: win.open_file(sys.argv[1]))
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
