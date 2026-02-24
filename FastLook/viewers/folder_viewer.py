from PySide6.QtWidgets import (QWidget, QVBoxLayout, QTreeView, QLineEdit, 
                             QHeaderView, QFileSystemModel, QMenu)
from PySide6.QtGui import QAction, QGuiApplication
from PySide6.QtCore import Qt, QSortFilterProxyModel, QModelIndex, QDir, QUrl, QMimeData
import os
import subprocess

class SubtreeFilterProxyModel(QSortFilterProxyModel):
    """A proxy model that strictly limits results to a specific root path."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.root_path = ""

    def set_root_path(self, path):
        self.root_path = os.path.normpath(path).lower()

    def filterAcceptsRow(self, source_row, source_parent):
        # 1. Standard index retrieval
        model = self.sourceModel()
        index = model.index(source_row, 0, source_parent)
        path = os.path.normpath(model.filePath(index)).lower()

        # 2. STRICT CHECK: Must be under the current root
        if self.root_path and not path.startswith(self.root_path):
            return False

        # 3. Apply standard string filtering
        return super().filterAcceptsRow(source_row, source_parent)

class FolderViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(10, 10, 10, 10)
        self.layout.setSpacing(10)
        self.current_root = ""

        # Search Bar
        self.search_bar = QLineEdit()
        self.search_bar.setPlaceholderText("Search inside this folder...")
        self.search_bar.setObjectName("FolderSearch")
        self.search_bar.textChanged.connect(self.on_search_changed)
        self.layout.addWidget(self.search_bar)

        # Models
        self.file_model = QFileSystemModel()
        self.file_model.setFilter(QDir.NoDotAndDotDot | QDir.AllEntries)
        
        self.proxy_model = SubtreeFilterProxyModel()
        self.proxy_model.setSourceModel(self.file_model)
        self.proxy_model.setFilterCaseSensitivity(Qt.CaseInsensitive)
        self.proxy_model.setFilterKeyColumn(0)
        self.proxy_model.setRecursiveFilteringEnabled(True)

        # Tree View
        self.tree_view = QTreeView()
        self.tree_view.setModel(self.proxy_model)
        self.tree_view.setSortingEnabled(True)
        self.tree_view.setSelectionMode(QTreeView.ExtendedSelection)
        self.tree_view.setIndentation(20)
        self.tree_view.setObjectName("FolderTree")
        self.tree_view.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree_view.customContextMenuRequested.connect(self.show_context_menu)
        
        header = self.tree_view.header()
        header.setSectionResizeMode(QHeaderView.ResizeToContents)
        header.setStretchLastSection(True)
        
        self.layout.addWidget(self.tree_view)

    def load_file(self, folder_path):
        if not os.path.exists(folder_path) or not os.path.isdir(folder_path):
            return

        self.current_root = folder_path
        self.proxy_model.set_root_path(folder_path)
        self.file_model.setRootPath(folder_path)
        
        source_index = self.file_model.index(folder_path)
        proxy_index = self.proxy_model.mapFromSource(source_index)
        self.tree_view.setRootIndex(proxy_index)
        
        self.tree_view.sortByColumn(0, Qt.AscendingOrder)
        self.search_bar.clear()
        self.tree_view.header().setSectionResizeMode(0, QHeaderView.ResizeToContents)

    def on_search_changed(self, text):
        self.proxy_model.setFilterFixedString(text)
        if text:
            # Expand all matches to show recursive results
            self.tree_view.expandAll()
        else:
            # Re-anchor root to current folder when search is cleared,
            # otherwise the view drifts up to the filesystem root (C:\)
            if self.current_root:
                source_index = self.file_model.index(self.current_root)
                proxy_index = self.proxy_model.mapFromSource(source_index)
                self.tree_view.setRootIndex(proxy_index)
            self.tree_view.collapseAll()

    def show_context_menu(self, pos):
        index = self.tree_view.indexAt(pos)
        if not index.isValid():
            return

        source_index = self.proxy_model.mapToSource(index)
        file_path = self.file_model.filePath(source_index)

        menu = QMenu(self)
        
        open_action = QAction("Open", self)
        open_action.triggered.connect(lambda: os.startfile(file_path))
        menu.addAction(open_action)
        
        explore_action = QAction("Open in Explorer", self)
        explore_action.triggered.connect(lambda: subprocess.run(['explorer', '/select,', os.path.normpath(file_path)]))
        menu.addAction(explore_action)
        
        menu.addSeparator()
        
        copy_action = QAction("Copy", self)
        copy_action.triggered.connect(lambda: self.copy_file(file_path))
        menu.addAction(copy_action)

        copy_path_action = QAction("Copy Path", self)
        copy_path_action.triggered.connect(lambda: QGuiApplication.clipboard().setText(file_path))
        menu.addAction(copy_path_action)

        menu.exec(self.tree_view.viewport().mapToGlobal(pos))

    def copy_file(self, file_path):
        url = QUrl.fromLocalFile(os.path.abspath(file_path))
        mime = QMimeData()
        mime.setUrls([url])
        QGuiApplication.clipboard().setMimeData(mime)

    def get_path_at_pos(self, global_pos):
        """Returns the file path at the given global position, if any."""
        local_pos = self.tree_view.viewport().mapFromGlobal(global_pos)
        index = self.tree_view.indexAt(local_pos)
        if index.isValid():
            source_index = self.proxy_model.mapToSource(index)
            return self.file_model.filePath(source_index)
        return None
