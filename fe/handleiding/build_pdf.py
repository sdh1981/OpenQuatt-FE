"""Bouw fe/handleiding-ombouw.pdf uit fe/handleiding-ombouw.md.

Gebruik (vanuit de repo-root):
    python -m pip install markdown xhtml2pdf svglib
    python fe/handleiding/build_pdf.py
"""

import pathlib
import re
import sys

import markdown
from xhtml2pdf import pisa

FE_DIR = pathlib.Path(__file__).resolve().parent.parent
SOURCE = FE_DIR / "handleiding-ombouw.md"
TARGET = FE_DIR / "handleiding-ombouw.pdf"

# Een TrueType-letter met pijlen en wiskundetekens (→, ≥); Helvetica heeft die niet.
FONT_CANDIDATES = [
    ("C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialbd.ttf"),
    ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
]
FONT_CSS = next(
    (
        f'@font-face {{ font-family: Doc; src: url("{regular}"); }}\n'
        f'@font-face {{ font-family: Doc; src: url("{bold}"); font-weight: bold; }}\n'
        for regular, bold in FONT_CANDIDATES
        if pathlib.Path(regular).exists() and pathlib.Path(bold).exists()
    ),
    "",
)

CSS = FONT_CSS + """
@page { size: a4 portrait; margin: 16mm 15mm 16mm 15mm;
        @frame footer { -pdf-frame-content: footer; bottom: 6mm; margin-left: 15mm; margin-right: 15mm; height: 8mm; } }
body, p, li, td, th, h1, h2, h3, blockquote { font-family: Doc, Helvetica; }
body { font-size: 9.5pt; line-height: 1.35; color: #111; }
h1 { font-size: 17pt; margin: 0 0 6pt 0; }
h2 { font-size: 13pt; margin: 14pt 0 4pt 0; border-bottom: 0.6pt solid #999; padding-bottom: 2pt; }
h3 { font-size: 11pt; margin: 10pt 0 3pt 0; }
p, li { margin: 2pt 0; }
blockquote { background: #fff4e5; border-left: 3pt solid #ef6c00; padding: 4pt 6pt; margin: 6pt 0; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 6pt 0; }
th, td { border: 0.5pt solid #999; padding: 3pt 4pt; vertical-align: top; font-size: 8.8pt; }
th { background: #eef2f7; }
code { font-family: Courier; font-size: 8.8pt; }
img { width: 180mm; }
#footer { font-size: 7.5pt; color: #666; text-align: center; }
"""


def link_callback(uri, _rel):
    path = (FE_DIR / uri).resolve()
    return str(path) if path.exists() else uri


def main() -> int:
    text = SOURCE.read_text(encoding="utf-8")
    # Links naar andere Markdown-bestanden worden in de PDF gewone tekst.
    text = re.sub(r"\[`?([^\]`]+)`?\]\(([^)]+\.md)\)", r"`\1`", text)
    body = markdown.markdown(text, extensions=["tables"])
    html = (
        "<html><head><meta charset='utf-8'><style>" + CSS + "</style></head><body>"
        "<div id='footer'>OpenQuatt FE · handleiding ombouw · pagina <pdf:pagenumber/></div>"
        + body + "</body></html>"
    )
    with TARGET.open("wb") as handle:
        result = pisa.CreatePDF(html, dest=handle, link_callback=link_callback, encoding="utf-8")
    if result.err:
        print(f"PDF-fouten: {result.err}", file=sys.stderr)
        return 1
    print(f"Geschreven: {TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
