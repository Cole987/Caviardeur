"""Thème Windows 2000 : palette, police, cadre de fenêtre et barre de titre classiques."""
from PySide6.QtCore import QEvent, QPoint, QRect, Qt
from PySide6.QtGui import QColor, QFont, QLinearGradient, QPainter, QPalette
from PySide6.QtWidgets import (QHBoxLayout, QProxyStyle, QPushButton, QStyle, QStyleFactory,
                               QVBoxLayout, QWidget)

import icons_win2000 as icons

FACE = QColor("#D4D0C8")         # gris « bouton » de Windows 2000
HILIGHT = QColor("#FFFFFF")
SHADOW = QColor("#808080")
DARK = QColor("#404040")
NAVY = QColor("#0A246A")         # barre de titre active, sélection
NAVY_END = QColor("#A6CAF0")
INACTIVE = QColor("#808080")
INACTIVE_END = QColor("#C0C0C0")
APP_WORKSPACE = QColor("#808080")  # fond de l'espace de travail derrière les pages
BORDER = 4                          # épaisseur du cadre de fenêtre


class Win2000Style(QProxyStyle):
    """Style « Windows » de Qt (reliefs 3D façon 9x/2000) ; icônes système prises dans le dossier « icones »."""
    ICONS = {
        QStyle.SP_MessageBoxInformation: "info",
        QStyle.SP_MessageBoxQuestion: "question",
        QStyle.SP_MessageBoxWarning: "warning",
        QStyle.SP_MessageBoxCritical: "error",
        QStyle.SP_DialogOpenButton: "open",
        QStyle.SP_DialogSaveButton: "save",
    }

    def __init__(self):
        super().__init__(QStyleFactory.create("Windows"))

    def standardIcon(self, sp, option=None, widget=None):
        if sp in self.ICONS:
            return icons.get(self.ICONS[sp])   # vide si le fichier manque : pas d'icône de remplacement
        return super().standardIcon(sp, option, widget)


def apply_theme(app):
    app._win2000_style = Win2000Style()   # garder une référence Python vivante
    app.setStyle(app._win2000_style)
    pal = QPalette()
    for group in (QPalette.Active, QPalette.Inactive, QPalette.Disabled):
        pal.setColor(group, QPalette.Window, FACE)
        pal.setColor(group, QPalette.Button, FACE)
        pal.setColor(group, QPalette.Base, HILIGHT)
        pal.setColor(group, QPalette.AlternateBase, FACE)
        pal.setColor(group, QPalette.Light, HILIGHT)
        pal.setColor(group, QPalette.Midlight, FACE)
        pal.setColor(group, QPalette.Mid, SHADOW)
        pal.setColor(group, QPalette.Dark, SHADOW)
        pal.setColor(group, QPalette.Shadow, DARK)
        pal.setColor(group, QPalette.Highlight, NAVY)
        pal.setColor(group, QPalette.HighlightedText, HILIGHT)
        pal.setColor(group, QPalette.ToolTipBase, QColor("#FFFFE1"))
        pal.setColor(group, QPalette.ToolTipText, Qt.black)
        pal.setColor(group, QPalette.Link, NAVY)
        pal.setColor(group, QPalette.PlaceholderText, SHADOW)
        for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
            pal.setColor(group, role, SHADOW if group == QPalette.Disabled else Qt.black)
    app.setPalette(pal)
    app.setFont(QFont("Tahoma", 8))


def draw_raised(p, r):
    """Relief « fenêtre » de Windows 2000 : 2 traits clairs en haut à gauche, 2 sombres en bas à droite."""
    x0, y0, x1, y1 = r.left(), r.top(), r.right(), r.bottom()
    for color, a, b in ((FACE, (x0, y0), (x1 - 1, y0)), (FACE, (x0, y0), (x0, y1 - 1)),
                        (DARK, (x0, y1), (x1, y1)), (DARK, (x1, y0), (x1, y1)),
                        (HILIGHT, (x0 + 1, y0 + 1), (x1 - 2, y0 + 1)), (HILIGHT, (x0 + 1, y0 + 1), (x0 + 1, y1 - 2)),
                        (SHADOW, (x0 + 1, y1 - 1), (x1 - 1, y1 - 1)), (SHADOW, (x1 - 1, y0 + 1), (x1 - 1, y1 - 1))):
        p.setPen(color)
        p.drawLine(QPoint(*a), QPoint(*b))


class CaptionButton(QPushButton):
    """Bouton de légende (réduire, agrandir, restaurer, fermer) aux symboles dessinés pixel par pixel."""

    def __init__(self, glyph, parent):
        super().__init__(parent)
        self.glyph = glyph   # "min", "max", "restore" ou "close"
        self.setFixedSize(16, 14)
        self.setFocusPolicy(Qt.NoFocus)

    def set_glyph(self, glyph):
        self.glyph = glyph
        self.update()

    def paintEvent(self, e):
        super().paintEvent(e)   # relief du bouton
        p = QPainter(self)
        p.setPen(Qt.black)
        o = 1 if self.isDown() else 0
        x, y = 4 + o, 3 + o
        if self.glyph == "min":
            p.fillRect(x, y + 6, 6, 2, Qt.black)
        elif self.glyph == "max":
            p.drawRect(x - 1, y - 1, 8, 7)
            p.drawLine(x - 1, y, x + 7, y)
        elif self.glyph == "restore":
            p.drawRect(x + 1, y - 1, 6, 5)
            p.drawLine(x + 1, y, x + 7, y)
            p.fillRect(x - 1, y + 2, 7, 6, FACE)
            p.drawRect(x - 1, y + 2, 6, 5)
            p.drawLine(x - 1, y + 3, x + 5, y + 3)
        elif self.glyph == "close":
            for d in (0, 1):
                p.drawLine(x + d, y, x + 6 + d, y + 6)
                p.drawLine(x + 6 + d, y, x + d, y + 6)


