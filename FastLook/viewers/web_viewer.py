from PySide6.QtWidgets import QWidget, QVBoxLayout
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtCore import QUrl

class WebView(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        
        self.web_view = QWebEngineView()
        self.layout.addWidget(self.web_view)

    def load_file(self, file_path):
        import os
        url = QUrl.fromLocalFile(os.path.abspath(file_path))
        self.web_view.load(url)
