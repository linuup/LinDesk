import sys
import os
import ctypes
import keyboard
import win32com.client
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import Qt, QObject, Signal, QPoint, QTimer
from PySide6.QtGui import QCursor, QIcon
from preview_manager import PreviewWindow
from explorer_integration import get_path_under_mouse
from viewers.folder_viewer import FolderViewer
from viewers.archive_viewer import ArchiveViewer
from file_type_detector import FileTypeDetector

class HotkeySignaler(QObject):
    triggered = Signal()

class AppController(QObject):
    def __init__(self):
        super().__init__()
        self.windows = []  # list of open PreviewWindow instances
        self.hotkey_signaler = HotkeySignaler()
        self.hotkey_signaler.triggered.connect(self.on_hotkey)
        keyboard.add_hotkey('space', self.hotkey_signaler.triggered.emit)

    def resolve_shortcut(self, path):
        # Support up to 3 levels of nested shortcuts
        curr_path = path
        for _ in range(3):
            if not curr_path.lower().endswith('.lnk'):
                break
            try:
                shell = win32com.client.Dispatch("WScript.Shell")
                shortcut = shell.CreateShortCut(curr_path)
                target = shortcut.TargetPath
                if target and os.path.exists(target):
                    curr_path = target
                else:
                    break
            except Exception as e:
                print(f"Debug: Shortcut resolution failed for {curr_path}: {e}")
                break
        return curr_path

    def on_hotkey(self):
        mouse_pos = QCursor.pos()

        # If the mouse is hovering over a non-folder/archive PreviewWindow,
        # close that window and stop. (activeWindow() is unreliable inside a
        # keyboard hook because focus state may be stale at signal delivery time.)
        widget = QApplication.widgetAt(mouse_pos)
        if widget:
            parent = widget
            while parent:
                if isinstance(parent, PreviewWindow):
                    if not isinstance(getattr(parent, 'viewer', None), (FolderViewer, ArchiveViewer)):
                        parent.close()
                        return
                    break  # Folder/archive preview → fall through to normal logic
                parent = parent.parentWidget()

        target_path = None
        from_internal = False  # True when triggered from within a FolderViewer/ArchiveViewer

        # 1. Internal detection (within our own previews)
        widget = QApplication.widgetAt(mouse_pos)
        if widget:    
            parent = widget
            while parent:
                if isinstance(parent, (FolderViewer, ArchiveViewer)):
                    target_path = parent.get_path_at_pos(mouse_pos)
                    if target_path:
                        from_internal = True
                    break
                parent = parent.parentWidget()
         
        # 2. External detection (Explorer / Desktop)
        if not target_path: 
            target_path = get_path_under_mouse()
            
        def close_all_windows():
            for win in list(self.windows):
                if not win.is_pinned:
                    win.close()

        if not target_path:
            close_all_windows()
            return 
     
        # Resolve .lnk shortcuts
        actual_path = self.resolve_shortcut(os.path.abspath(target_path))  
        abs_path = os.path.abspath(actual_path)
        
        # Redirection logic: if it's a file that isn't previewable, show its parent folder
        desktop_path = os.path.join(os.environ['USERPROFILE'], 'Desktop').lower()
        if os.path.isfile(abs_path) and not FileTypeDetector.is_previewable(abs_path):
            parent_dir = os.path.dirname(abs_path)
            if parent_dir.lower() != desktop_path:
                abs_path = parent_dir                                     
            else:
                close_all_windows()
                return

        # Normalize path
        abs_path = os.path.normpath(abs_path)
        win_key = abs_path.lower()

        # Duplicate-window guard: only applies for external (Explorer/Desktop) triggers.
        # When triggered from inside a FolderViewer, always open a fresh preview.
        if not from_internal:
            existing = [w for w in self.windows if os.path.normpath(w.file_path).lower() == win_key]
            if existing:
                pinned = [w for w in existing if w.is_pinned]
                if pinned:
                    pinned[0].bring_to_front()
                close_all_windows()
                return

        # FINAL CHECK: Don't open a generic Desktop preview if not explicitly requested
        if abs_path.lower() == desktop_path:
            # Only allow if the user specifically hovered over a FOLDER icon on desktop,
            # not just the background or a blocked file.
            if not os.path.isdir(target_path) or target_path.lower() == desktop_path:
                close_all_windows()
                return

        if FileTypeDetector.is_previewable(abs_path):
            if not from_internal:
                close_all_windows()
            win = PreviewWindow(abs_path)
            win.closed.connect(lambda path, w=win: self.on_window_closed(w))
            self.windows.append(win)
                                 
            screen = QApplication.screenAt(mouse_pos)
            if not screen:
                screen = QApplication.primaryScreen()
            
            screen_geometry = screen.availableGeometry()
            win_size = win.size()
            
            base_x = screen_geometry.x() + (screen_geometry.width() - win_size.width()) // 2
            base_y = screen_geometry.y() + (screen_geometry.height() - win_size.height()) // 2
            
            offset_idx = (len(self.windows) - 1) % 8
            offset = offset_idx * 30
            
            win.move(base_x + offset, base_y + offset)
            win.bring_to_front()
            # Delay slightly so Explorer releases focus before we claim it
            QTimer.singleShot(50, win.activateWindow)
        else:
            close_all_windows()

    def on_window_closed(self, win):
        if win in self.windows:
            self.windows.remove(win)

def main():
    # Set AppUserModelID to ensure taskbar icon works correctly on Windows
    myappid = u'linuup.fastlook.previewer.1.0'
    ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)

    app = QApplication(sys.argv)
    app.setApplicationName("Fastlook")
    app.setApplicationDisplayName("Fastlook")
    app.setQuitOnLastWindowClosed(False)
    
    icon_path = os.path.join(os.path.dirname(__file__), "app_icon.png")
    if os.path.exists(icon_path):
        app.setWindowIcon(QIcon(icon_path))
    
    controller = AppController()
    
    sys.exit(app.exec())

if __name__ == "__main__":
    main()