# Hoja de ruta de AguaraPOS

Línea base: commit `6d8f7e7` (reconexión de PostgreSQL en segundo plano).

Objetivo: llevar AguaraPOS a una **primera versión usable en un comercio real**, la v1.0. El trabajo se ordena por dependencias: cada fase entrega algo que se puede probar y usar.

---

## 1. Dónde estamos

**Lo que ya funciona** (versión 1.0.0-beta.2, probada en Windows 7 con el servidor PostgreSQL incluido y actualizada con un parche)

| Área | Estado |
|---|---|
| Arquitectura | Core, `aguara_interfaces.dll` y módulos (plugins con `QPluginLoader`). Compilación SUBDIRS. Wizard de Qt Creator para crear módulos. |
| Plataforma | Qt 5.15.2, MinGW de 32 bits. Compatible con Windows 7. |
| Datos | SQLite (un puesto) o PostgreSQL (varios puestos en LAN). Migraciones por dueño, `NOTIFY` entre cajas, reconexión en segundo plano y cola local de ventas, para no perder ninguna sin conexión. |
| Dinero | `Money = qint64` en guaraníes enteros, con IVA incluido (10 %, 5 % y exenta). Formato `es_PY`. |
| Usuarios | Roles, permisos, autorización de supervisor, bloqueo y auditoría. |
| Inventario | Stock como libro de movimientos, compras, proveedores, recuentos, cambio de precios, etiquetas y aviso de stock bajo en el momento. |
| Ventas | Caja con arqueo a ciegas, pagos combinados, comprobantes por conciliar, clientes, devoluciones y notas de crédito. |
| Salidas | Ticket interno ESC/POS, cajón, planilla de cierre y reportes con exportación a PDF y CSV. |
| Versiones | Versión del programa y nivel de API separados, con `autoVersion.py`. Un módulo de otra API no carga. |
| Instalación | Instalador y parches con Inno Setup. Manuales del cajero y del encargado en PDF. |
| Calidad | Pruebas en el repositorio, en SQLite y PostgreSQL, y CI en GitHub Actions. |

**Lo que falta para usarlo en un comercio**

- **PostgreSQL 10.23:** el servidor ya funcionó en Windows 7, pero las pruebas automáticas corren contra PostgreSQL 17, no contra esa versión.
- **Hardware:** no se probó una impresora térmica ni un cajón reales.
- **Piloto:** el comercio ya está elegido. Falta elegir la impresora y el cajón (decisión 3 de la sección 5) y hacer el piloto.

---

## 2. Decisiones de diseño transversales

Estas decisiones afectan a todas las fases, así que conviene fijarlas antes de empezar.

### 2.1 Servicios en el core, pantallas en módulos

Se mantiene el patrón de `ProductService`:

- **Reglas de negocio y SQL:** viven en el core y se publican como servicios a través de `AguaraHost`.
- **Módulos:** solo aportan UI, o extienden el sistema mediante interfaces.

Servicios nuevos:

| Servicio | Responsabilidad |
|---|---|
| `UserService` | Inicio de sesión, usuarios, roles, permisos, autorización de supervisor y auditoría |
| `InventoryService` | Movimientos de stock, compras, ajustes, recuentos y proveedores |
| `SalesService` | Cobro, cola local de ventas, anulación y numeración. La venta abierta vive en la pantalla |
| `CashService` | Cajas, sesiones de caja, movimientos de efectivo, arqueo y cierre |
| `PrintService` | Comprobantes, reimpresión y apertura del cajón |
| `ReportService` | Consultas de reportes, exportación a CSV o PDF, e impresión |
| `CustomerService` | Clientes |
| `ReturnService` | Devoluciones y notas de crédito |
| `SchemaService` | *(Pendiente, para la fase 7)* Permite a los módulos registrar **sus propias migraciones** (el mecanismo por dueño ya existe; falta publicarlo) |

### 2.2 Extensión por plugins: nuevas interfaces

El diseño original de la base (`doc/arquitectura/db/test_database.graphml`) tiene muchos tipos de línea de venta:

- `ventas_item_producto`
- `ventas_item_minicarga`
- `ventas_item_activacion`
- `ventas_item_servicios_varios`
- `ventas_item_servicio_continuo`
- `western_union_transaction`
- `pagos_transacciones`

