#!/usr/bin/env python3
"""Builds a SQLite database of a busy shop, to stress the reports on a test machine.

    python scripts/make-test-db.py TEMPLATE.db OUTPUT.db [--days 730] [--tickets 220] [--seed 7]

TEMPLATE.db is an empty database already migrated by the program (open a new
one with AguaraPOS and close it). Its schema is copied as it is, so nothing here
can drift from the migrations. The same seed always gives the same shop.

It replays the shop day by day, with the same arithmetic as the program: VAT
inside the price, the sale's discount shared among its lines, cash rounded to
50, stock as a ledger, cash sessions that add up. Then it checks all of that
with SQL. FOR TESTING ONLY: every account has a known password.
"""
import argparse
import base64
import bisect
import datetime as dt
import hashlib
import heapq
import os
import random
import shutil
import sqlite3
import sys
import uuid

PASSWORD = 'prueba123'
ONE = 1000                      # a Quantity is in thousandths
CASH_STEP = 50
NOTES = (100000, 50000, 20000, 10000, 5000, 2000, 1000, 500, 100, 50)

STORE = (('comercio.nombre', 'SUPERMERCADO DE PRUEBA'), ('comercio.ruc', '80009735-1'),
         ('comercio.direccion', 'Avda. de Prueba 1234, Asunción'), ('comercio.telefono', '021 555 123'),
         ('comercio.pie', 'Base de datos de prueba'))

# Category -> subcategory -> (article names, VAT rate, unit id, base price)
# Accents on purpose: the searches and the reports have to cope with them.
CATALOG = {
    'ALMACÉN': {
        'Arroz y fideos': (['ARROZ TIPO 1', 'ARROZ PARBOIL', 'FIDEO TALLARÍN', 'FIDEO SPAGHETTI', 'FIDEO CODITO'], 5, 1, 9000),
        'Harinas y azúcar': (['HARINA 000', 'HARINA 0000', 'AZÚCAR BLANCA', 'AZÚCAR RUBIA', 'SAL FINA'], 5, 1, 7500),
        'Aceites': (['ACEITE DE GIRASOL', 'ACEITE DE SOJA', 'ACEITE DE OLIVA', 'VINAGRE DE ALCOHOL'], 10, 1, 21000),
        'Conservas': (['ARVEJAS EN LATA', 'CHOCLO EN LATA', 'TOMATE PERITA', 'ATÚN AL NATURAL', 'SARDINA EN ACEITE'], 10, 1, 12500),
    },
    'BEBIDAS': {
        'Gaseosas': (['GASEOSA COLA', 'GASEOSA NARANJA', 'GASEOSA LIMA LIMÓN', 'AGUA TÓNICA'], 10, 1, 9500),
        'Aguas y jugos': (['AGUA MINERAL', 'AGUA CON GAS', 'JUGO DE NARANJA', 'JUGO DE DURAZNO'], 10, 1, 6500),
        'Cervezas y vinos': (['CERVEZA RUBIA', 'CERVEZA NEGRA', 'VINO TINTO', 'VINO BLANCO', 'CAÑA PARAGUAYA'], 10, 1, 14000),
        'Yerba y café': (['YERBA MATE', 'YERBA MATE CON MENTA', 'CAFÉ MOLIDO', 'TÉ NEGRO', 'COCIDO EN SAQUITOS'], 10, 1, 16000),
    },
    'LÁCTEOS Y FIAMBRES': {
        'Leches': (['LECHE ENTERA', 'LECHE DESCREMADA', 'LECHE EN POLVO', 'LECHE CHOCOLATADA'], 5, 1, 6000),
        'Yogures y postres': (['YOGUR DE FRUTILLA', 'YOGUR NATURAL', 'FLAN DE VAINILLA', 'POSTRE DE CHOCOLATE'], 10, 1, 7000),
        'Quesos': (['QUESO PARAGUAY', 'QUESO MOZZARELLA', 'QUESO CREMOSO', 'QUESO RALLADO'], 10, 2, 42000),
        'Fiambres': (['JAMÓN COCIDO', 'MORTADELA', 'SALAME', 'PANCETA AHUMADA'], 10, 2, 48000),
    },
    'CARNES Y VERDURAS': {
        'Carnicería': (['CARNE MOLIDA', 'ASADO DE TIRA', 'POLLO ENTERO', 'COSTILLA DE CERDO', 'MILANESA DE NALGA'], 5, 2, 38000),
        'Frutas': (['BANANA', 'MANZANA ROJA', 'NARANJA', 'MANDIOCA', 'PIÑA'], 0, 2, 9000),
        'Verduras': (['TOMATE', 'CEBOLLA', 'PAPA', 'ZANAHORIA', 'LECHUGA', 'LOCOTE'], 0, 2, 11000),
    },
    'LIMPIEZA': {
        'Ropa': (['JABÓN EN POLVO', 'JABÓN EN BARRA', 'SUAVIZANTE', 'LAVANDINA'], 10, 1, 15000),
        'Hogar': (['DETERGENTE', 'DESINFECTANTE', 'LIMPIAVIDRIOS', 'ESPONJA DE COCINA', 'PAPEL HIGIÉNICO'], 10, 1, 11500),
        'Suelta': (['ALCOHOL RECTIFICADO', 'CERA LÍQUIDA'], 10, 3, 18000),
    },
    'PERFUMERÍA': {
        'Cuidado personal': (['SHAMPOO', 'ACONDICIONADOR', 'JABÓN DE TOCADOR', 'DESODORANTE', 'CREMA DENTAL', 'AFEITADORA'], 10, 1, 19000),
        'Bebés': (['PAÑAL TALLE M', 'PAÑAL TALLE G', 'TOALLITAS HÚMEDAS', 'TALCO PARA BEBÉ'], 10, 1, 52000),
    },
    'PANADERÍA Y GOLOSINAS': {
        'Panificados': (['PAN DE MOLDE', 'GALLETITAS SURTIDAS', 'GALLETA MARÍA', 'CHIPÁ CONGELADO'], 10, 1, 8500),
        'Golosinas': (['CHOCOLATE CON LECHE', 'CARAMELOS SURTIDOS', 'ALFAJOR TRIPLE', 'CHICLES DE MENTA'], 10, 1, 4500),
    },
}
BRANDS = ['Kurupí', 'Selecta', 'Pajarito', 'La Fortuna', 'Doña Ñata', 'Trébol', 'Lactolanda', 'El Molino', 'San José',
          'Coronel', 'Aurora', 'Campo Sur', 'Bella Vista', 'Marangatú', 'Yguazú', 'Tres Ases', 'Del Chaco', 'Ñandutí',
          'Sol Naciente', 'Don Pedro', 'La Paraguaya', 'Mburucuyá', 'Itapúa', 'Cerro Corá', 'Primavera', 'Gran Sol']
