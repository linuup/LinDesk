import os

class FileTypeDetector:
    TEXT_EXTS = {
        '.txt', '.py', '.c', '.cpp', '.h', '.hpp', '.java', '.php', 
        '.js', '.ts', '.html', '.css', '.go', '.rs', '.rb', '.sql', 
        '.cs', '.sh', '.bat', '.json', '.yaml', '.yml', '.xml', 
        '.log', '.list', '.conf', '.ini', '.ps1', '.lua', '.md', '.markdown',
        '.env', '.gitignore', '.dockerfile', '.toml', '.gradle', '.properties',
        '.cmake', '.makefile', '.am', '.in', '.pc'
    }
    IMAGE_EXTS = {'.png', '.jpg', '.jpeg', '.gif', '.bmp', '.ico', '.webp', '.svg'}
    PDF_EXTS = {'.pdf'}
    OFFICE_EXTS = {'.doc', '.docx', '.xls', '.xlsx', '.ppt', '.pptx'}
    ARCHIVE_EXTS = {'.zip', '.7z', '.tar', '.gz', '.bz2', '.xz', '.tgz', '.rar'}
    
    # Files that are either dangerous, useless to peek, or too large/binary
    BLOCKED_EXTS = {
        '.exe', '.msi', '.dll', '.sys', '.bin', '.dat', '.obj', '.lib', 
        '.url', '.pif', '.scr', '.com', '.cpl', '.ocx',
        '.iso',
        '.tmp', '.bak', '.old', '.swp'
    }

    @staticmethod
    def is_previewable(file_path):
        if not os.path.exists(file_path):
            return False
        if os.path.isdir(file_path):
            return True
        _, ext = os.path.splitext(file_path)
        return ext.lower() not in FileTypeDetector.BLOCKED_EXTS

    @staticmethod
    def detect_type(file_path):
        if not os.path.exists(file_path):
            return "unknown"
        
        if os.path.isdir(file_path):
            return "folder"
        
        _, ext = os.path.splitext(file_path)
        ext = ext.lower()
        
        if ext in FileTypeDetector.TEXT_EXTS:
            return "text"
        elif ext in FileTypeDetector.IMAGE_EXTS:
            return "image"
        elif ext in FileTypeDetector.PDF_EXTS:
            return "pdf"
        elif ext in FileTypeDetector.OFFICE_EXTS:
            return "office"
        elif ext in FileTypeDetector.ARCHIVE_EXTS:
            return "archive"
        else:
            return "web" # Fallback

if __name__ == "__main__":
    detector = FileTypeDetector()
    test_files = ["test.py", "test.cpp", "test.java", "test.php", "test.txt", "test.md", "image.png", "document.pdf"]
    print("Testing extension detection logic:")
    for f in test_files:
        _, ext = os.path.splitext(f)
        ext = ext.lower()
        t = "web"
        if ext in detector.TEXT_EXTS: t = "text"
        elif ext in detector.IMAGE_EXTS: t = "image"
        elif ext in detector.PDF_EXTS: t = "pdf"
        elif ext in detector.OFFICE_EXTS: t = "office"
        elif ext in detector.ARCHIVE_EXTS: t = "archive"
        print(f"Extension: {ext} -> Category: {t}")