En vez de meter todos esos tipos en el core, cada sistema externo será un **módulo** que implementa estas interfaces. Ya existen `PermissionProvider` y `ReportProvider`; las otras se harán con la fase 7:

| Interfaz | Para qué |
|---|---|
| `SaleItemProvider` | Agrega un tipo de línea a la venta (carga de saldo, giro, pago de servicio). Debe validar la línea, guardar sus datos dentro de la **misma transacción** que la venta y aportar texto al comprobante. |
| `PaymentMethodProvider` | Agrega un medio de pago, como una tarjeta por POS, una transferencia, un QR o una billetera. |
| `CashBalanceProvider` | Agrega saldos que se concilian al cerrar la caja, como el saldo de billetera o de minicarga de una línea. |
| `PermissionProvider` | El módulo declara sus permisos, por ejemplo `wu.enviar` o `telefonia.activar`. |
| `ReportProvider` | El módulo aporta sus propios reportes. |

**Regla de atomicidad:** una venta se guarda entera o no se guarda. Hay que evitar que el giro quede registrado y la venta no, o al revés. Por eso los proveedores escriben dentro de la transacción que abre `SalesService`.

Todos los IID terminan en un único nivel de API (`AGUARA_API_VERSION`), que cubre también los tipos de datos compartidos. Un módulo de otro nivel se descarta antes de cargar su código, y `autoVersion.py check` falla si cambian los encabezados públicos sin subir el nivel.

### 2.3 Modelo de datos: cambios respecto del diagrama

| Tema | Diagrama actual | Propuesta |
|---|---|---|
| Stock | Columna `productos.stock` | Libro de **movimientos** (`stock_movimientos`: venta, compra, ajuste, devolución, recuento). El campo `stock` queda como saldo que se actualiza en la misma transacción. |
| Pagos de una venta | `ventas.pago_id`, un solo pago | Tabla `ventas_pagos` (venta, medio, monto, vuelto, referencia). Así se permiten pagos mixtos. |
| Venta | `fecha`, `monto`, `usuario`, `cliente` | Agregar `sesion_caja_id`, `numero`, `estado` (abierta, cobrada o anulada), `anulada_por`, `motivo` y los totales de IVA por tasa. |
| Contraseñas | `password` y `sal` | Hash PBKDF2-SHA256 con sal por usuario y muchas iteraciones. `QPasswordDigestor` de QtNetwork sirve en Qt 5.15. |
| Precios | Solo el precio actual | Historial en `precios_historial`, para auditoría y para reportes de margen correctos. |
| Auditoría | No existe | `auditoria` (usuario, acción, entidad, valores anteriores y nuevos, fecha y puesto) |

---

## 3. Fases

Tamaño estimado: **S** = días, **M** = 1 a 2 semanas, **L** = 3 semanas o más, para una persona.

### Fase 0: Base de calidad *(M)*

Sin esto, cada fase siguiente rompe algo sin que nadie se entere.

- [x] Pruebas en el repositorio: `tests/`, 10 suites de Qt Test (`make check`), integradas al `SUBDIRS`. Las filas de PostgreSQL y las pruebas de reinicio del servidor corren con `tests/run-postgresql-tests.sh`. Ver [tests/README.md](../tests/README.md).
  - Para que las pruebas y el programa usen el mismo código, el core pasó a ser una biblioteca estática (`src/core`) y `aguara.exe` se compila aparte (`src/app`).
- [x] CI en GitHub Actions (`.github/workflows/ci.yml`): compila en release con Qt 5.15.2 MinGW 32 y corre todas las pruebas con SQLite y con un servidor PostgreSQL de prueba. Debug se compila en cada máquina de desarrollo. *Pendiente: confirmar la primera ejecución en GitHub.*
- [x] Registro en `%APPDATA%\Aguara\AguaraPOS\logs\aguara.log`: rota cada 1 MB y conserva 5 archivos. Los errores técnicos van al registro y el usuario ve un mensaje en español (`src/core/errors.h`).
- [x] Contraseña de la base de datos cifrada con DPAPI de Windows (`CryptProtectData`). Las contraseñas guardadas en texto plano por versiones anteriores se cifran al iniciar.
- [x] Teclado y lector de códigos:
  - Atajos de la ventana: Ctrl+1…9, Ctrl+Tab, Ctrl+W, F1, Ctrl+I y Ctrl+Q.
  - Inventario: búsqueda por código exacto o descripción con Enter, e Ins, F2 y Supr.
  - *Importar* pasó a Ctrl+I y ahora funciona. Antes tenía Ctrl+N, pero el atajo no hacía nada porque la acción no estaba en ningún menú.