class TitleBar(QWidget):
    HEIGHT = 18

    def __init__(self, frame):
        super().__init__(frame)
        self.frame = frame
        self.setFixedHeight(self.HEIGHT)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 2, 2, 2)
        lay.setSpacing(0)
        lay.addStretch(1)
        self.btn_min = CaptionButton("min", self)
        self.btn_max = CaptionButton("max", self)
        self.btn_close = CaptionButton("close", self)
        self.btn_min.clicked.connect(frame.showMinimized)
        self.btn_max.clicked.connect(frame.toggle_maximized)
        self.btn_close.clicked.connect(frame.close)
        lay.addWidget(self.btn_min)
        lay.addWidget(self.btn_max)
        lay.addSpacing(2)
        lay.addWidget(self.btn_close)

    def paintEvent(self, _):
        p = QPainter(self)
        active = self.frame.isActiveWindow()
        g = QLinearGradient(0, 0, self.width(), 0)
        g.setColorAt(0, NAVY if active else INACTIVE)
        g.setColorAt(1, NAVY_END if active else INACTIVE_END)
        p.fillRect(self.rect(), g)
        icon = self.frame.windowIcon()
        if not icon.isNull():
            icon.paint(p, QRect(2, 1, 16, 16))
        f = QFont("Tahoma", 8)
        f.setBold(True)
        p.setFont(f)
        p.setPen(HILIGHT if active else FACE)
        text_rect = QRect(21, 0, self.width() - 21 - 56, self.height())
        title = p.fontMetrics().elidedText(self.frame.windowTitle(), Qt.ElideRight, text_rect.width())
        p.drawText(text_rect, Qt.AlignVCenter | Qt.AlignLeft, title)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton and not self.frame.maximized:
            self.frame.windowHandle().startSystemMove()

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.frame.toggle_maximized()


class Win2000Frame(QWidget):
    """Fenêtre sans cadre système, avec bordure en relief et barre de titre Windows 2000."""

    def __init__(self, content):
        super().__init__(None, Qt.Window | Qt.FramelessWindowHint)
        self.content = content
        self.maximized = False
        self._normal = None
        self.setMouseTracking(True)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(BORDER, BORDER, BORDER, BORDER)
        lay.setSpacing(1)
        self.title = TitleBar(self)
        lay.addWidget(self.title)
        lay.addWidget(content)
        self.setWindowTitle(content.windowTitle())
        self.setWindowIcon(content.windowIcon())
        content.windowTitleChanged.connect(self.setWindowTitle)
        self.windowTitleChanged.connect(lambda _: self.title.update())

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), FACE)
        if not self.maximized:
            draw_raised(p, self.rect())

    def changeEvent(self, e):
        if e.type() == QEvent.ActivationChange:
            self.title.update()
        super().changeEvent(e)

    def toggle_maximized(self):
        if self.maximized:
            self.maximized = False
            self.setGeometry(self._normal)
            self.title.btn_max.set_glyph("max")
        else:
            self._normal = self.geometry()
            self.maximized = True
            # Zone de travail de l'écran : la barre des tâches reste visible.
            self.setGeometry(self.screen().availableGeometry())
            self.title.btn_max.set_glyph("restore")
        self.update()

    # --- redimensionnement par les bords
    def _edges(self, pos):
        if self.maximized:
            return Qt.Edges()
        m, edges = BORDER + 2, Qt.Edges()
        if pos.x() <= m:
            edges |= Qt.LeftEdge
        if pos.x() >= self.width() - m:
            edges |= Qt.RightEdge
        if pos.y() <= m:
            edges |= Qt.TopEdge
        if pos.y() >= self.height() - m:
            edges |= Qt.BottomEdge
        return edges

    def mouseMoveEvent(self, e):
        edges = self._edges(e.position().toPoint())
        diag1 = (Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.BottomEdge)
        diag2 = (Qt.RightEdge | Qt.TopEdge, Qt.LeftEdge | Qt.BottomEdge)
        if edges in diag1:
            self.setCursor(Qt.SizeFDiagCursor)
        elif edges in diag2:
            self.setCursor(Qt.SizeBDiagCursor)
        elif edges & (Qt.LeftEdge | Qt.RightEdge):
            self.setCursor(Qt.SizeHorCursor)
        elif edges & (Qt.TopEdge | Qt.BottomEdge):
            self.setCursor(Qt.SizeVerCursor)
        else:
            self.unsetCursor()

    def mousePressEvent(self, e):
        edges = self._edges(e.position().toPoint())
        if e.button() == Qt.LeftButton and edges:
            self.windowHandle().startSystemResize(edges)

    def closeEvent(self, e):
        self.content.close()
        super().closeEvent(e)