SIZES = ['250 G', '500 G', '1 KG', '2 KG', '500 ML', '1 L', '1,5 L', '2 L', 'X 6', 'X 12', 'PACK 3', 'FAMILIAR', 'ECONÓMICO']
FIRST = ['María', 'José', 'Juan', 'Ana', 'Carlos', 'Rosa', 'Luis', 'Carmen', 'Pedro', 'Lourdes', 'Marcos', 'Ramona',
         'Óscar', 'Ángela', 'Félix', 'Mirta', 'Hugo', 'Nilda', 'Víctor', 'Zunilda', 'Néstor', 'Sofía', 'Adrián', 'Inés']
LAST = ['González', 'Benítez', 'Martínez', 'López', 'Giménez', 'Vera', 'Duarte', 'Ramírez', 'Rojas', 'Cáceres', 'Núñez',
        'Ortiz', 'Acosta', 'Báez', 'Fernández', 'Sosa', 'Ayala', 'Cabrera', 'Ibáñez', 'Peña', 'Domínguez', 'Ruiz Díaz']
COMPANY = ['Comercial', 'Distribuidora', 'Importadora', 'Mayorista', 'Agroindustrial', 'Frigorífico', 'Panificadora',
           'Lácteos', 'Alimentos', 'Servicios']
PLACE = ['del Sur', 'del Este', 'Guaraní', 'Asunción', 'Central', 'Ñeembucú', 'Itá', 'Luque', 'Caacupé', 'Encarnación',
         'Paraguarí', 'San Lorenzo', 'Limpio', 'Villarrica', 'Concepción', 'Capiatá', 'Lambaré', 'Areguá']
SUFFIX = ['S.A.', 'S.R.L.', 'E.I.R.L.', 'y Cía.']
RETURN_REASONS = ['Producto vencido', 'Envase dañado', 'No le sirvió', 'Se equivocó de artículo', 'Mala calidad']
VOID_REASONS = ['Error de carga', 'El cliente desistió', 'Cobrado dos veces', 'Precio equivocado']
DIFFERENCE_REASONS = ['Error al dar vuelto', 'Faltó registrar un gasto', 'Billete dudoso', 'Sin explicación']
ADJUST_REASONS = ['Rotura', 'Vencimiento', 'Robo o faltante', 'Consumo interno', 'Corrección']
METHODS = (('efectivo', 62), ('tarjeta_debito', 15), ('tarjeta_credito', 11), ('transferencia', 7), ('qr', 5))
HOURS = [(7, 3), (8, 6), (9, 8), (10, 10), (11, 11), (12, 8), (13, 5), (14, 5), (15, 6), (16, 8), (17, 11), (18, 12), (19, 9), (20, 4)]


