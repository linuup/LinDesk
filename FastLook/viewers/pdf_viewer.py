from PySide6.QtWidgets import QWidget, QVBoxLayout
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWebEngineCore import QWebEngineSettings
from PySide6.QtCore import QUrl
import os

class PDFViewer(QWidget):
    """
    Using QWebEngineView to render PDF files (Chromium's built-in PDF viewer).
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        
        self.web_view = QWebEngineView()
        
        # Explicitly enable PDF viewer plugin
        settings = self.web_view.settings()
        settings.setAttribute(QWebEngineSettings.PluginsEnabled, True)
        settings.setAttribute(QWebEngineSettings.PdfViewerEnabled, True)
        
        self.layout.addWidget(self.web_view)

    def load_file(self, file_path):
        abs_path = os.path.abspath(file_path)
        # Using #toolbar=0 to hide the built-in PDF viewer toolbar
        url_str = QUrl.fromLocalFile(abs_path).toString() + "#toolbar=0&navpanes=0"
        self.web_view.setUrl(QUrl(url_str))