- [x] Respaldo:
  - SQLite: copia en caliente con `VACUUM INTO`, copia diaria automática (se conservan 14) y *Archivo > Copia de seguridad…*.
  - PostgreSQL: `scripts/backup-postgresql.ps1` con `pg_dump` y una guía para programarlo en la [guía de varios puestos](deploy/varios-puestos-postgresql.md).

**Hecho cuando:** un *push* compila y prueba solo, y un error de base de datos se ve como un mensaje en español con el detalle en el log.

### Fase 1: Usuarios y permisos *(M)*

Va primero porque todo lo demás necesita saber **quién** hizo cada cosa.

- [x] `UserService` (`src/interfaces/userservice.h`), con las tablas `usuarios`, `roles`, `permisos`, `rol_tiene_permisos` y `usuario_tiene_roles` (migración 2). Los permisos van por rol.
- [x] Inicio de sesión al abrir el programa, con hash PBKDF2-SHA256 y espera de un minuto tras 5 intentos fallidos. La contraseña que asigna un administrador se cambia en el primer ingreso.
- [x] Bloqueo por inactividad (`lock_minutes` en `aguara.ini`, 10 por defecto), bloqueo manual (Ctrl+L) y cambio de usuario sin cerrar el programa. El usuario se muestra en la barra lateral y en la barra de estado.
- [x] Registro de permisos: los del core más los que declara cada módulo (`PermissionProvider`). Cada página tiene un permiso `modulo.<id>` automático, y la barra lateral oculta lo que el usuario no puede abrir.
- [x] **Autorización de supervisor:** `UserService::authorize()` pide las credenciales de un usuario con permiso sin cerrar la sesión. Hoy la usan importar, respaldar, crear y modificar artículos y darlos de baja. Anular, descontar, abrir el cajón y cerrar caja con diferencia la usarán al llegar sus fases.
- [x] Roles iniciales: Administrador, Encargado y Cajero.
- [x] Asistente de primer uso: crea el administrador cuando la base no tiene usuarios.
- [x] Tabla `auditoria`, con ingresos, intentos fallidos, autorizaciones, usuarios, roles y artículos. Los servicios de las próximas fases deben registrar también lo suyo.
- [x] Módulo **Usuarios**: usuarios, roles y permisos, y consulta de la auditoría.
- [ ] *(Opcional, pendiente)* **PIN** corto para el cambio rápido de cajero.
- [ ] *(Opcional, pendiente)* Excepciones de permisos por usuario.

**Hecho cuando:** un cajero no puede entrar a Inventario ni anular una venta sin que un encargado lo autorice, y todo queda en la auditoría. *(Inventario y el mecanismo de autorización ya están hechos; anular llega con las ventas, en la fase 3.)*

### Fase 2: Inventario completo *(L)*

- [x] **Movimientos de stock** como única forma de cambiar el stock, con un kardex por artículo.
- [x] **Proveedores:** ABM, usando la tabla `proveedores` que ya existe.
- [x] **Compras:** ingreso de mercadería por proveedor con número de factura, costo por línea y actualización opcional del costo y del precio con margen sugerido.
- [x] **Ajustes** con motivo (rotura, vencimiento, robo o consumo interno), que requieren permiso.
- [x] **Recuento físico:** se carga la cantidad contada (con lector o a mano) y se generan los ajustes por diferencia. Se puede hacer por rubro.
- [x] Rubros jerárquicos (`rubros.parent_id`).
- [x] **Unidades:**
  - Artículos por unidad o por peso y volumen (`unidades_basicas`), con cantidades decimales.
  - Códigos de balanza (EAN-13 con peso o precio embebido, prefijos 20 a 29).
- [x] Varios códigos de barras por artículo, para presentaciones y proveedores distintos.
- [x] Historial de precios, cambio masivo por porcentaje (por rubro, marca o proveedor) e impresión de etiquetas de góndola.
- [x] Alertas de stock mínimo, aprovechando las notificaciones del core. Llegan en el momento, también desde la otra caja (`LowStockWatcher`), y se actualizan solas al reponer.
- [x] Stock negativo: **se permite**, con aviso y registro, para que una carga atrasada no frene la venta.

