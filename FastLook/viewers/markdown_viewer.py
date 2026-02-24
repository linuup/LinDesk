from PySide6.QtWidgets import QWidget, QVBoxLayout, QTextBrowser
import markdown

class MarkdownViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        
        self.browser = QTextBrowser()
        self.browser.setOpenExternalLinks(True)
        self.layout.addWidget(self.browser)

    def load_file(self, file_path):
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                text = f.read()
                html = markdown.markdown(text)
                self.browser.setHtml(html)
        except Exception as e:
            self.browser.setPlainText(f"Error loading markdown: {str(e)}")
