#!/usr/bin/env python3
"""Draws the project's diagrams, as SVG and PNG. Needs PyMuPDF.

    python doc/arquitectura/build.py [path/to/migrated.db]

What can be read from the code is read from it: the tables from a database
(any SQLite file the program has migrated; without one that drawing is left as
it is), and who implements what from the module headers. A label that doesn't
fit its box stops the build.
"""
import html
import re
import sqlite3
import sys
from pathlib import Path

import pymupdf

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

INK = '#1F2933'
MUTED = '#52606D'
LINE = '#7B8794'
BORDER = '#CBD2D9'
SURFACE = '#F5F7FA'
WHITE = '#FFFFFF'
ACCENT = '#B84A0A'
SOFT = '#FDF0E7'
YELLOW = '#FFC400'
FONT = 'Arial, Helvetica, sans-serif'


def text_width(text, size, bold=False):
    return pymupdf.get_text_length(text, fontname='hebo' if bold else 'helv', fontsize=size)


class Sheet:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.items = []

    def rect(self, x, y, w, h, fill=WHITE, stroke=BORDER, r=6, width=1.2):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" ry="{r}" fill="{fill}" '
                          f'stroke="{stroke}" stroke-width="{width}"/>')

    def text(self, x, y, value, size=13, bold=False, color=INK, anchor='start', italic=False):
        weight = ' font-weight="bold"' if bold else ''
        slant = ' font-style="italic"' if italic else ''
        self.items.append(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}"{weight}{slant} '
                          f'fill="{color}" text-anchor="{anchor}">{html.escape(value)}</text>')

    def circle(self, x, y, r, fill):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"/>')

    def line(self, points, color=LINE, dash=False, arrow=True, width=1.4, open_head=False):
        if dash:
            for (x0, y0), (x1, y1) in zip(points, points[1:]):
                length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
                for start in range(0, int(length), 10):
                    a, b = start / length, min(start + 6, length) / length
                    self.items.append(f'<line x1="{x0 + (x1 - x0) * a}" y1="{y0 + (y1 - y0) * a}" '
                                      f'x2="{x0 + (x1 - x0) * b}" y2="{y0 + (y1 - y0) * b}" '
                                      f'stroke="{color}" stroke-width="{width}"/>')
        else:
            path = ' '.join(f'{x},{y}' for x, y in points)
            self.items.append(f'<polyline points="{path}" fill="none" stroke="{color}" stroke-width="{width}"/>')
        if arrow:
            (x0, y0), (x1, y1) = points[-2], points[-1]
            length = max(((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5, 0.01)
            ux, uy = (x1 - x0) / length, (y1 - y0) / length
            a = (x1 - 9 * ux + 4.5 * uy, y1 - 9 * uy - 4.5 * ux)
            b = (x1 - 9 * ux - 4.5 * uy, y1 - 9 * uy + 4.5 * ux)
            fill = WHITE if open_head else color
            self.items.append(f'<polygon points="{a[0]},{a[1]} {x1},{y1} {b[0]},{b[1]}" fill="{fill}" '
                              f'stroke="{color}" stroke-width="1.2"/>')

    def box(self, x, y, w, h, title, lines=(), fill=WHITE, stroke=BORDER, title_color=INK, size=13):
        """A titled box: its text, centered, has to fit."""
        self.rect(x, y, w, h, fill, stroke)
        total = size + 4 + len(lines) * 16
        top = y + (h - total) / 2 + size
        for value, s, bold in [(title, size, True)] + [(v, 11, False) for v in lines]:
            if text_width(value, s, bold) > w - 10:
                raise SystemExit(f'"{value}" does not fit in a box {w} wide')
        self.text(x + w / 2, top, title, size, True, title_color, 'middle')
        for i, value in enumerate(lines):
            self.text(x + w / 2, top + 18 + i * 16, value, 11, False, MUTED, 'middle')

    def label(self, x, y, value, size=11, anchor='middle', color=MUTED):
        """Text on a white patch, so a line can pass behind it."""
        w = text_width(value, size) + 8
        left = x - w / 2 if anchor == 'middle' else x - 4 if anchor == 'start' else x - w + 4
        self.items.append(f'<rect x="{left}" y="{y - size}" width="{w}" height="{size + 5}" fill="{WHITE}"/>')
        self.text(x, y, value, size, False, color, anchor)

    def title(self, name, note):
        self.text(40, 42, name, 24, True, ACCENT)
        self.text(40, 64, note, 13, False, MUTED)

    def save(self, path):
        body = '\n'.join(self.items)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" height="{self.height}" '
               f'viewBox="0 0 {self.width} {self.height}">\n'
               f'<rect width="{self.width}" height="{self.height}" fill="{WHITE}"/>\n{body}\n</svg>\n')
        path.with_suffix('.svg').write_text(svg, encoding='utf-8')
        document = pymupdf.open(stream=svg.encode('utf-8'), filetype='svg')
        document[0].get_pixmap(dpi=144).save(str(path.with_suffix('.png')))
        document.close()
        print(path.with_suffix('.svg').relative_to(ROOT))


def api_level():
    return re.search(r'AGUARA_API_VERSION "(\d+)"', (ROOT / 'src/interfaces/apiversion.h').read_text()).group(1)


# ---------------------------------------------------------------- the system
def general():
    s = Sheet(1400, 900)
    s.title('Estructura general', 'Qué corre en cada caja y con qué habla.')

    s.rect(40, 90, 900, 770, SURFACE)
    s.text(60, 118, 'Cada caja (Windows 7 SP1, 32 bits)', 15, True)

    s.box(70, 135, 840, 110, 'aguara.exe', ['src/app + src/core, una biblioteca estática',
                                            'Ventana principal, servicios, acceso a datos y carga de módulos'],
          fill=SOFT, stroke=ACCENT, title_color=ACCENT)
    s.box(70, 300, 840, 90, 'aguara_interfaces.dll',
          [f'src/interfaces: interfaces, tipos de datos y estilo, compartidos. Nivel de API {api_level()}'])
    s.line([(490, 245), (490, 300)])
    s.label(490, 276, 'usa')

    s.rect(70, 445, 840, 165, WHITE)
    s.text(90, 470, 'modules\\  (un plugin modulo_*.dll por módulo)', 13, True)
    names = [('Inventario', 'control_inventario'), ('Clientes', 'customers'), ('Importar', 'importar'),
             ('Ventas', 'sales_module'), ('Reportes', 'stats'), ('Usuarios', 'usuarios')]
    for i, (name, file) in enumerate(names):
        s.box(90 + i * 135, 485, 125, 70, name, [file], fill=SURFACE)
    s.text(90, 588, 'Un módulo no usa SQL: pide servicios al core en initialize().', 12, False, MUTED)
    s.line([(490, 445), (490, 390)])
    s.label(490, 421, 'usan')

    # the core loads them: around the DLL, on the left
    s.line([(70, 190), (52, 190), (52, 527), (88, 527)], dash=True)
    s.items.append(f'<text x="47" y="360" font-family="{FONT}" font-size="11" fill="{MUTED}" text-anchor="middle" '
                   f'transform="rotate(-90 47 360)">carga los de su nivel de API</text>')

    s.rect(70, 645, 840, 190, WHITE)
    s.text(90, 670, 'En %APPDATA%\\Aguara\\AguaraPOS', 13, True)
    files = [('aguara.ini', ['conexión e impresora', 'contraseña cifrada']),
             ('ventas-pendientes-aguara.db', ['cola: cada venta se guarda', 'aquí antes de subir']),
             ('aguara.db', ['datos, solo con', 'un puesto (SQLite)']),
             ('logs\\aguara.log', ['detalle técnico', 'de los errores'])]
    for i, (name, lines) in enumerate(files):
        s.box(90 + i * 202, 685, 192, 90, name, lines, fill=SURFACE, size=12)
    s.text(90, 805, 'La cola sigue funcionando sin servidor: la venta en curso se cobra y sube al volver la conexión.',
           12, False, MUTED)

    s.box(1040, 135, 320, 110, 'PostgreSQL 10.23', ['en una de las cajas, con IP fija', 'base de datos aguara'],
          fill=SOFT, stroke=ACCENT, title_color=ACCENT)
    s.line([(910, 175), (1040, 175)])
    s.line([(1040, 205), (910, 205)])
    s.label(975, 169, 'TCP 5432')
    s.label(975, 224, 'NOTIFY')

    s.box(1040, 330, 320, 80, 'Las demás cajas', ['AguaraPOS, igual que esta'])
    s.line([(1200, 330), (1200, 245)])
    s.label(1200, 292, 'misma base, misma red')

    s.box(1040, 500, 320, 90, 'Impresora y cajón', ['ESC/POS o impresora común', 'por la cola de Windows'])
    s.line([(940, 545), (1040, 545)])

    s.box(1040, 680, 320, 110, 'Terminal de tarjetas y banco', ['fuera del sistema: el cajero', 'copia el código del comprobante'],
          fill=SURFACE)
    s.line([(1040, 735), (940, 735)], dash=True)
    s.save(HERE / 'estructura-general')


# ---------------------------------------------------------------- the core
def core():
    s = Sheet(1300, 930)
    s.title('Estructura del core', 'src/core por capas: cada una usa la de abajo.')

    def band(y, h, name, boxes, per_row=None, foot=0):
        s.rect(40, y, 1220, h, SURFACE)
        s.text(60, y + 24, name, 14, True)
        per_row = per_row or len(boxes)
        rows = [boxes[i:i + per_row] for i in range(0, len(boxes), per_row)]
        each = (h - 40 - foot - 10 * (len(rows) - 1)) / len(rows)
        for r, row in enumerate(rows):
            w = (1180 - 10 * (len(row) - 1)) / len(row)
            for i, (title, *lines) in enumerate(row):
                s.box(60 + i * (w + 10), y + 34 + r * (each + 10), w, each, title, lines, size=12)

    band(85, 120, 'Interfaz', [
        ('MainWindow', 'menús, sesión, páginas', 'carga los módulos'),
        ('PageCero', 'la página Avisos'),
        ('NotificationDialog', 'cada aviso'),
        ('sessiondialogs', 'ingreso, bloqueo,', 'supervisor'),
        ('Preferencias', 'conexión e impresora'),
        ('ModuleDialog', 'módulos instalados')])
    band(235, 120, 'Para los módulos', [
        ('CoreHost', 'implementa AguaraHost: registro de servicios y avisos'),
        ('LowStockWatcher', 'aviso de stock bajo al cambiar el stock')])
    band(385, 210, 'Servicios', [
        ('SqlProductService', 'artículos, marcas, rubros'),
        ('SqlInventoryService', 'compras, recuentos, proveedores'),
        ('SqlUserService', 'usuarios, permisos, auditoría'),
        ('SqlCashService', 'cajas, sesiones, arqueo'),
        ('SqlSalesService', 'cobro y cola de ventas'),
        ('SqlPrintService', 'tickets, cajón, PDF'),
        ('SqlCustomerService', 'clientes'),
        ('SqlReturnService', 'devoluciones, notas de crédito'),
        ('SqlReportService', 'reportes, CSV y PDF')], per_row=5, foot=26)
    s.text(60, 584, 'Sales usa Product y Cash. Return y Print usan Sales y Cash. Inventory y Report usan Product.',
           12, False, MUTED)
    band(615, 105, 'Piezas de los servicios', [
        ('StockLedger', 'único que cambia productos.stock'),
        ('SaleOutbox', 'la cola local, en SQLite'),
        ('EscPos', 'arma los tickets'),
        ('PasswordHash', 'PBKDF2-SHA256')])
    band(750, 105, 'Datos', [
        ('ConnectionMonitor', 'detecta cortes y reconecta'),
        ('Database', 'abre la base y aplica migraciones'),
        ('Backup', 'copia de seguridad diaria')])
    s.text(40, 885, 'En todas las capas: logging (registro), errors (mensajes para el usuario) e InactivityWatcher '
                    '(bloqueo por inactividad).', 12, False, MUTED)
    for y0, y1 in ((205, 235), (355, 385), (595, 615), (720, 750)):
        s.line([(650, y0), (650, y1)], color=ACCENT)
    s.save(HERE / 'estructura-del-core')


# ---------------------------------------------------------------- module interfaces
def card(s, x, y, w, name, kind, rows, accent=False):
    """A class: its name, and one row per member."""
    h = 48 + len(rows) * 20 + 6
    s.rect(x, y, w, h, WHITE, ACCENT if accent else BORDER)
    s.text(x + w / 2, y + 17, kind, 10, False, MUTED, 'middle', italic=True)
    s.text(x + w / 2, y + 35, name, 13, True, ACCENT if accent else INK, 'middle')
    s.items.append(f'<line x1="{x}" y1="{y + 44}" x2="{x + w}" y2="{y + 44}" stroke="{BORDER}"/>')
    for i, row in enumerate(rows):
        if text_width(row, 12) > w - 16:
            raise SystemExit(f'"{row}" does not fit in {name}')
        s.text(x + 10, y + 62 + i * 20, row, 12)
    return h


def module_capabilities():
    """Which interfaces each module's root class declares."""
    names = {'customers': 'Clientes', 'import': 'Importar', 'inventory': 'Inventario', 'reports': 'Reportes',
             'sales': 'Ventas', 'users': 'Usuarios'}
    found = {}
    for header in sorted((ROOT / 'src/modules').glob('*/*.h')):
        text = header.read_text(encoding='utf-8', errors='replace')
        declared = re.search(r'Q_INTERFACES\(([^)]*)\)', text)
        if declared:
            key = next(k for k in names if k in header.stem.lower()
                       or (k == 'sales' and 'sales' in header.stem.lower()))
            found[names[key]] = (re.search(r'class (\w+)\s*:', text).group(1), declared.group(1).split())
    return dict(sorted(found.items()))


def interfaces():
    s = Sheet(1400, 980)
    s.title('Interfaces de módulos', 'src/interfaces/interfaces.h: un módulo implementa AguaraModuleInterface más sus capacidades.')

    s.rect(40, 90, 1320, 435, SURFACE)
    s.text(60, 116, 'aguara_interfaces.dll', 14, True)

    card(s, 70, 135, 300, 'AguaraModuleInterface', 'obligatoria',
         ['moduleInfo(): ModuleInfo', 'initialize(host): bool'], accent=True)
    card(s, 440, 135, 300, 'ModuleInfo', 'datos',
         ['id, name, description', 'version, icon, iconName'])
    card(s, 810, 135, 300, 'AguaraHost', 'lo que el core ofrece',
         ['service<T>(): T*', 'serviceObject(iid): QObject*', 'notify(msg)'])
    s.line([(370, 165), (440, 165)])
    s.label(405, 158, 'describe')
    s.line([(370, 195), (400, 195), (400, 268), (960, 268), (960, 249)])
    s.label(560, 272, 'la recibe en initialize()')

    s.text(70, 304, 'Capacidades: un módulo implementa las que necesita', 13, True)
    y = 315
    w = 243
    caps = [('WidgetModuleInterface', ['createWidget(parent)', 'allowsMultipleInstances()', 'keepPageOpen()']),
            ('ImportArticlesInterface', ['fileTypes()', 'importFrom(fileName)']),
            ('PermissionProvider', ['permissions()']),
            ('ReportProvider', ['reports()', 'runReport(id, filter, ...)'])]
    for i, (name, rows) in enumerate(caps):
        card(s, 70 + i * (w + 11), y, w, name, 'capacidad', rows)
    card(s, 1090, 315, 240, 'AguaraWidget', 'base opcional de una página (QWidget)',
         ['notify(msg)', 'keepOpenChanged()'])
    s.text(70, 465, f'Cada interfaz tiene un ID que termina en el nivel de API: org.AguaraPOS.ModuleInterface/{api_level()}.',
           12, False, MUTED)
    s.text(70, 485, 'El core lee ese ID de los metadatos del plugin: con otro nivel lo salta, sin cargar su código.',
           12, False, MUTED)
    s.text(70, 505, 'Servicios por service<T>(): Product, Inventory, User, Cash, Sales, Print, Customer, Return y Report.',
           12, False, MUTED)

    s.rect(40, 545, 1320, 95, WHITE, ACCENT)
    s.text(60, 571, 'En el core', 14, True, ACCENT)
    s.text(60, 595, 'CoreHost implementa AguaraHost y lleva los avisos a la ventana. MainWindow::loadPlugins() revisa el nivel de API,',
           13)
    s.text(60, 617, 'carga cada DLL, llama a initialize() y pide las capacidades con qobject_cast.', 13)

    s.text(40, 675, 'Quién implementa qué', 16, True)
    columns = [('Widget', 'WidgetModuleInterface'), ('Importar', 'ImportArticlesInterface'),
               ('Permisos', 'PermissionProvider'), ('Reportes', 'ReportProvider')]
    capabilities = module_capabilities()
    top = 693
    s.rect(40, top, 1320, 32, INK, INK, r=0)
    s.text(60, top + 21, 'Módulo', 13, True, WHITE)
    s.text(240, top + 21, 'Clase raíz', 13, True, WHITE)
    for c, (title, _) in enumerate(columns):
        s.text(640 + c * 180, top + 21, title, 13, True, WHITE, 'middle')
    for r, (module, (root_class, declared)) in enumerate(capabilities.items()):
        y = top + 32 + r * 34
        s.rect(40, y, 1320, 34, WHITE if r % 2 == 0 else SURFACE, BORDER, r=0, width=0.6)
        s.text(60, y + 22, module, 13, True)
        s.text(240, y + 22, root_class, 13)
        for c, (_, interface) in enumerate(columns):
            if interface in declared:
                s.circle(640 + c * 180, y + 17, 6, ACCENT)
    s.text(40, top + 32 + len(capabilities) * 34 + 26, 'ReportProvider queda para módulos futuros: '
           'los reportes de hoy los da el core y el módulo Reportes solo los muestra.', 12, False, MUTED)
    s.save(HERE / 'interfaces-de-modulos')


# ---------------------------------------------------------------- database
GROUPS = [
    ('Usuarios y auditoría', [['usuarios', 'roles', 'permisos', 'auditoria'],
                              ['usuario_tiene_roles', 'rol_tiene_permisos']]),
    ('Artículos e inventario', [['marcas', 'rubros', 'iva_cat', 'unidades_basicas', 'proveedores'],
                                ['productos', 'compras'],
                                ['codigos_barras', 'precios_historial', 'stock_movimientos', 'compra_items']]),
    ('Caja, ventas y devoluciones', [['cajas', 'clientes', 'modos_de_pago'],
                                     ['sesiones_caja', 'ventas_espera'],
                                     ['sesion_conteos', 'caja_movimientos', 'ventas'],
                                     ['ventas_items', 'ventas_pagos', 'devoluciones'],
                                     ['devoluciones_items', 'notas_credito']]),
    ('Sistema', [['configuracion', 'schema_migrations']]),
]


def read_schema(path):
    db = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    tables = {}
    for (name,) in db.execute("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"):
        keys = {k[3]: k[2] for k in db.execute(f'PRAGMA foreign_key_list("{name}")')}
        tables[name] = [(c[1], bool(c[5]), keys.get(c[1])) for c in db.execute(f'PRAGMA table_info("{name}")')]
    version = db.execute('SELECT MAX(version) FROM schema_migrations').fetchone()[0]
    return tables, version


def database(path):
    tables, version = read_schema(path)
    placed = [t for _, tiers in GROUPS for tier in tiers for t in tier]
    if sorted(placed) != sorted(tables):
        raise SystemExit(f'Add to GROUPS, or take out: {sorted(set(placed) ^ set(tables))}')

    size = 12
    row = 18

    def table_size(name):
        def row_width(column, primary, foreign):
            # the helv metrics have no arrow: count it as a capital
            arrow = text_width(f'W {foreign}', size) + 6 if foreign else 0
            return text_width(column, size, primary) + arrow + 38
        widest = max(row_width(*column) for column in tables[name])
        return max(widest, text_width(name, size + 1, True) + 24), 32 + len(tables[name]) * row + 8

    gutter, panel_pad = 26, 22

    def layout(group, tiers, left, top):
        """Place a group's tables in rows, from `top`. Returns what's needed to draw it."""
        y = top + 42
        boxes = {}
        right = left
        for t, tier in enumerate(tiers):
            x = left + panel_pad
            tallest = 0
            for name in tier:
                w, h = table_size(name)
                boxes[name] = (x, y, w, h)
                x += w + gutter
                tallest = max(tallest, h)
            right = max(right, x)
            lower = tiers[t + 1] if t + 1 < len(tiers) else []
            links = sum(1 for n in lower for _, _, f in tables[n] if f in tier)
            y += tallest + 34 + links * 9
        return dict(group=group, tiers=tiers, boxes=boxes, left=left, top=top, right=right - gutter + panel_pad,
                    bottom=y - 10)

    panels = []
    top = 90
    for group, tiers in GROUPS[:2] + GROUPS[3:]:
        panels.append(layout(group, tiers, 40, top))
        top = panels[-1]['bottom'] + 20
    split = max(p['right'] for p in panels)
    for p in panels:
        p['right'] = split
    group, tiers = GROUPS[2]
    panels.append(layout(group, tiers, split + 20, 90))
    bottom = max(p['bottom'] for p in panels)
    panels[-1]['bottom'] = bottom
    panels[len(panels) - 2]['bottom'] = bottom

    s = Sheet(int(panels[-1]['right'] + 40), int(bottom + 60))
    s.title('Base de datos', f'Las {len(tables)} tablas de la migración {version}. Las tablas previstas para la fase 7 '
                             'siguen en test_database.graphml.')
    for panel in panels:
        tiers, boxes, left, top = panel['tiers'], panel['boxes'], panel['left'], panel['top']
        s.rect(left, top, panel['right'] - left, panel['bottom'] - top, SURFACE)
        s.text(left + 20, top + 28, panel['group'], 15, True, ACCENT)

        # links only between one tier and the next: the gap between them is free
        for t in range(len(tiers) - 1):
            parents = tiers[t]
            lane = 0
            floor = max(boxes[p][1] + boxes[p][3] for p in parents)
            for child in tiers[t + 1]:
                cx, cy, cw, _ = boxes[child]
                links = [f for _, _, f in tables[child] if f in parents]
                for i, parent in enumerate(links):
                    px, py, pw, ph = boxes[parent]
                    lane += 1
                    channel = floor + 8 + lane * 9
                    start = cx + cw * (i + 1) / (len(links) + 1)
                    target = px + pw / 2 + (lane % 5 - 2) * 8
                    s.line([(start, cy), (start, channel), (target, channel), (target, py + ph)], width=1.1)

        for name, (x, y, w, h) in boxes.items():
            s.rect(x, y, w, h, WHITE, BORDER, r=4)
            s.items.append(f'<path d="M{x} {y + 26} V{y + 4} a4 4 0 0 1 4 -4 H{x + w - 4} a4 4 0 0 1 4 4 V{y + 26} Z" '
                           f'fill="{INK}"/>')
            s.text(x + 10, y + 18, name, size + 1, True, WHITE)
            for i, (column, primary, foreign) in enumerate(tables[name]):
                line = y + 45 + i * row
                if primary:
                    s.text(x + 8, line, 'PK', 8, True, ACCENT)
                s.text(x + 26, line, column, size, primary)
                if foreign:
                    gap = text_width(column, size, primary) + 6
                    s.text(x + 26 + gap, line, f'→ {foreign}', size, False, ACCENT)
    s.text(40, s.height - 24, 'Las flechas unen tablas de filas contiguas. Las demás relaciones figuran en la columna '
                              '(→ tabla). Solo se muestran las claves foráneas declaradas.', 12, False, MUTED)
    s.save(HERE / 'db' / 'base-de-datos')


# ---------------------------------------------------------------- the main window
def screen():
    s = Sheet(1300, 780)
    s.title('Pantalla principal', 'Lo que el core dibuja, con la caja abierta, y lo que aporta cada módulo.')
    left, top, w, h = 40, 90, 900, 650
    s.rect(left, top, w, h, WHITE, INK, r=4, width=1.6)
    s.rect(left, top, w, 30, SURFACE, BORDER, r=0)
    menu = left + 16
    for name in ('Archivo', 'Sesión', 'Preferencias', 'Ayuda'):
        s.text(menu, top + 20, name, 12)
        menu += text_width(name, 12) + 26

    side = 190
    s.rect(left, top + 30, side, h - 60, INK, INK, r=0)
    s.text(left + 20, top + 62, 'AguaraPOS', 16, True, WHITE)
    s.text(left + 20, top + 80, 'Punto de venta', 11, False, BORDER)
    s.text(left + 20, top + 116, 'MÓDULOS', 11, True, BORDER)
    for i, name in enumerate(('Inventario', 'Clientes', 'Ventas', 'Reportes', 'Usuarios')):
        y = top + 130 + i * 34
        if name == 'Ventas':
            s.rect(left, y, side, 32, '#3E4C59', '#3E4C59', r=0)
            s.rect(left, y, 3, 32, '#F08A3E', '#F08A3E', r=0)
        s.text(left + 24, y + 21, name, 13, name == 'Ventas', WHITE)
    s.text(left + 44, top + h - 112, 'Avisos', 13, False, WHITE)
    s.items.append(f'<circle cx="{left + 30}" cy="{top + h - 117}" r="8" fill="none" stroke="{WHITE}" stroke-width="1.5"/>')
    s.circle(left + 36, top + h - 124, 4, YELLOW)
    s.text(left + 24, top + h - 62, 'Usuario y rol', 12, True, WHITE)

    x = left + side
    s.rect(x, top + 30, w - side, 40, WHITE, BORDER, r=0)
    s.text(x + 24, top + 56, 'Ventas', 13, True, ACCENT)
    s.items.append(f'<line x1="{x + 10}" y1="{top + 69}" x2="{x + 90}" y2="{top + 69}" stroke="{ACCENT}" stroke-width="2"/>')
    s.text(x + 110, top + 56, 'Avisos  ×', 13, False, MUTED)
    s.text(x + 200, top + 56, 'Inventario  ×', 13, False, MUTED)

    s.rect(x + 20, top + 90, w - side - 40, 500, SOFT, ACCENT, r=4)
    s.text(x + 44, top + 128, 'Página del módulo', 18, True, ACCENT)
    s.text(x + 44, top + 150, 'createWidget() del módulo activo. Cambia con cada pestaña.', 13, False, MUTED)
    s.rect(x + 44, top + 170, w - side - 88, 34, WHITE, BORDER)
    s.text(x + 56, top + 192, 'Título, subtítulo y acciones', 12, False, MUTED)
    s.rect(x + 44, top + 214, w - side - 88, 350, WHITE, BORDER)
    s.text(x + 56, top + 236, 'Contenido: tabla, formulario o la venta en curso', 12, False, MUTED)

    s.rect(left, top + h - 30, w, 30, SURFACE, BORDER, r=0)
    s.text(left + 16, top + h - 10, 'Origen de los datos y conexión', 12)
    s.text(left + w - 48, top + h - 10, 'Usuario, fecha y hora', 12, False, INK, 'end')

    notes = [(left + w - 24, top + 15, 'Menús', ['Ayuda > Módulos instalados está aquí.',
                                                 'Sesión aparece al iniciar sesión.']),
             (left + w - 24, top + 50, 'Pestañas', ['Ventas, con la caja abierta, va primera y sin X.',
                                                    'Avisos solo se cierra mientras una página',
                                                    'deba quedar abierta.']),
             (left + side - 24, top + 145, 'Barra lateral', ['Solo los módulos que el usuario puede abrir.',
                                                             'Ctrl+1 a Ctrl+9 los abren.']),
             (left + side - 24, top + h - 117, 'Avisos', ['F1 los abre. El punto amarillo indica',
                                                          'que hay avisos sin ver.']),
             (left + side - 24, top + h - 66, 'Usuario', ['El candado bloquea la sesión (Ctrl+L).']),
             (left + w - 24, top + 128, 'Página del módulo', ['La aporta el módulo; el resto es del core.']),
             (left + w - 24, top + h - 15, 'Barra de estado', ['Origen de los datos, avisos de conexión,',
                                                               'usuario, fecha y hora.'])]
    y = top + 6
    for number, (mx, my, name, lines) in enumerate(notes, 1):
        s.circle(mx, my, 10, ACCENT)
        s.text(mx, my + 4, str(number), 11, True, WHITE, 'middle')
        s.circle(1002, y + 8, 10, ACCENT)
        s.text(1002, y + 12, str(number), 11, True, WHITE, 'middle')
        s.text(1020, y + 13, name, 13, True)
        for i, value in enumerate(lines):
            s.text(1020, y + 31 + i * 17, value, 11.5, False, MUTED)
        y += 36 + len(lines) * 17 + 22
    s.save(ROOT / 'doc' / 'gui' / 'pantalla-principal')


if __name__ == '__main__':
    general()
    core()
    interfaces()
    screen()
    if len(sys.argv) > 1:
        database(sys.argv[1])
    else:
        print('No database given: base-de-datos left as it is.')