**Hecho cuando:**
- el stock de cualquier artículo se explica sumando sus movimientos;
- se puede cargar una compra completa;
- se puede hacer un recuento por rubro.

### Fase 3: Ventas y sesión de caja *(L)*

Es el corazón del sistema.

**Caja (`CashService`)**

- [x] Las **cajas** se identifican por puesto: cada PC es una caja configurada.
- [x] **Apertura:** usuario, caja, fecha y hora, y **fondo inicial** contado por denominación. Billetes: 2.000, 5.000, 10.000, 20.000, 50.000 y 100.000. Monedas: 50, 100, 500 y 1.000.
- [x] Mientras la sesión no esté abierta, no se puede vender.
- [x] **Movimientos de efectivo** con motivo y permiso: ingresos, egresos (pagos a proveedores, gastos) y **retiros parciales** (sangrías).
- [x] **Cierre:**
  1. El sistema calcula lo esperado por medio de pago: fondo + ventas en efectivo − vuelto ± movimientos ± saldos de proveedores (billeteras, minicarga, ver la fase 7).
  2. El cajero hace un **arqueo ciego**: cuenta por denominación sin ver el monto esperado.
  3. Si el conteo **coincide**, la caja se cierra.
  4. Si **no coincide**, el cierre se bloquea. Puede volver a contar, o un supervisor autoriza el cierre con la diferencia y un motivo obligatorio. Esa diferencia queda registrada y aparece en los reportes.
- [x] Planilla de cierre impresa y reimprimible, con el detalle por medio de pago, los movimientos y las diferencias.
- [x] Con la caja abierta, Ventas es la ventana principal: se abre al ingresar, queda primera y no se cierra hasta cerrar la caja (`WidgetModuleInterface::keepPageOpen()`). Sobrevive al cambio de usuario, con la venta en curso.
- [x] Con la caja abierta no se sale del programa sin cerrarla o sin autorización (`caja.salir_abierta`), tampoco desde la pantalla de bloqueo. El apagado de Windows no se impide, pero queda auditado.
- [x] Recuperación: si el programa o la PC se caen, al reabrir se retoma la sesión abierta. Una sesión olvidada de otro día solo la puede cerrar un supervisor.

**Venta (`SalesService` y pantalla de ventas)**

- [x] Pantalla de cobro pensada para teclado y lector:
  - código o búsqueda;
  - cantidad (`3*` y código);
  - cambio de cantidad y borrado de línea (con permiso, si ya está impresa);
  - subtotal y total grandes.
- [x] Venta en espera: estacionar una venta y atender a otro cliente.
- [x] Cobro en efectivo con cálculo de vuelto. Solo lo que se paga en efectivo se **redondea a 50 Gs.**, la moneda más chica: sin eso, una venta por peso no podría cuadrar en el arqueo. El redondeo queda guardado en la venta.
- [x] **Cola local de ventas** (`SaleOutbox`): cada venta va primero a un archivo SQLite en el disco del equipo y recién después a la base. Sin conexión no se inician ventas nuevas, pero la venta en curso se cobra, imprime y abre el cajón; al volver la conexión sube sola. `ventas.uuid` evita duplicados si la conexión se corta durante el COMMIT, y la caja no cierra con ventas en cola.
- [x] Guardado atómico en una sola transacción: venta, líneas, pagos, movimientos de stock y numeración. La numeración es correlativa por caja y no se repite entre puestos (contador `cajas.ultimo_numero`, dentro de la transacción de la venta: en las dos bases, y sin saltos si una venta falla).
- [x] **Anulación** de una venta de la sesión actual, con permiso y motivo. Revierte el stock y la caja.
- [x] **Descuentos** por línea o por total, en porcentaje o monto, con un límite por rol. Pasar el límite requiere supervisor.
- [x] Cálculo de IVA por línea con `vatIncluded` y totales por tasa, como pide el comprobante.

**Decisiones:**
- Una sesión es de la caja, no del usuario: cada venta guarda quién la hizo y el relevo sigue en la misma sesión.
- La venta en pantalla vive en memoria hasta cobrarla. Las ventas en espera sí se guardan y sobreviven a un reinicio.
- Quitar una línea no pide permiso mientras no haya impresión por línea, pero queda en la auditoría. El permiso llega si la fase 4 imprime líneas antes del cobro.

