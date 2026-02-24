from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel, QScrollArea
from PySide6.QtGui import QPixmap, QImage
from PySide6.QtCore import Qt

class ImageViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setAlignment(Qt.AlignCenter)
        self.scroll_area.setStyleSheet("border: none;")
        
        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignCenter)
        self.scroll_area.setWidget(self.image_label)
        
        self.layout.addWidget(self.scroll_area)

    def load_file(self, file_path):
        pixmap = QPixmap(file_path)
        if pixmap.isNull():
            self.image_label.setText("Failed to load image.")
            return

        # Simple scaling if the image is too large
        screen_size = self.size()
        if not pixmap.isNull():
             self.image_label.setPixmap(pixmap.scaled(self.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
        
    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.image_label.pixmap():
            self.image_label.setPixmap(self.image_label.pixmap().scaled(self.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