def line_total(unit, quantity):
    product = unit * quantity
    return (product + ONE // 2) // ONE


def vat_included(amount, rate):
    return 0 if rate <= 0 else (amount * rate + (100 + rate) // 2) // (100 + rate)


def cash_rounding(amount):
    return (amount + CASH_STEP // 2) // CASH_STEP * CASH_STEP - amount


def ruc_digit(base):
    total, weight = 0, 2
    for c in reversed(base):
        total += int(c) * weight
        weight = 2 if weight == 11 else weight + 1
    remainder = total % 11
    return 11 - remainder if remainder > 1 else 0


def ean13(digits):
    odd = sum(int(c) for c in digits[0::2])
    even = sum(int(c) for c in digits[1::2])
    return digits + str((10 - (odd + even * 3) % 10) % 10)


def password_hash(rng, password, iterations=120000):
    salt = bytes(rng.getrandbits(8) for _ in range(16))
    key = hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, iterations, 32)
    return 'pbkdf2-sha256$%d$%s$%s' % (iterations, base64.b64encode(salt).decode(), base64.b64encode(key).decode())


def stamp(moment):
    return moment.strftime('%Y-%m-%dT%H:%M:%S')


def price_step(amount):
    return max(100, (amount + 50) // 100 * 100)


class Shop:
    def __init__(self, db, args):
        self.db = db
        self.rng = random.Random(args.seed)
        self.args = args
        self.last_day = args.end
        self.first_day = args.end - dt.timedelta(days=args.days - 1)
        self.products = {}
        self.sellable, self.cumulative = [], []
        self.movements, self.items, self.payments, self.prices = [], [], [], []
        self.recent = []                # sales that can still be returned
        self.sale_id = self.session_id = self.purchase_id = self.return_id = self.movement_id = 0
        self.numbers = {1: 0, 2: 0}
        self.notes = []                 # credit notes with a balance
        self.invoices = 0
        self.counts = dict(sales=0, voided=0, returns=0, purchases=0, sessions=0, notes=0)

    def run(self, sql, rows):
        self.db.executemany(sql, rows)

    # --- what doesn't change every day ------------------------------------------

    def people(self):
        rng = self.rng
        users = [('admin', 'Administrador de Prueba', 1), ('encargado', 'Lourdes Benítez', 2), ('supervisor', 'Hugo Núñez', 2)]
        users += [('cajero%d' % i, '%s %s' % (rng.choice(FIRST), rng.choice(LAST)), 3) for i in range(1, 7)]
        hash_ = password_hash(rng, PASSWORD)    # the same one: this is a test database
        for i, (name, full, role) in enumerate(users, 1):
            self.db.execute('INSERT INTO usuarios (id, usuario, nombre, clave, es_activo) VALUES (?, ?, ?, ?, ?)',
                            (i, name, full, hash_, 0 if name == 'cajero6' else 1))
            self.db.execute('INSERT INTO usuario_tiene_roles (usuario_id, rol_id) VALUES (?, ?)', (i, role))
        self.cashiers = [u[0] for u in users if u[2] == 3]
        self.supervisors = ['encargado', 'supervisor']
        self.run('INSERT INTO configuracion (clave, valor) VALUES (?, ?)', STORE)
        self.run('INSERT INTO cajas (id, nombre, puesto) VALUES (?, ?, ?)', [(1, 'Caja 1', 'PRUEBA-CAJA-1'), (2, 'Caja 2', 'PRUEBA-CAJA-2')])

    def suppliers(self):
        rng = self.rng
        names, rows = set(), []
        while len(rows) < self.args.suppliers:
            name = '%s %s %s' % (rng.choice(COMPANY), rng.choice(PLACE), rng.choice(SUFFIX))
            if name in names:
                continue
            names.add(name)
            slug = ''.join(c for c in name.lower().split()[1] if c.isascii() and c.isalpha()) or 'proveedor'
            rows.append((len(rows) + 1, name, 'Ruta %d km %d' % (rng.randint(1, 9), rng.randint(5, 300)),
                         '021 %03d %03d' % (rng.randint(200, 999), rng.randint(0, 999)),
                         'ventas%d@%s.com.py' % (len(rows) + 1, slug), '', '%s %s' % (rng.choice(FIRST), rng.choice(LAST)),
                         'www.%s%d.com.py' % (slug, len(rows) + 1), 0 if rng.random() < 0.06 else 1))
        self.run('INSERT INTO proveedores (id, nombre, direccion, telefono, email, otros_detalles, contacto, web, es_activo)'
                 ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)', rows)
        self.supplier_ids = [r[0] for r in rows if r[8]]

    def customers(self):
        rng = self.rng
        used, rows = set(), []
        when = stamp(dt.datetime.combine(self.first_day, dt.time(8)))
        while len(rows) < self.args.customers:
            number = rng.randint(800000, 7999999)
            company = rng.random() < 0.2
            if number in used:
                continue
            used.add(number)
            if company:
                base = str(80000000 + number % 999999)
                name = '%s %s %s' % (rng.choice(COMPANY), rng.choice(PLACE), rng.choice(SUFFIX))
                kind, document = 'ruc', '%s-%d' % (base, ruc_digit(base))
            elif rng.random() < 0.5:
                name = '%s %s %s' % (rng.choice(FIRST), rng.choice(LAST), rng.choice(LAST))
                kind, document = 'ruc', '%d-%d' % (number, ruc_digit(str(number)))
            else:
                name = '%s %s %s' % (rng.choice(FIRST), rng.choice(LAST), rng.choice(LAST))
                kind, document = 'ci', str(number)
            if document in used:
                continue
            used.add(document)
            rows.append((len(rows) + 1, name, kind, document, 'Barrio %s' % rng.choice(PLACE),
                         '09%d%d %06d' % (rng.randint(7, 8), rng.randint(1, 9), rng.randint(0, 999999)),
                         'cliente%d@correo.com.py' % (len(rows) + 1), 0 if rng.random() < 0.03 else 1, when))
        self.run('INSERT INTO clientes (id, nombre, tipo_documento, documento, direccion, telefono, correo, activo, alta)'
                 ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)', rows)
        self.customer_rows = [r for r in rows if r[7]]

    def catalog(self):
        rng = self.rng
        self.run('INSERT INTO marcas (id, nombre) VALUES (?, ?)', list(enumerate(BRANDS, 1)))
        groups, category_id = [], 0
        for top, subs in CATALOG.items():
            category_id += 1
            parent = category_id
            self.db.execute('INSERT INTO rubros (id, rubro) VALUES (?, ?)', (parent, top))
            for sub, spec in subs.items():
                category_id += 1
                self.db.execute('INSERT INTO rubros (id, rubro, parent_id) VALUES (?, ?, ?)', (category_id, sub, parent))
                groups.append((category_id, spec))

        start = stamp(dt.datetime.combine(self.first_day, dt.time(6)))
        codes, rows, barcodes = set(), [], []
        while len(rows) < self.args.articles:
            category, (names, vat, unit, base) = rng.choice(groups)
            brand = rng.randrange(len(BRANDS)) + 1
            size = '' if unit != 1 else ' ' + rng.choice(SIZES)
            description = '%s %s%s' % (rng.choice(names), BRANDS[brand - 1].upper(), size)
            code = ean13('784%09d' % rng.randint(0, 999999999))
            if code in codes:
                continue
            codes.add(code)
            price = price_step(int(base * rng.uniform(0.55, 1.8)))
            cost = price_step(int(price * rng.uniform(0.62, 0.86)))
            decimals, abbreviation = {1: (0, 'un'), 2: (3, 'kg'), 3: (3, 'l')}[unit]
            minimum = rng.randint(2, 24) * ONE
            stock = minimum * rng.randint(3, 8)
            supplier = rng.choice(self.supplier_ids)
            rank = len(rows) + 1
            dead = rng.random() < self.args.dead / 100
            active = 0 if rng.random() < 0.03 else 1
            self.products[code] = dict(code=code, description=description, price=price, cost=cost, stock=stock, minimum=minimum,
                                       unit=abbreviation, decimals=decimals, vat=vat, supplier=supplier, active=active)
            rows.append((code, description, price, cost, stock, minimum, self.first_day.isoformat(), start, active, brand,
                         category, vat, supplier, unit))
            self.moved(code, 'inicial', stock, stock, cost, start, 'Saldo inicial', '', 'admin')
            if rng.random() < 0.08:
                barcodes.append((ean13('789%09d' % (len(rows) + 1)), code))
            if active and not dead:
                self.sellable.append(code)
                self.cumulative.append((self.cumulative[-1] if self.cumulative else 0) + 1 / rank ** 0.8)
        self.run('INSERT INTO productos (codigo, descripcion, precio, costo, stock, stock_min, f_compra, f_mod, es_activo, marca_id,'
                 ' rubro_id, iva, proveedor_id, unidad_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', rows)
        self.run('INSERT INTO codigos_barras (codigo, producto_codigo) VALUES (?, ?)', barcodes)

    # --- ledger -----------------------------------------------------------------

    def moved(self, code, kind, quantity, balance, cost, when, reason='', document='', user='', station='PRUEBA-CAJA-1'):
        self.movement_id += 1
        self.movements.append((self.movement_id, when, code, kind, quantity, balance, cost, reason, document, user, station))

    def move(self, code, kind, quantity, cost, when, reason='', document='', user='', station='PRUEBA-CAJA-1'):
        p = self.products[code]
        p['stock'] += quantity
        self.moved(code, kind, quantity, p['stock'], cost, when, reason, document, user, station)

    def flush(self):
        self.run('INSERT INTO stock_movimientos (id, fecha, producto_codigo, tipo, cantidad, saldo, costo_unitario, motivo,'
                 ' documento, usuario, puesto) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', self.movements)
        self.run('INSERT INTO ventas_items (venta_id, linea, producto_codigo, descripcion, unidad, decimales, cantidad,'
                 ' precio_unitario, bruto, descuento, importe, iva_tasa, iva, costo_unitario)'
                 ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', self.items)
        self.run('INSERT INTO ventas_pagos (venta_id, medio, monto, vuelto, referencia) VALUES (?, ?, ?, ?, ?)', self.payments)
        self.run('INSERT INTO precios_historial (fecha, producto_codigo, precio_anterior, precio_nuevo, costo_anterior,'
                 ' costo_nuevo, motivo, usuario) VALUES (?, ?, ?, ?, ?, ?, ?, ?)', self.prices)
        self.movements, self.items, self.payments, self.prices = [], [], [], []
        self.db.commit()

    # --- one day ----------------------------------------------------------------

    def purchases(self, day):
        """Every article at its minimum goes on an order. The last days get none, so that stock runs low."""
        if (self.last_day - day).days < 4:
            return
        orders = {}
        for code in self.sellable:
            p = self.products[code]
            if p['stock'] <= p['minimum']:
                orders.setdefault(p['supplier'], []).append(code)
        when = stamp(dt.datetime.combine(day, dt.time(6, 30)))
        for supplier, codes in orders.items():
            self.invoices += 1
            self.purchase_id += 1
            invoice = '001-001-%07d' % self.invoices
            total, rows = 0, []
            for code in codes:
                p = self.products[code]
                quantity = p['minimum'] * self.rng.randint(4, 9)
                total += line_total(p['cost'], quantity)
                rows.append((self.purchase_id, code, quantity, p['cost'], 0))
                self.move(code, 'compra', quantity, p['cost'], when, '', 'Factura %s' % invoice, 'encargado')
            self.db.execute('INSERT INTO compras (id, fecha, proveedor_id, factura, total, usuario) VALUES (?, ?, ?, ?, ?, ?)',
                            (self.purchase_id, day.isoformat(), supplier, invoice, total, 'encargado'))
            self.run('INSERT INTO compra_items (compra_id, producto_codigo, cantidad, costo_unitario, precio_nuevo)'
                     ' VALUES (?, ?, ?, ?, ?)', rows)
            self.counts['purchases'] += 1

    def reprice(self, day):
        """Prices go up some four times a year, so that cost and price move under the sales."""
        rng = self.rng
        if rng.random() > 1 / 90:
            return
        when = stamp(dt.datetime.combine(day, dt.time(6, 0)))
        factor = rng.uniform(1.02, 1.07)
        changed = []
        for code in rng.sample(self.sellable, len(self.sellable) // 3):
            p = self.products[code]
            price, cost = price_step(int(p['price'] * factor)), price_step(int(p['cost'] * factor))
            self.prices.append((when, code, p['price'], price, p['cost'], cost, 'Cambio masivo', 'encargado'))
            p['price'], p['cost'] = price, cost
            changed.append((price, cost, when, code))
        self.run('UPDATE productos SET precio = ?, costo = ?, f_mod = ? WHERE codigo = ?', changed)

    def adjustments(self, day):
        rng = self.rng
        when = stamp(dt.datetime.combine(day, dt.time(21, 15)))
        for _ in range(rng.randint(0, 3)):
            code = rng.choice(self.sellable)
            p = self.products[code]
            quantity = -min(p['stock'], rng.randint(1, 3) * ONE)
            if quantity:
                self.move(code, 'ajuste', quantity, p['cost'], when, rng.choice(ADJUST_REASONS), '', 'encargado')
        if day.day == 1:                # the monthly count of a few shelves
            for code in rng.sample(self.sellable, min(60, len(self.sellable))):
                p = self.products[code]
                delta = rng.randint(-2, 2) * ONE
                if delta and p['stock'] + delta >= 0:
                    self.move(code, 'recuento', delta, p['cost'], when, 'Recuento físico: Todos los rubros', '', 'encargado')

    def tickets_today(self, day):
        """Saturdays and paydays are busy, Sundays quiet, December the best month, and the shop grows."""
        weekday = (1.0, 0.9, 0.9, 1.0, 1.25, 1.55, 0.55)[day.weekday()]
        month = {12: 1.6, 1: 0.85, 2: 0.85, 7: 1.1}.get(day.month, 1.0)
        payday = 1.3 if day.day <= 5 or 28 <= day.day else 1.0
        growth = 0.8 + 0.4 * (day - self.first_day).days / max(1, self.args.days)
        return int(self.args.tickets * weekday * month * payday * growth * self.rng.uniform(0.9, 1.1))

    def open_session(self, day, register):
        rng = self.rng
        self.session_id += 1
        opened = dt.datetime.combine(day, dt.time(6, 45)) + dt.timedelta(minutes=rng.randint(0, 20))
        user = rng.choice(self.cashiers[:5])
        cash = 500000 + 100000 * rng.randint(0, 3)
        self.db.execute('INSERT INTO sesiones_caja (id, caja_id, estado, apertura, usuario_apertura, fondo) VALUES (?, ?, ?, ?, ?, ?)',
                        (self.session_id, register, 'abierta', stamp(opened), user, cash))
        self.count(self.session_id, 'apertura', cash)
        self.counts['sessions'] += 1
        return dict(id=self.session_id, register=register, user=user, cash=cash, opened=opened)

    def count(self, session, moment, amount):
        rows = []
        for note in NOTES:
            if amount >= note:
                rows.append((session, moment, note, amount // note))
                amount %= note
        self.run('INSERT INTO sesion_conteos (sesion_id, momento, denominacion, cantidad) VALUES (?, ?, ?, ?)', rows)

    def cash_movement(self, session, when):
        rng = self.rng
        roll = rng.random()
        if roll < 0.7 and session['cash'] > 2500000:
            kind, amount, reason = 'retiro', session['cash'] // 100000 * 60000, 'Retiro parcial al cofre'
        elif roll < 0.9 and session['cash'] > 400000:
            kind, amount, reason = 'egreso', rng.randint(2, 30) * 10000, rng.choice(['Pago a proveedor', 'Limpieza', 'Flete', 'Combustible'])
        elif roll >= 0.9:
            kind, amount, reason = 'ingreso', rng.randint(2, 10) * 50000, 'Cambio para la caja'
        else:
            return
        session['cash'] += amount if kind == 'ingreso' else -amount
        self.db.execute('INSERT INTO caja_movimientos (sesion_id, fecha, tipo, monto, motivo, usuario) VALUES (?, ?, ?, ?, ?, ?)',
                        (session['id'], stamp(when), kind, amount, reason, session['user']))

    def pick_lines(self):
        rng = self.rng
        wanted = min(40, 1 + int(rng.expovariate(1 / 3.2)))
        chosen = {}
        for code in rng.choices(self.sellable, cum_weights=self.cumulative, k=wanted):
            if code in chosen:
                continue
            p = self.products[code]
            quantity = rng.randint(250, 3000) if p['decimals'] else rng.choice((1, 1, 1, 2, 2, 3, 6)) * ONE
            quantity = min(quantity, p['stock'])
            if not p['decimals']:
                quantity -= quantity % ONE
            if quantity > 0:
                chosen[code] = quantity
        return chosen

    def sale(self, session, when):
        rng = self.rng
        chosen = self.pick_lines()
        if not chosen:
            return
        moment = stamp(when)
        items = []
        for code, quantity in chosen.items():
            p = self.products[code]
            gross = line_total(p['price'], quantity)
            discount = gross // 100 * rng.choice((5, 10)) if rng.random() < 0.05 else 0
            items.append(dict(code=code, description=p['description'], unit=p['unit'], decimals=p['decimals'], quantity=quantity,
                              price=p['price'], gross=gross, discount=discount, net=gross - discount, vat_rate=p['vat'], cost=p['cost']))

        # the sale's discount, shared among the lines exactly as AguaraSale::totals() does
        after_lines = sum(i['net'] for i in items)
        sale_discount = after_lines // 100 * rng.choice((5, 10)) if rng.random() < 0.03 else 0
        running, shared = 0, 0
        for item in items:
            running += item['net']
            reached = (sale_discount * running + after_lines // 2) // after_lines
            share = reached - shared
            shared = reached
            item['discount'] += share
            item['net'] -= share
        totals = dict(gross=0, discount=0, net=0, t10=0, v10=0, t5=0, v5=0, exempt=0)
        for item in items:
            item['vat'] = vat_included(item['net'], item['vat_rate'])
            totals['gross'] += item['gross']
            totals['discount'] += item['discount']
            totals['net'] += item['net']
            if item['vat_rate'] == 10:
                totals['t10'] += item['net']
                totals['v10'] += item['vat']
            elif item['vat_rate'] == 5:
                totals['t5'] += item['net']
                totals['v5'] += item['vat']
            else:
                totals['exempt'] += item['net']
        net = totals['net']
        if net < CASH_STEP:
            return

        customer = rng.choice(self.customer_rows) if rng.random() < 0.2 else None
        self.sale_id += 1
        self.numbers[session['register']] += 1
        number = self.numbers[session['register']]
        printed = '%03d-%06d' % (session['register'], number)

        # payments: a credit note, or a card, then what is left in cash
        payments, electronic = [], 0
        method = rng.choices([m for m, _ in METHODS], [w for _, w in METHODS])[0]
        if self.notes and rng.random() < 0.01:
            note = self.notes.pop()
            used = min(note['balance'], net)
            note['balance'] -= used
            self.db.execute('UPDATE notas_credito SET saldo = ? WHERE id = ?', (note['balance'], note['id']))
            payments.append(('nota_credito', used, 0, 'NC-%06d' % note['id']))
            electronic += used
        if method != 'efectivo' and electronic < net:
            amount = net - electronic
            if rng.random() < 0.08 and amount > 20000:     # part by card, the rest in cash
                amount = amount // 2 // 1000 * 1000
            payments.append((method, amount, 0, '%s%d' % (method[0].upper(), rng.randint(100000, 999999))))
            electronic += amount
        left = net - electronic
        due = left + cash_rounding(left)
        cash_paid = 0
        if due > 0:
            step = rng.choice((1000, 5000, 10000, 50000, 100000))
            tendered = (due + step - 1) // step * step if rng.random() < 0.7 else due
            payments.append(('efectivo', tendered, tendered - due, None))
            session['cash'] += due
            cash_paid = due
        total = electronic + due

        self.db.execute(
            'INSERT INTO ventas (id, uuid, caja_id, sesion_id, numero, fecha, usuario, estado, impresiones, cliente_id, cliente_nombre,'
            ' cliente_documento, bruto, descuento, neto, redondeo, total, gravado10, iva10, gravado5, iva5, exento)'
            ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
            (self.sale_id, str(uuid.UUID(int=rng.getrandbits(128), version=4)), session['register'], session['id'], number, moment,
             session['user'], 'cobrada', 1, customer[0] if customer else None, customer[1] if customer else '',
             customer[3] if customer else '', totals['gross'], totals['discount'], net, total - net, total,
             totals['t10'], totals['v10'], totals['t5'], totals['v5'], totals['exempt']))
        station = 'PRUEBA-CAJA-%d' % session['register']
        for position, item in enumerate(items, 1):
            self.items.append((self.sale_id, position, item['code'], item['description'], item['unit'], item['decimals'],
                               item['quantity'], item['price'], item['gross'], item['discount'], item['net'], item['vat_rate'],
                               item['vat'], item['cost']))
            self.move(item['code'], 'venta', -item['quantity'], item['cost'], moment, '', 'Venta %s' % printed, session['user'], station)
        for p in payments:
            self.payments.append((self.sale_id,) + p)
        self.counts['sales'] += 1

        record = dict(id=self.sale_id, printed=printed, items=items, session=session, payments=payments, cash=cash_paid,
                      total=total, customer=customer, day=when.date())
        if rng.random() < 0.008:
            return record, 'void'
        if rng.random() < 0.012:
            self.recent.append(record)
        return record, None

    def void(self, record, when):
        rng = self.rng
        session = record['session']
        moment = stamp(when)
        supervisor = rng.choice(self.supervisors)
        if any(p[0] == 'nota_credito' for p in record['payments']):
            return                      # its balance would have to come back: not worth it here
        session['cash'] -= record['cash']
        reason = rng.choice(VOID_REASONS)
        self.db.execute('UPDATE ventas SET estado = ?, anulada_por = ?, anulada_fecha = ?, motivo = ? WHERE id = ?',
                        ('anulada', supervisor, moment, reason, record['id']))
        for item in record['items']:
            self.move(item['code'], 'devolucion', item['quantity'], item['cost'], moment, reason,
                      'Anulación de la venta %s' % record['printed'], supervisor)
        self.db.execute('INSERT INTO auditoria (fecha, usuario, autorizado_por, puesto, accion, entidad, entidad_id, despues)'
                        ' VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                        (moment, supervisor, supervisor, 'PRUEBA-CAJA-1', 'venta.anular', 'venta', record['printed'], reason))
        self.counts['voided'] += 1

    def give_back(self, record, session, when):
        """A return of one line, whole or in part, in the session open today."""
        rng = self.rng
        moment = stamp(when)
        item = rng.choice(record['items'])
        sold = item['quantity']
        quantity = sold
        if sold > ONE and rng.random() < 0.3:
            quantity = sold // 2
            if not item['decimals']:
                quantity -= quantity % ONE
        amount = item['net'] if quantity >= sold else (item['net'] * quantity + sold // 2) // sold

        by_method = {p[0]: p[1] - p[2] for p in record['payments']}
        method = 'nota_credito'
        paid = amount
        if rng.random() < 0.7:
            cash_back = amount + cash_rounding(amount)
            if by_method.get('efectivo', 0) >= cash_back and session['cash'] >= cash_back:
                method, paid = 'efectivo', cash_back
                session['cash'] -= cash_back
            else:
                for name, available in by_method.items():
                    if name not in ('efectivo', 'nota_credito') and available >= amount:
                        method = name
                        break
        self.return_id += 1
        reference = '' if method in ('efectivo', 'nota_credito') else 'REV%d' % rng.randint(100000, 999999)
        reason = rng.choice(RETURN_REASONS)
        supervisor = rng.choice(self.supervisors)
        self.db.execute('INSERT INTO devoluciones (id, venta_id, caja_id, sesion_id, fecha, usuario, autorizo, motivo, importe, pagado,'
                        ' medio, referencia) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
                        (self.return_id, record['id'], session['register'], session['id'], moment, session['user'], supervisor,
                         reason, amount, paid, method, reference))
        line = record['items'].index(item) + 1
        self.db.execute('INSERT INTO devoluciones_items (devolucion_id, linea, producto_codigo, descripcion, unidad, decimales,'
                        ' cantidad, importe, iva) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)',
                        (self.return_id, line, item['code'], item['description'], item['unit'], item['decimals'], quantity, amount,
                         vat_included(amount, item['vat_rate'])))
        self.move(item['code'], 'devolucion', quantity, item['cost'], moment, reason, 'Devolución de la venta %s' % record['printed'],
                  session['user'], 'PRUEBA-CAJA-%d' % session['register'])
        if method == 'nota_credito':
            customer = record['customer']
            cursor = self.db.execute('INSERT INTO notas_credito (devolucion_id, cliente_id, cliente_nombre, fecha, monto, saldo)'
                                     ' VALUES (?, ?, ?, ?, ?, ?)',
                                     (self.return_id, customer[0] if customer else None, customer[1] if customer else '', moment,
                                      amount, amount))
            note = cursor.lastrowid
            self.db.execute('UPDATE devoluciones SET referencia = ? WHERE id = ?', ('NC-%06d' % note, self.return_id))
            self.notes.append(dict(id=note, balance=amount))
            self.counts['notes'] += 1
        self.db.execute('INSERT INTO auditoria (fecha, usuario, autorizado_por, puesto, accion, entidad, entidad_id, despues)'
                        ' VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
                        (moment, session['user'], supervisor, 'PRUEBA-CAJA-1', 'venta.devolver', 'venta', record['printed'], reason))
        self.counts['returns'] += 1

    def close_session(self, session, day):
        rng = self.rng
        closed = dt.datetime.combine(day, dt.time(21)) + dt.timedelta(minutes=rng.randint(0, 25))
        expected = session['cash']
        difference, reason, authorized = 0, None, None
        if rng.random() < 0.08:
            difference = rng.choice((-1, -1, 1)) * rng.randint(1, 40) * 500
            reason, authorized = rng.choice(DIFFERENCE_REASONS), rng.choice(self.supervisors)
        counted = expected + difference
        self.db.execute('UPDATE sesiones_caja SET estado = ?, cierre = ?, usuario_cierre = ?, esperado = ?, contado = ?, diferencia = ?,'
                        ' motivo = ?, autorizado_por = ? WHERE id = ?',
                        ('cerrada', stamp(closed), session['user'], expected, counted, difference, reason, authorized, session['id']))
        self.count(session['id'], 'cierre', counted)

    def day(self, day):
        rng = self.rng
        self.reprice(day)
        self.purchases(day)
        registers = (1, 2) if day.weekday() != 6 else (1,)
        sessions = [self.open_session(day, r) for r in registers]

        # sales, voids and cash movements in the order they happen
        events, order = [], 0
        hours = [h for h, _ in HOURS]
        weights = [w for _, w in HOURS]
        for _ in range(self.tickets_today(day)):
            moment = dt.datetime.combine(day, dt.time(rng.choices(hours, weights)[0], rng.randint(0, 59), rng.randint(0, 59)))
            order += 1
            events.append((moment, order, 'sale', rng.choice(sessions)))
        for session in sessions:
            for hour in (12, 18):
                order += 1
                events.append((dt.datetime.combine(day, dt.time(hour, rng.randint(0, 40))), order, 'movement', session))
        heapq.heapify(events)
        while events:
            moment, _, kind, payload = heapq.heappop(events)
            if kind == 'movement':
                self.cash_movement(payload, moment)
            elif kind == 'void':
                self.void(payload, moment)
            else:
                outcome = self.sale(payload, moment)
                if outcome and outcome[1] == 'void' and moment.hour < 20:
                    order += 1
                    later = moment + dt.timedelta(minutes=rng.randint(2, 35))
                    heapq.heappush(events, (later, order, 'void', outcome[0]))

        # returns come days later, in a session open that day
        due = [r for r in self.recent if (day - r['day']).days >= 1 and rng.random() < 0.2]
        for record in due:
            self.recent.remove(record)
            self.give_back(record, rng.choice(sessions), dt.datetime.combine(day, dt.time(rng.randint(9, 19), rng.randint(0, 59))))
        self.recent = [r for r in self.recent if (day - r['day']).days <= 30]

        for session in sessions:
            self.close_session(session, day)
        self.adjustments(day)

    def build(self):
        self.people()
        self.suppliers()
        self.customers()
        self.catalog()
        day = self.first_day
        while day <= self.last_day:
            self.day(day)
            if len(self.movements) > 200000:
                self.flush()
            if day.day == 1:
                print('  %s  %7d ventas' % (day.isoformat(), self.counts['sales']), flush=True)
            day += dt.timedelta(days=1)
        self.flush()
        self.run('UPDATE productos SET stock = ? WHERE codigo = ?', [(p['stock'], c) for c, p in self.products.items()])
        for register, number in self.numbers.items():
            self.db.execute('UPDATE cajas SET ultimo_numero = ? WHERE id = ?', (number, register))
        self.db.commit()


def check(db):
    """What the program takes for granted. Any row here is a bug in this script."""
    tests = {
        'integrity': "SELECT 1 FROM pragma_integrity_check WHERE integrity_check <> 'ok'",
        'foreign keys': 'SELECT 1 FROM pragma_foreign_key_check',
        'stock = sum of its movements': 'SELECT p.codigo FROM productos p LEFT JOIN stock_movimientos m ON m.producto_codigo = p.codigo'
                                        ' GROUP BY p.codigo HAVING p.stock <> COALESCE(SUM(m.cantidad), 0)',
        'no negative stock': 'SELECT codigo FROM productos WHERE stock < 0',
        'lines add up to the sale': 'SELECT v.id FROM ventas v JOIN ventas_items i ON i.venta_id = v.id GROUP BY v.id'
                                    ' HAVING v.neto <> SUM(i.importe) OR v.bruto <> SUM(i.bruto) OR v.descuento <> SUM(i.descuento)'
                                    ' OR v.gravado10 + v.gravado5 + v.exento <> v.neto',
        'payments add up to the total': 'SELECT v.id FROM ventas v JOIN ventas_pagos p ON p.venta_id = v.id GROUP BY v.id'
                                        ' HAVING v.total <> SUM(p.monto - p.vuelto)',
        'expected cash of each session': """
            SELECT s.id FROM sesiones_caja s WHERE s.esperado <> s.fondo
              + COALESCE((SELECT SUM(p.monto - p.vuelto) FROM ventas_pagos p JOIN ventas v ON v.id = p.venta_id
                          WHERE v.sesion_id = s.id AND v.estado = 'cobrada' AND p.medio = 'efectivo'), 0)
              - COALESCE((SELECT SUM(d.pagado) FROM devoluciones d WHERE d.sesion_id = s.id AND d.medio = 'efectivo'), 0)
              + COALESCE((SELECT SUM(CASE m.tipo WHEN 'ingreso' THEN m.monto ELSE -m.monto END) FROM caja_movimientos m
                          WHERE m.sesion_id = s.id), 0)""",
        'counted = expected + difference': 'SELECT id FROM sesiones_caja WHERE contado <> esperado + diferencia',
        'closing count adds up': """SELECT s.id FROM sesiones_caja s JOIN sesion_conteos c ON c.sesion_id = s.id AND c.momento = 'cierre'
                                    GROUP BY s.id HAVING s.contado <> SUM(c.denominacion * c.cantidad)""",
        'no session left open': "SELECT id FROM sesiones_caja WHERE estado <> 'cerrada'",
        'returns only of paid sales': "SELECT d.id FROM devoluciones d JOIN ventas v ON v.id = d.venta_id WHERE v.estado <> 'cobrada'",
        'numbers are unique and complete': 'SELECT c.id FROM cajas c WHERE c.ultimo_numero <>'
                                           ' COALESCE((SELECT COUNT(*) FROM ventas v WHERE v.caja_id = c.id), 0)',
    }
    failed = 0
    for name, sql in tests.items():
        rows = db.execute('SELECT COUNT(*) FROM (%s)' % sql).fetchone()[0]
        print('  %-34s %s' % (name, 'ok' if rows == 0 else '%d WRONG' % rows))
        failed += 1 if rows else 0
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('template')
    parser.add_argument('output')
    parser.add_argument('--days', type=int, default=730)
    parser.add_argument('--tickets', type=int, default=220, help='tickets on an ordinary day')
    parser.add_argument('--articles', type=int, default=3000)
    parser.add_argument('--customers', type=int, default=3000)
    parser.add_argument('--suppliers', type=int, default=60)
    parser.add_argument('--dead', type=int, default=6, help='percent of articles that never sell')
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--end', type=dt.date.fromisoformat, default=dt.date.today() - dt.timedelta(days=1))
    args = parser.parse_args()

    if os.path.abspath(args.template) == os.path.abspath(args.output):
        sys.exit('the template and the output must be different files')
    template = sqlite3.connect('file:%s?mode=ro' % args.template.replace('\\', '/'), uri=True)
    counted = template.execute('SELECT (SELECT COUNT(*) FROM ventas), (SELECT COUNT(*) FROM productos),'
                               ' (SELECT COUNT(*) FROM usuarios)').fetchone()
    if any(counted):
        sys.exit('the template must be an empty database')
    template.close()
    for leftover in (args.output, args.output + '-wal', args.output + '-shm'):
        if os.path.exists(leftover):
            os.remove(leftover)
    shutil.copyfile(args.template, args.output)

    db = sqlite3.connect(args.output)
    db.execute('PRAGMA journal_mode = OFF')
    db.execute('PRAGMA synchronous = OFF')
    db.execute('PRAGMA cache_size = -400000')
    print('Generando %d días, desde %s...' % (args.days, args.end - dt.timedelta(days=args.days - 1)), flush=True)
    shop = Shop(db, args)
    shop.build()
    db.execute('PRAGMA journal_mode = DELETE')
    db.execute('PRAGMA synchronous = FULL')
    print('Índices y estadísticas...', flush=True)
    db.execute('ANALYZE')
    db.commit()
    print('Comprobando...', flush=True)
    failed = check(db)
    for table in ('productos', 'proveedores', 'clientes', 'ventas', 'ventas_items', 'ventas_pagos', 'devoluciones', 'notas_credito',
                  'compras', 'stock_movimientos', 'sesiones_caja', 'caja_movimientos', 'precios_historial'):
        print('  %-20s %9d' % (table, db.execute('SELECT COUNT(*) FROM %s' % table).fetchone()[0]))
    db.close()
    print('%s: %.0f MB' % (args.output, os.path.getsize(args.output) / 1e6))
    print('Usuarios: admin, encargado, supervisor, cajero1 a cajero5. Contraseña de todos: %s' % PASSWORD)
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