**Hecho cuando:** un cajero puede abrir la caja, vender todo el día con lector, anular con autorización y cerrar con un arqueo que cuadra. Ese cierre debe coincidir al guaraní con el reporte de ventas.

### Fase 4: Comprobantes e impresión *(M)*

- [x] `PrintService` con dos caminos:
  - **ESC/POS** directo a impresoras térmicas de 58 u 80 mm, por la cola de Windows en modo RAW (`WritePrinter`). Es rápido, tiene corte de papel y **pulso para abrir el cajón**.
  - **`QPrinter`**, para impresoras comunes (A4) y para exportar a PDF.
- [x] Plantilla de ticket configurable:
  - datos del comercio y RUC (el timbrado llega con la fase 8: el ticket es interno y lo aclara);
  - número de comprobante, caja, cajero, fecha y hora;
  - líneas con IVA, totales por tasa, pagos y vuelto;
  - pie de texto;
  - pendiente: texto aportado por los módulos (por ejemplo, el MTCN de un giro). Llega con `SaleItemProvider`, que todavía no existe; `PrintService::printReceipt()` ya imprime cualquier `Receipt`.
- [x] Reimpresión con la marca "COPIA", que requiere permiso. El primer ticket que sale es el original, aunque la impresora haya fallado al cobrar (`ventas.impresiones`).
- [x] Prueba de impresión desde la configuración del puesto (Preferencias > Impresora y comercio).
- [x] Cajón de dinero: se abre al cobrar en efectivo, o a mano con permiso y queda auditado.
- [ ] **Probar con una térmica y un cajón reales.** El código está probado byte por byte, pero todavía no se imprimió en una impresora de verdad: la página de códigos 850, el corte y el pulso se confirman en el hardware.
- [ ] Verificar los drivers de las impresoras elegidas en Windows 7 (Epson TM-T20, Bixolon y genéricas chinas de 80 mm).

**Hecho cuando:** cada venta imprime su ticket en una térmica de 80 mm, el cajón se abre solo y la planilla de cierre sale impresa.

### Fase 5: Medios de pago, clientes y devoluciones *(M)*

> **Redondeo:** tarjeta, transferencia y demás medios electrónicos cobran el monto exacto, al guaraní. El total de una venta es exacto y solo se redondea la parte pagada en efectivo (`AguaraSale::cashRounding()`). En un pago mixto, el efectivo es lo último que se calcula: de 15.625 con 10.000 por tarjeta, se redondean los 5.625.

- [x] **Pagos mixtos** (`ventas_pagos`): efectivo, tarjeta de débito o crédito y transferencia, con carga manual del comprobante de la terminal o del número de operación. Un código ya usado en otra venta no anulada necesita autorización (`ventas.comprobante_repetido`), sin índice único, porque las terminales pueden repetir números entre lotes. La planilla de cierre los lista para conciliar.
- [x] Pago por QR, como un medio más. La integración con el POS del procesador puede venir después, como módulo.
- [x] Medios de pago configurables (`modos_de_pago`). Todos menos el efectivo se concilian aparte: el arqueo se cuenta billete por billete, así que ningún otro medio puede contarse en él.
- [x] **Clientes:**
  - ABM con RUC o CI y verificación del dígito verificador (módulo 11).
  - Búsqueda rápida y "consumidor final" por defecto.
  - Se usan para la factura (fase 8) y la cuenta corriente.
- [x] **Devoluciones** de ventas anteriores, buscando la venta por número. La devolución puede ser parcial. Repone el stock, devuelve el dinero por el mismo medio o como nota de crédito (interna, con saldo, usable como medio de pago), y requiere permiso.
- [ ] *(Opcional)* **Cuenta corriente** ("fiado"): venta a crédito para clientes habilitados, con límite, cobro de cuotas o saldos y estado de cuenta.

### Fase 6: Reportes *(M)*

`ReportService` muestra los reportes en pantalla con filtros de fecha, caja, usuario y rubro. Se pueden imprimir y exportar a CSV o PDF, y los módulos pueden aportar los suyos.

**Ventas**
- [x] Ventas del día o de un período (totales, cantidad de tickets, ticket promedio)
- [x] Ventas por hora
- [x] Ventas por artículo, rubro, marca, cajero y medio de pago
- [x] Margen bruto por artículo y rubro, con el costo del momento de la venta

