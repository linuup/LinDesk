from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QPushButton, QHBoxLayout, QLabel)
from PySide6.QtCore import Qt, Signal, QObject
from PySide6.QtGui import QCursor, QGuiApplication, QIcon
import win32gui
import win32con
import os

from file_type_detector import FileTypeDetector
from viewers.text_viewer import TextViewer
from viewers.image_viewer import ImageViewer
from viewers.markdown_viewer import MarkdownViewer
from viewers.pdf_viewer import PDFViewer
from viewers.web_viewer import WebView
from viewers.folder_viewer import FolderViewer
from viewers.archive_viewer import ArchiveViewer

class PreviewWindow(QMainWindow):
    # Signal to notify controller when this window is closed
    closed = Signal(str) # Emits the file path

    def __init__(self, file_path):
        super().__init__()
        self.file_path = os.path.abspath(file_path)
        self.setWindowTitle("Fastlook")
        self.resize(800, 600)
        
        # Set Window Icon
        icon_path = os.path.join(os.path.dirname(__file__), "app_icon.png")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        
        # Fluent UI Window Setup
        self.setWindowFlags(Qt.Window | Qt.FramelessWindowHint)
        self.setAttribute(Qt.WA_TranslucentBackground)

        # Load Global Style
        style_path = os.path.join(os.path.dirname(__file__), "style.css")
        if os.path.exists(style_path):
            with open(style_path, "r", encoding="utf-8") as f:
                self.setStyleSheet(f.read())

        self.central_widget = QWidget()
        self.central_widget.setObjectName("CentralWidget")
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.layout.setContentsMargins(1, 1, 1, 1)
        self.layout.setSpacing(0)

        # Custom Title Bar (Modern Centered)
        self.title_bar_widget = QWidget()
        self.title_bar_widget.setObjectName("TitleBar")
        self.title_bar_widget.setFixedHeight(38)
        self.title_bar = QHBoxLayout(self.title_bar_widget)
        self.title_bar.setContentsMargins(10, 2, 10, 2)
        
        self.title_bar.addStretch()
        self.title_label = QLabel(f"Fastlook - {os.path.basename(file_path)}")
        self.title_label.setObjectName("TitleLabel")
        self.title_label.setAlignment(Qt.AlignCenter)
        self.title_bar.addWidget(self.title_label)
        self.title_bar.addStretch()
        
        self.btn_pin = QPushButton("📌")
        self.btn_pin.setObjectName("PinButton")
        self.btn_pin.setFixedSize(28, 28)
        self.is_pinned = False
        self.btn_pin.clicked.connect(self.toggle_pin)
        self.title_bar.addWidget(self.btn_pin)

        self.btn_close = QPushButton("✕")
        self.btn_close.setObjectName("CloseButton")
        self.btn_close.setFixedSize(28, 28)
        self.btn_close.clicked.connect(self.close)
        self.title_bar.addWidget(self.btn_close)
        
        self.layout.addWidget(self.title_bar_widget)

        self.viewer_container = QVBoxLayout()
        self.viewer_container.setContentsMargins(0, 0, 0, 0)
        self.layout.addLayout(self.viewer_container)
        
        self.load_preview()

    def toggle_pin(self):
        self.is_pinned = not self.is_pinned
        self.btn_pin.setProperty("pinned", "true" if self.is_pinned else "false")
        self.btn_pin.style().unpolish(self.btn_pin)
        self.btn_pin.style().polish(self.btn_pin)
        
        hwnd = self.winId()
        if self.is_pinned:
            win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, 0, 0, 0, 0,
                                 win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)
        else:
            win32gui.SetWindowPos(hwnd, win32con.HWND_NOTOPMOST, 0, 0, 0, 0,
                                 win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)

    def load_preview(self):
        file_type = FileTypeDetector.detect_type(self.file_path)
        self.viewer = self.get_viewer_for_type(file_type)
        self.viewer_container.addWidget(self.viewer)
        try:
            self.viewer.load_file(self.file_path)
        except Exception:
            pass

    def get_viewer_for_type(self, file_type):
        if file_type == "text": return TextViewer()
        if file_type == "image": return ImageViewer()
        if file_type == "pdf": return PDFViewer()
        if file_type == "office": return PDFViewer()
        if file_type == "folder": return FolderViewer()
        if file_type == "archive": return ArchiveViewer()
        return WebView()

    def showEvent(self, event):
        super().showEvent(event)

    def bring_to_front(self):
        """Brings the window to the front and activates it."""
        if self.isMinimized():
            self.showNormal()

        self.show()
        self.raise_()
        self.activateWindow()

        # Force foreground using Win32 API.
        # Strategy: briefly set HWND_TOPMOST to steal focus, then immediately
        # restore HWND_NOTOPMOST (unless the user has pinned the window).
        # This avoids leaving the window permanently on-top without user consent.
        try:
            hwnd = self.winId()
            if not win32gui.IsWindowVisible(hwnd):
                win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
            win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, 0, 0, 0, 0,
                                  win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)
            if not self.is_pinned:
                win32gui.SetWindowPos(hwnd, win32con.HWND_NOTOPMOST, 0, 0, 0, 0,
                                      win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)
        except Exception:
            pass

    def closeEvent(self, event):
        self.closed.emit(self.file_path)
        super().closeEvent(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.drag_pos = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
            event.accept()

    def mouseMoveEvent(self, event):
        if event.buttons() == Qt.LeftButton:
            self.move(event.globalPosition().toPoint() - self.drag_pos)
            event.accept()
