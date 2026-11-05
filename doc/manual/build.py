# Builds the manuals' PDFs from their HTML. Needs PyMuPDF (pip install pymupdf).
#
#   python doc/manual/build.py
import os
import pymupdf

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = pymupdf.paper_rect('a4')
FRAME = PAGE + (54, 50, -54, -56)

CSS = """
* { font-family: sans-serif; }
body { font-size: 10pt; line-height: 1.35; color: #1f2933; }
h1 { font-size: 26pt; color: #b84a0a; margin: 0 0 2pt 0; }
p.sub { font-size: 13pt; color: #52606d; margin: 0 0 14pt 0; }
h2 { font-size: 14pt; color: #b84a0a; margin: 16pt 0 5pt 0; }
h3 { font-size: 11pt; color: #1f2933; margin: 11pt 0 3pt 0; }
p { margin: 0 0 6pt 0; }
ul, ol { margin: 0 0 6pt 0; }
li { margin: 0 0 2pt 0; }
img { display: block; margin: 5pt 0 8pt 0; }
table { margin: 3pt 0 9pt 0; border-collapse: collapse; }
th { text-align: left; padding: 3pt 6pt; color: #b84a0a; border-bottom: 1px solid #b84a0a; }
td { padding: 3pt 6pt; border-bottom: 1px solid #d9e2ec; }
p.note { border-left: 3px solid #b84a0a; padding: 2pt 0 2pt 8pt; }
.page { page-break-before: always; }
"""


def build(name, title):
    with open(os.path.join(HERE, name + '.html'), encoding='utf-8') as f:
        html = f.read()
    story = pymupdf.Story(html=html, user_css=CSS, archive=pymupdf.Archive(HERE))
    target = os.path.join(HERE, name + '.pdf')
    raw = target + '.raw'
    writer = pymupdf.DocumentWriter(raw)
    more = 1
    while more:
        device = writer.begin_page(PAGE)
        more, _ = story.place(FRAME)
        story.draw(device)
        writer.end_page()
    writer.close()
    del writer, device, story           # on Windows they hold the file

    with open(raw, 'rb') as f:
        document = pymupdf.open(stream=f.read(), filetype='pdf')
    os.remove(raw)
    for number, page in enumerate(document, 1):
        page.insert_text((54, PAGE.height - 30), title, fontsize=8, color=(0.45, 0.5, 0.55))
        page.insert_text((PAGE.width - 80, PAGE.height - 30), 'Página %d de %d' % (number, len(document)),
                         fontsize=8, color=(0.45, 0.5, 0.55))
    document.set_metadata({'title': title, 'author': 'AguaraPOS'})
    document.save(target, garbage=4, deflate=True, deflate_images=True, deflate_fonts=True)
    document.close()
    return target


if __name__ == '__main__':
    print(build('manual-cajero', 'AguaraPOS - Manual del cajero'))
    print(build('manual-encargado', 'AguaraPOS - Manual del encargado'))
