from PySide6.QtWidgets import QWidget, QVBoxLayout
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtCore import Qt
import os

try:
    from pygments import highlight
    from pygments.lexers import get_lexer_for_filename, TextLexer
    from pygments.formatters import HtmlFormatter
    PYGMENTS_AVAILABLE = True
except ImportError:
    PYGMENTS_AVAILABLE = False

class TextViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        
        self.web_view = QWebEngineView()
        self.layout.addWidget(self.web_view)

    def load_file(self, file_path):
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()

            if PYGMENTS_AVAILABLE:
                try:
                    lexer = get_lexer_for_filename(file_path)
                except Exception:
                    lexer = TextLexer()
                
                # GitHub-like style configuration
                formatter = HtmlFormatter(
                    full=False, # Changed to False to get only a snippet
                    linenos=True,
                    style='friendly',
                    noclasses=False,
                    cssclass="highlight"
                )
                
                style_css = formatter.get_style_defs('.highlight')
                
                # Custom Softened GitHub-style refinement CSS
                custom_css = f"""
                <style>
                {style_css}
                * {{ box-sizing: border-box; }}
                body {{ 
                    background-color: #fcfcfc; 
                    margin: 0; 
                    padding: 0;
                    color: #2f363d;
                    font-family: 'JetBrains Mono', 'Cascadia Code', 'Consolas', monospace;
                }}
                .highlight {{ 
                    background: #fcfcfc; 
                    font-size: 15px;
                    line-height: 1.6;
                }}
                .highlight table {{
                    border-spacing: 0;
                    width: 100%;
                    border-collapse: collapse;
                }}
                .highlight td {{
                    padding: 0;
                    margin: 0;
                    vertical-align: top;
                }}
                .highlight pre {{ 
                    margin: 0; 
                    padding: 20px 0; 
                    white-space: pre;
                    overflow-x: auto;
                }}
                .highlight .linenos {{ 
                    color: #959da5;
                    text-align: right; 
                    padding: 0 10px 0 5px; 
                    user-select: none;
                    background-color: #f8f9fa;
                    border-right: 1px solid #e1e4e8;
                    min-width: 40px;
                }}
                .highlight .code {{
                    padding-left: 15px;
                    background-color: #fcfcfc;
                }}
                .highlight .code pre {{
                    padding-left: 15px;
                }}
                
                /* Softer Token Colors */
                .highlight .k {{ color: #d73a49; }} 
                .highlight .s {{ color: #032f62; }} 
                .highlight .c {{ color: #6a737d; font-style: italic; }} 
                .highlight .nf {{ color: #6f42c1; }} 
                .highlight .nb {{ color: #005cc5; }} 
                .highlight .nd {{ color: #6f42c1; }} 
                .highlight .nc {{ color: #6f42c1; font-weight: bold; }} 
                .highlight .nv {{ color: #e36209; }} 
                .highlight .o {{ color: #d73a49; }} 
                .highlight .mi {{ color: #005cc5; }} 
                
                ::-webkit-scrollbar {{ width: 10px; height: 10px; }}
                ::-webkit-scrollbar-track {{ background: #fcfcfc; }}
                ::-webkit-scrollbar-thumb {{ background: #e1e4e8; border-radius: 5px; border: 2px solid #fcfcfc; }}
                ::-webkit-scrollbar-thumb:hover {{ background: #d1d5da; }}
                </style>
                """
                
                inner_html = highlight(content, lexer, formatter)
                final_html = f"<!DOCTYPE html><html><head><meta charset='utf-8'>{custom_css}</head><body>{inner_html}</body></html>"
                self.web_view.setHtml(final_html)
            else:
                # Fallback if pygments is not found (though it should be)
                self.web_view.setHtml(f"<html><body><pre>{content}</pre></body></html>")
                
        except Exception as e:
            self.web_view.setHtml(f"<html><body><p>Error loading file: {str(e)}</p></body></html>")

if __name__ == "__main__":
    import sys
    from PySide6.QtWidgets import QApplication
    app = QApplication(sys.argv)
    viewer = TextViewer()
    viewer.load_file(__file__)
    viewer.resize(800, 600)
    viewer.show()
    sys.exit(app.exec())