**Caja**
- [x] Historial de sesiones, con aperturas, cierres, diferencias y quién las autorizó
- [x] Movimientos de efectivo
- [x] Anulaciones y devoluciones, con su motivo y quién las autorizó

**Inventario**
- [x] Stock actual valorizado (a costo y a precio)
- [x] Artículos bajo el mínimo
- [x] Artículos sin movimiento en N días
- [x] Kardex de un artículo
- [x] Compras por proveedor
- [x] Ajustes

**Impositivo**
- [x] Resumen de IVA por tasa (10 %, 5 % y exenta) del período
- [x] Libro de ventas en borrador: comprobante por comprobante, con cliente, RUC e IVA, exportable.
- [ ] Libro de ventas en el formato que pida la SET para el registro de comprobantes. A confirmar con el contador; ver la fase 8.

**Auditoría**
- [x] Consulta de la auditoría por usuario, acción y fecha

### ★ Hito v1.0: primera versión usable

Con las **fases 0 a 6** más este hito, el sistema sirve para un comercio de venta de productos.

- [x] **Instalador**, con Inno Setup (`installer/`), y **parches** que se aplican a mano en cada equipo: programa y módulos, runtime de MinGW y Qt, `libpq` de 32 bits con sus DLL, runtime VC++ 2013 x86 y accesos directos. Se generan; falta ejecutarlos.
- [x] Probar el instalador y el programa en Windows 7 de 32 bits, el sistema del piloto.
- [x] Aplicar un parche sobre esa instalación.
- [x] Probar la tarea **Instalar el servidor PostgreSQL** en esa máquina.
- [ ] Ejecutar las pruebas contra PostgreSQL 10.23 de 32 bits, el servidor del piloto.
- [x] Versiones: `autoVersion.py` separa la versión del programa del nivel de API, con control antes de cada commit y al armar el instalador.
- [ ] Quitar la etiqueta beta (`autoVersion.py final`) y etiquetar `v1.0.0` al terminar el piloto. Una vez etiquetada una versión, cualquier cambio necesita otra.
- [x] Manual corto para el cajero y para el encargado (`doc/manual`, en PDF).
- [ ] **Piloto** de dos semanas en un comercio real, con PostgreSQL en una de las dos cajas.
- [ ] Corrección de lo que surja en el piloto antes de seguir con las fases 7 en adelante.

### Fase 7: Sistemas externos, como módulos *(L, cada uno M)*

Salen de las tablas del diagrama y de lo habitual en comercios de Paraguay. Cada uno es un módulo independiente, que se instala solo donde se usa.

**Telefonía** (`telefonia_operadoras`, `telefonia_lineas`, `ventas_item_minicarga`, `ventas_item_activacion`, `telefonia_activaciones_metas`)
- [ ] Líneas propias del comercio por operadora, con su **saldo de minicarga** y su **saldo de billetera**.
- [ ] Venta de **minicarga**: descuenta del saldo de la línea, registra la comisión y suma la línea a la venta.
- [ ] **Activaciones de chip** con número de formulario, y **metas y bonos** por período.
- [ ] **Billeteras móviles** (`billeteras_operaciones`, `billeteras_transacciones`): carga y retiro de dinero de clientes, con comisión al cliente y ganancia.
  - El efectivo entra o sale de la caja, y el saldo de la billetera se mueve en sentido contrario. Las dos cosas se concilian al cierre mediante `CashBalanceProvider`.
- [ ] Recarga de saldo de las líneas propias, como compra al operador.
- [ ] Reportes de comisiones, avance de metas y saldos.

**Cobranza de servicios** (`pagos_procesadores`, `pagos_servicios`, `pagos_transacciones`)
- [ ] Cobro de facturas de servicios (ANDE, ESSAP, telefonía, cable) por redes de cobranza, con el procesador y su comisión.
- [ ] Primera versión con **carga manual**: el pago se hace en el sistema del procesador y AguaraPOS registra el cobro, el comprobante y la comisión.
- [ ] Integración automática por API solo si el procesador la ofrece y el comercio la contrata, cada una como módulo aparte.

**Western Union** (`western_union_transaction`)
- [ ] Registro de envíos y pagos de giros: cliente (con documento), MTCN, monto, comisión y sentido del efectivo (entra al enviar, sale al pagar).
- [ ] La operación real se hace en el sistema del agente de Western Union. AguaraPOS registra la caja, el comprobante y los reportes.

