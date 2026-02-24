import uiautomation as auto
import win32com.client
import os
import urllib.parse
import sys

def get_path_under_mouse():
    """
    Returns the full path of the file or folder currently under the mouse cursor
    in Windows Explorer or on the Desktop.
    """
    try:
        # 1. Get the element under the cursor
        control = auto.ControlFromCursor()
        if not control:
            print("Debug: No control under cursor")
            return None
        
        # 2. Find the top-level window
        window = control.GetTopLevelControl()
        if not window:
            print("Debug: No top level window")
            return None
        
        class_name = window.ClassName
        print(f"Debug: Window class: {class_name}")
        # CabinetWClass = Explorer, WorkerW/Progman = Desktop
        if class_name not in ['CabinetWClass', 'WorkerW', 'Progman']:
            return None

        # 3. Use Shell.Application to find the folder path
        shell = win32com.client.Dispatch("Shell.Application")
        hwnd = window.NativeWindowHandle
        
        folder_path = None
        
        if class_name in ['WorkerW', 'Progman']:
             # Desktop path
             folder_path = os.path.join(os.environ['USERPROFILE'], 'Desktop')
             print(f"Debug: Desktop path: {folder_path}")
        else:
            # Match HWND with open Explorer windows
            for win in shell.Windows():
                if win.HWND == hwnd:
                    url = win.LocationURL
                    if url:
                        # Decode file URL
                        parsed = urllib.parse.urlparse(url)
                        folder_path = urllib.parse.unquote(parsed.path)
                        if folder_path.startswith('/'):
                            folder_path = folder_path[1:]
                        folder_path = folder_path.replace('/', '\\')
                        # Handle cases like "C:" vs "C:\"
                        if len(folder_path) == 2 and folder_path[1] == ':':
                            folder_path += '\\'
                    break
        
        if not folder_path:
            print("Debug: Could not determine folder path")
            return None

        # 4. Find the item name by traversing up from the hovered control
        print(f"Debug: Hovered control name: {control.Name}, type: {control.ControlTypeName}")
        curr = control
        
        # We try to look for the most specific file name first
        while curr and curr != window:
            name = curr.Name
            # Some symbols or names might not be files, but if we are in a list item, we should try to match
            if name and (curr.ControlTypeName in ['ListItem', 'DataItem', 'ListItemControl', 'DataItemControl'] or 'Item' in curr.ControlTypeName):
                # Try with name as is
                full_path = os.path.join(folder_path, name)
                if os.path.exists(full_path):
                    print(f"Debug: Found path: {full_path}")
                    return full_path
                
                # Check for files without extension (hidden extensions)
                try:
                    # Match exact name without extension or exact full name
                    for entry in os.scandir(folder_path):
                        # Case insensitive check for convenience
                        if entry.name.lower() == name.lower() or os.path.splitext(entry.name.lower())[0] == name.lower():
                            print(f"Debug: Match found in directory: {entry.path}")
                            return entry.path
                except Exception:
                    pass

            curr = curr.GetParentControl()

        print("Debug: No valid file path found in traversal")
        return None
    except Exception as e:
        print(f"Debug: Exception in get_path_under_mouse: {str(e)}")
        return None

if __name__ == "__main__":
    # Test script
    import time
    print("Hover over a file in Explorer and wait 3 seconds...")
    time.sleep(3)
    path = get_path_under_mouse()
    print(f"Path detected: {path}")
