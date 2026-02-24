from PySide6.QtWidgets import QStackedWidget, QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt

class PreviewStackWidget(QStackedWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.placeholder = QLabel("Hover over a file and press Space")
        self.placeholder.setObjectName("PlaceholderLabel")
        self.placeholder.setAlignment(Qt.AlignCenter)
        self.addWidget(self.placeholder)

    def show_message(self, message):
        self.placeholder.setText(message)
        self.setCurrentWidget(self.placeholder)