**Servicios** (`servicios_simples`, `servicios_continuos`, `ventas_item_servicios_varios`, `ventas_item_servicio_continuo`)
- [ ] Servicios de una vez, como fotocopias o impresiones, con vendedor y comisión.
- [ ] **Servicios continuos** o suscripciones (por ejemplo, alquileres o internet), con período, renovación automática, avisos de vencimiento y reporte de cobros pendientes.

### Fase 8: Comprobantes fiscales *(L)*

La forma exige definirla con el contador del comercio, porque la obligación depende de cada contribuyente:

- [ ] **Factura con timbrado preimpreso o autoimpresor**:
  - numeración por establecimiento y punto de expedición (`001-001-0000001`);
  - vigencia del timbrado;
  - controles de rango.
- [ ] **Factura electrónica (SIFEN, e-Kuatia)**: es un módulo grande. Incluye:
  - XML firmado con certificado digital;
  - envío y consulta a la SET;
  - CDC;
  - KuDE impreso con QR;
  - contingencia sin conexión;
  - eventos (cancelación, inutilización).
  Conviene evaluar si se construye o si se integra un proveedor de facturación electrónica.
- [ ] Notas de crédito vinculadas a las devoluciones de la fase 5.

**Hasta resolver esto, el comprobante de la fase 4 es un ticket interno**, no fiscal. Hay que decidir pronto si el comercio piloto lo necesita para el hito v1.0.

### Fase 9: Más adelante

- **Contabilidad** (módulo previsto en el diseño original): asientos automáticos desde ventas, compras y caja, y exportación al sistema del contador.
- **Varias sucursales:** stock por depósito, transferencias entre sucursales y reportes consolidados.
- **Promociones:** combos, 2x1, precios por cantidad, listas de precios por cliente y horarios.
- **Pedidos y presupuestos.**
- **Visor de cliente** (segundo monitor o display de poste) y **balanza conectada** por puerto serie.
- **Modo sin conexión para PostgreSQL:** seguir vendiendo cuando el servidor no responde y sincronizar después. Es complejo y por ahora queda fuera, porque se aceptó que la caja se detenga si no hay servidor.

---

## 4. Orden y dependencias

```
Fase 0 ─► Fase 1 ─┬─► Fase 2 ───────────┐
                  └─► Fase 3 ─► Fase 4 ─┼─► Fase 5 ─► Fase 6 ─► ★ v1.0 ─► Fase 7 (módulos, en paralelo)
                                        │                           └──► Fase 8 (según la obligación fiscal)
```

- Las fases 2 y 3 se pueden hacer en paralelo, si se acuerda antes el contrato de `stock_movimientos`.
- La fase 8 puede adelantarse si el piloto exige factura desde el primer día.

## 5. Decisiones pendientes

| # | Pregunta | Recomendación |
|---|---|---|
| 1 | ¿Qué comprobante necesita el comercio piloto: ticket interno, factura con timbrado o factura electrónica? | **Decidido:** ticket interno por ahora. La factura sigue en la fase 8. |
| 2 | Cierre con diferencia: ¿bloquear siempre o permitir que un supervisor cierre con la diferencia registrada? | **Decidido:** con supervisor y motivo. |
| 3 | ¿Qué impresora térmica y qué cajón se van a usar? | Comprar una ESC/POS de 80 mm de marca conocida para probar desde la fase 4. |
| 4 | ¿Se permite stock negativo? | **Decidido:** sí, con aviso y registro. |
| 5 | ¿Qué sistemas externos opera realmente el comercio: operadoras, redes de cobranza, Western Union? | Hacer solo esos módulos y en ese orden. |
| 6 | ¿Hace falta cuenta corriente ("fiado") en la v1.0? | **Decidido:** no. |
| 7 | ¿Dónde corre PostgreSQL y qué sistema tienen las cajas? | **Decidido:** en una de las cajas, con Windows 7 de 32 bits, así que PostgreSQL 10.23. Las copias fuera del local quedan para más adelante. |
| 8 | ¿Cómo llegan las actualizaciones? | **Decidido:** parches que se aplican a mano en cada equipo. Un equipo sin actualizar no entra a una base más nueva. |
