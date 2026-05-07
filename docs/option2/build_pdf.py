#!/usr/bin/env python3
"""Build option2 PDF: markdown -> HTML (with embedded mermaid PNG) -> PDF."""
import base64
import re
from pathlib import Path

import markdown
from weasyprint import HTML, CSS

DOC_DIR = Path("/home/saman/agentic-synth/docs")
OUT_DIR = DOC_DIR / "option2"
MD_PATH = DOC_DIR / "architecture-option2.md"
PNG_PATH = OUT_DIR / "diagram.png"
HTML_PATH = OUT_DIR / "architecture-option2.html"
PDF_PATH = DOC_DIR / "agentic-synth-architecture-option2.pdf"

md_text = MD_PATH.read_text()

# Replace ```mermaid ... ``` block with image
png_b64 = base64.b64encode(PNG_PATH.read_bytes()).decode()
img_html = (
    f'<div class="diagram">'
    f'<img src="data:image/png;base64,{png_b64}" alt="System Architecture Diagram"/>'
    f'</div>'
)

mermaid_pattern = re.compile(r"```mermaid\n.*?\n```", re.DOTALL)
md_text_no_mermaid = mermaid_pattern.sub("__MERMAID_DIAGRAM__", md_text)

html_body = markdown.markdown(
    md_text_no_mermaid,
    extensions=["tables", "fenced_code", "toc", "sane_lists"],
)
html_body = html_body.replace("<p>__MERMAID_DIAGRAM__</p>", img_html)

css = """
@page {
  size: Letter;
  margin: 0.75in 0.75in 0.85in 0.75in;
  @bottom-center {
    content: "Agentic Synth — Architecture Option 2  •  Page " counter(page) " of " counter(pages);
    font-family: 'Inter', sans-serif;
    font-size: 9pt;
    color: #607d8b;
  }
}
html { font-family: 'Inter', 'Helvetica Neue', Arial, sans-serif; color: #0d1b2a; }
body { font-size: 10.5pt; line-height: 1.55; }
h1 {
  font-size: 22pt; color: #0b3d91; border-bottom: 3px solid #1565c0;
  padding-bottom: 0.25em; margin-top: 0; margin-bottom: 0.6em;
  page-break-after: avoid;
}
h2 {
  font-size: 15pt; color: #0b3d91; margin-top: 1.4em;
  border-bottom: 1px solid #b0bec5; padding-bottom: 0.15em;
  page-break-after: avoid; page-break-before: auto;
}
h3 {
  font-size: 12.5pt; color: #1565c0; margin-top: 1.1em;
  page-break-after: avoid;
}
h4 { font-size: 11pt; color: #263238; margin-top: 0.9em; page-break-after: avoid; }
p { margin: 0.45em 0; }
strong { color: #0b3d91; }
code {
  font-family: 'JetBrains Mono', 'Menlo', monospace;
  background: #eceff1; color: #0d47a1;
  padding: 1px 5px; border-radius: 3px; font-size: 9.5pt;
}
pre {
  background: #0d1b2a; color: #e0f2f1;
  padding: 12px 14px; border-radius: 6px;
  font-family: 'JetBrains Mono', 'Menlo', monospace;
  font-size: 8.8pt; line-height: 1.45;
  overflow-x: auto; page-break-inside: avoid;
  border-left: 4px solid #1565c0;
}
pre code { background: transparent; color: inherit; padding: 0; }
table {
  border-collapse: collapse; width: 100%;
  margin: 0.8em 0; font-size: 9.5pt;
  page-break-inside: avoid;
}
th {
  background: #1565c0; color: white; padding: 8px 10px;
  text-align: left; font-weight: 600; border: 1px solid #0b3d91;
}
td { padding: 7px 10px; border: 1px solid #cfd8dc; vertical-align: top; }
tr:nth-child(even) td { background: #f5f7fa; }
ul, ol { margin: 0.4em 0 0.6em 1.4em; padding: 0; }
li { margin: 0.18em 0; }
hr { border: 0; border-top: 1px solid #cfd8dc; margin: 1.4em 0; }
blockquote {
  border-left: 4px solid #1565c0; background: #f1f8ff;
  margin: 0.6em 0; padding: 0.4em 0.9em; color: #263238;
}
.diagram {
  text-align: center; margin: 1.2em 0;
  page-break-inside: avoid; page-break-before: auto;
}
.diagram img {
  max-width: 100%; max-height: 9in;
  border: 1px solid #cfd8dc; border-radius: 4px;
  background: white; padding: 8px;
}
"""

html_full = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Agentic Synth — Architecture Option 2</title>
<style>{css}</style>
</head>
<body>
{html_body}
</body>
</html>
"""

HTML_PATH.write_text(html_full)
print(f"HTML: {HTML_PATH}")

HTML(string=html_full, base_url=str(OUT_DIR)).write_pdf(str(PDF_PATH))
print(f"PDF:  {PDF_PATH}")
