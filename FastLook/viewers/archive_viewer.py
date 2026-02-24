from PySide6.QtWidgets import (QWidget, QVBoxLayout, QTreeWidget, QTreeWidgetItem, 
                             QLineEdit, QHeaderView)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
import os
import zipfile
import tarfile
try:
    import py7zr
except ImportError:
    py7zr = None
try:
    import rarfile
except ImportError:
    rarfile = None

class ArchiveViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(10, 10, 10, 10)
        self.layout.setSpacing(10)

        # Search Bar
        self.search_bar = QLineEdit()
        self.search_bar.setPlaceholderText("Filter archive contents...")
        self.search_bar.setObjectName("FolderSearch")
        self.search_bar.textChanged.connect(self.filter_items)
        self.layout.addWidget(self.search_bar)

        # Tree Widget
        self.tree_view = QTreeWidget()
        self.tree_view.setHeaderLabels(["Name", "Size", "Type", "Modified"])
        self.tree_view.setObjectName("FolderTree")
        self.tree_view.setSortingEnabled(True)
        self.tree_view.header().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.tree_view.header().setStretchLastSection(True)
        self.layout.addWidget(self.tree_view)

        self.archive_items = []

    def load_file(self, file_path):
        self.tree_view.clear()
        self.archive_items = []
        self.search_bar.clear()
        
        if not os.path.exists(file_path):
            return

        ext = os.path.splitext(file_path)[1].lower()
        if ext == '.zip':
            self.load_zip(file_path)
        elif ext in ['.tar', '.gz', '.bz2', '.xz', '.tgz']:
            self.load_tar(file_path)
        elif ext == '.7z':
            self.load_7z(file_path)
        elif ext == '.rar':
            self.load_rar(file_path)

    def _add_to_tree(self, paths, name, size, is_dir, date_str):
        parts = name.strip('/').split('/')
        parent_item = self.tree_view
        current_path = ""
        
        for i, part in enumerate(parts):
            current_path = os.path.join(current_path, part).replace('\\', '/')
            
            if current_path not in paths:
                item = QTreeWidgetItem()
                item.setText(0, part)
                
                is_this_part_file = (i == len(parts) - 1 and not is_dir)
                
                if is_this_part_file:
                    size_mb = size / (1024 * 1024)
                    item.setText(1, f"{size_mb:.2f} MB" if size_mb > 0.1 else f"{size/1024:.1f} KB")
                    item.setText(2, "File")
                else:
                    item.setText(2, "Folder")
                
                item.setText(3, date_str)
                
                if isinstance(parent_item, QTreeWidget):
                    parent_item.addTopLevelItem(item)
                else:
                    parent_item.addChild(item)
                
                paths[current_path] = item
                self.archive_items.append(item)
                
            parent_item = paths[current_path]

    def load_zip(self, file_path):
        try:
            with zipfile.ZipFile(file_path, 'r') as z:
                paths = {}
                for info in sorted(z.infolist(), key=lambda x: x.filename):
                    date = info.date_time
                    date_str = f"{date[0]}-{date[1]:02d}-{date[2]:02d} {date[3]:02d}:{date[4]:02d}"
                    self._add_to_tree(paths, info.filename, info.file_size, info.is_dir(), date_str)
        except Exception as e:
            self.tree_view.addTopLevelItem(QTreeWidgetItem([f"Error: {str(e)}"]))

    def load_tar(self, file_path):
        try:
            with tarfile.open(file_path, 'r:*') as t:
                paths = {}
                for member in sorted(t.getmembers(), key=lambda x: x.name):
                    import datetime
                    date_str = datetime.datetime.fromtimestamp(member.mtime).strftime('%Y-%m-%d %H:%M')
                    self._add_to_tree(paths, member.name, member.size, member.isdir(), date_str)
        except Exception as e:
            self.tree_view.addTopLevelItem(QTreeWidgetItem([f"Error: {str(e)}"]))

    def load_7z(self, file_path):
        if not py7zr:
            self.tree_view.addTopLevelItem(QTreeWidgetItem(["py7zr not installed"]))
            return
        try:
            with py7zr.SevenZipFile(file_path, mode='r') as z:
                paths = {}
                for info in sorted(z.list(), key=lambda x: x.filename):
                    date_str = info.creationtime.strftime('%Y-%m-%d %H:%M') if info.creationtime else ""
                    self._add_to_tree(paths, info.filename, info.uncompressed, info.is_directory, date_str)
        except Exception as e:
            self.tree_view.addTopLevelItem(QTreeWidgetItem([f"Error: {str(e)}"]))

    def load_rar(self, file_path):
        if not rarfile:
            self.tree_view.addTopLevelItem(QTreeWidgetItem(["rarfile not installed"]))
            return
        try:
            # Note: rarfile may require unrar.exe on PATH
            with rarfile.RarFile(file_path) as r:
                paths = {}
                for info in sorted(r.infolist(), key=lambda x: x.filename):
                    date = info.date_time
                    date_str = f"{date[0]}-{date[1]:02d}-{date[2]:02d} {date[3]:02d}:{date[4]:02d}"
                    self._add_to_tree(paths, info.filename, info.file_size, info.isdir(), date_str)
        except Exception as e:
            msg = str(e)
            if "unrar" in msg.lower():
                msg = "RAR support requires unrar binary on system PATH."
            self.tree_view.addTopLevelItem(QTreeWidgetItem([f"Error: {msg}"]))

    def filter_items(self, text):
        text = text.lower()
        # Simple recursive hide/show
        for i in range(self.tree_view.topLevelItemCount()):
            item = self.tree_view.topLevelItem(i)
            self._filter_recursive(item, text)

    def _filter_recursive(self, item, text):
        match = text in item.text(0).lower()
        child_match = False
        
        for i in range(item.childCount()):
            if self._filter_recursive(item.child(i), text):
                child_match = True
        
        should_show = match or child_match
        item.setHidden(not should_show)
        if match and text:
            item.setExpanded(True)
        return should_show
    def get_path_at_pos(self, global_pos):
        """Returns the virtual internal path at the given global position."""
        local_pos = self.tree_view.viewport().mapFromGlobal(global_pos)
        item = self.tree_view.itemAt(local_pos)
        if item:
            # Reconstruct the internal path
            path_parts = []
            curr = item
            while curr:
                path_parts.insert(0, curr.text(0))
                curr = curr.parent()
            return "/".join(path_parts)
        return None
