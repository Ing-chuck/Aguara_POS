# Aguara POS
 sistema modular de gestion para punto de venta, utilizando el framework Qt 6, diseñado con el objetivo de ser:
 
 * facil de usar
 * confiable
 * facil de expandir

# Arquitectura
El sistema se basa en la arquitectura plug-in que permite la creacion de modulos independientes para realizar diferentes tareas
### Los modulos actualmente planeados son:
* Control de inventario
* Ventas
* Usuarios y permisos

# Compilar
Requisitos: **Qt 5.15.2 con MinGW 8.1.0 32-bit** (ultima combinacion compatible con Windows 7).
En el Maintenance Tool de Qt se encuentran activando el filtro "Archive".

Abrir **`Aguara_POS.pro`** (en la raiz del repositorio) en Qt Creator y compilar. Este proyecto
compila todo en orden: primero `interfaces`, luego el core y los modulos, y al final las pruebas.
No abrir los sub-proyectos por separado: sin el proyecto raiz no comparten la carpeta de salida.

| Carpeta | Contenido |
|---|---|
| `src/interfaces` | `aguara_interfaces.dll`: lo que comparten el core y los modulos |
| `src/core` | biblioteca estatica: datos, servicios y ventana principal |
| `src/app` | `aguara.exe` (solo `main()`) |
| `src/modules` | los modulos (plugins) |
| `tests` | pruebas automaticas, ver [tests/README.md](tests/README.md) |
| `scripts` | herramientas de instalacion, por ejemplo el respaldo del servidor |

Estructura de la salida dentro de la carpeta de compilacion:

    bin/aguara.exe
    bin/aguara_interfaces.dll
    bin/modules/*.dll          <- modulos, cargados al iniciar
    lib/                       <- solo para enlazar
    tests/bin/tst_*.exe        <- pruebas (no se instalan)


**Pruebas**: `mingw32-make check` en la carpeta de compilacion, o el panel *Test Results* de
Qt Creator. Cada push las ejecuta en GitHub Actions (`.github/workflows/ci.yml`), con SQLite y con
un servidor PostgreSQL de prueba.

# Base de datos
Dos opciones, elegidas en `aguara.exe --configure-database` (o automaticamente si la base de
datos no se puede abrir). La eleccion se guarda en `%APPDATA%\Aguara\AguaraPOS\aguara.ini`.

* **Un solo puesto** (por defecto): un archivo SQLite creado al iniciar, sin servidor:
  `%APPDATA%\Aguara\AguaraPOS\aguara.db`. Para usar otro archivo:
  `aguara.exe --database C:\ruta\otra.db`.
  * Al iniciar se hace una **copia de seguridad diaria** automatica en
    `%APPDATA%\Aguara\AguaraPOS\backups`, y se conservan las 14 mas nuevas.
  * **Archivo > Copia de seguridad...** guarda una copia donde se elija, por ejemplo en un pendrive,
    sin cerrar el programa.
  * Para restaurar, cerrar AguaraPOS y reemplazar `aguara.db` por la copia. Antes, borrar
    `aguara.db-wal` y `aguara.db-shm` si existen.
* **Varios puestos en la misma red**: un servidor PostgreSQL compartido. Ver
  [doc/deploy/varios-puestos-postgresql.md](doc/deploy/varios-puestos-postgresql.md). Los
  respaldos se hacen en el servidor (`scripts/backup-postgresql.ps1`).

* El **core** es dueño de la base de datos: crea y actualiza el esquema con migraciones
  (`src/core/database.cpp`, tabla `schema_migrations`). Un cambio de esquema se agrega como
  una migracion nueva, con su version para SQLite y para PostgreSQL; las ya publicadas no se
  modifican. Las consultas de los servicios son las mismas para ambas bases de datos.
* Los **modulos no usan SQL**: piden servicios al core (`AguaraHost::service<ProductService>()`)
  en `initialize()`. Ver `src/interfaces/aguarahost.h` y `productservice.h`.
* **Montos** en guaranies enteros (ISO 4217: PYG no tiene decimales), con IVA incluido.
  Ver `src/interfaces/money.h`.

# Usuarios y permisos
La primera vez que se abre el programa pide crear el administrador; despues, siempre pide usuario
y contraseña. Se administran en el modulo **Usuarios** (usuarios, roles y permisos, auditoria).

* **Roles**: Administrador (todo, siempre), Encargado y Cajero, y los que se creen. Cada usuario
  tiene uno o mas. Los cambios en un rol valen desde el proximo ingreso de cada usuario.
* **Contraseñas**: se guardan como hash PBKDF2-SHA256 (`src/core/passwordhash.h`). La que asigna un
  administrador se debe cambiar en el primer ingreso. Tras 5 intentos fallidos, la cuenta espera un
  minuto.
* **Sesion**: *Sesion > Bloquear* (Ctrl+L) o el candado de la barra lateral. Se bloquea sola tras
  10 minutos sin actividad; se cambia con `lock_minutes` en la seccion `[session]` de `aguara.ini`
  (0: nunca). Otro usuario puede tomar la caja desde la pantalla de bloqueo.
* **Auditoria**: quien hizo que, cuando y desde que puesto, con los valores de antes y despues. En
  PostgreSQL la fecha es la del servidor. Ingresos, intentos fallidos, autorizaciones, usuarios,
  roles y articulos ya quedan registrados.

**En un modulo**: la pagina de un modulo necesita el permiso `modulo.<id>`, que se crea solo
(Encargado si, Cajero no). Para permisos propios, implementar `PermissionProvider`:

    QList<PermissionInfo> permissions() const override {
        return { { "caja.anular", "Anular ventas", "Caja" } };      // Encargado si, Cajero no
    }

Y antes de una accion sensible, pedir la autorizacion. Si el usuario no tiene el permiso, un
supervisor escribe sus credenciales ahi mismo, sin cerrar la sesion:

    if (!users->authorize("caja.anular", tr("Anular la venta %1").arg(numero), this))
        return;

`host->service<UserService>()` da el servicio; `can()` sirve para ocultar o deshabilitar cosas y
`audit()` para registrar lo que hizo el modulo.

# Inventario

El stock solo cambia con un **movimiento**: saldo inicial, compra, venta, devolucion, ajuste o
recuento. Cada uno guarda cantidad, saldo, usuario, fecha y documento, asi que el stock de un
articulo siempre se explica sumando su kardex (`Mas > Historial`). Por eso el stock no se edita
en la ficha del articulo: se usa `Mas > Ajustar stock`. `StockLedger` es el unico que escribe
`productos.stock`, y lo hace en la misma transaccion que el documento.

El stock **puede quedar negativo**: el sistema avisa y lo registra, para que una compra cargada
tarde no frene la venta.

**Cantidades:** `Quantity` guarda milesimas en un entero (`AguaraQuantity::whole(3)` son 3 unidades,
`1250` son 1,250 kg). Nunca `double`, igual que `Money`. Al escribir, la coma es siempre decimal y
el punto con tres cifras separa miles: `2,75` y `1.500`.

**Codigos de balanza:** un EAN-13 con prefijo 20 a 29 se lee como `PPPPPPPVVVVVC`.

| Prefijo | `PPPPPPP` | `VVVVV` | Ejemplo |
|---|---|---|---|
| 20 a 27 | codigo del articulo, 7 digitos | peso en gramos | `2012345` y `01250` son 1,250 kg |
| 28 y 29 | codigo del articulo, 7 digitos | precio en guaranies | `2854321` y `25000` son Gs. 25.000 |

El digito verificador `C` se comprueba: un codigo mal leido no se acepta. Este formato es el mas
comun, pero cada balanza se configura distinto: **confirmarlo con la balanza real** antes de usarla.

**Permisos:** ademas de `inventario.editar` y `inventario.baja`, estan `inventario.ajustar`,
`inventario.compras`, `inventario.recuento`, `inventario.precios` e `inventario.proveedores`.

**En un modulo**, `host->service<InventoryService>()` da compras, ajustes, proveedores y
`resolveScan()`, que convierte lo que manda el lector (codigo, codigo alternativo o balanza) en
articulo, cantidad y precio.

# Caja y ventas

Cada computadora es una **caja**: se crea sola la primera vez y se le cambia el nombre en
`Caja > Nombre de esta caja`. Sin una sesion abierta no se puede vender. La sesion esta en la base
de datos, asi que si el programa o la PC se caen, al volver sigue abierta. Una sesion de otro dia
frena las ventas hasta que un supervisor la cierre.

**Teclado de la pantalla de cobro** (el cursor siempre queda en el campo del lector):

| Tecla | Accion |
|---|---|
| Enter | Agrega el codigo escrito. `3*codigo` agrega tres; un texto busca por descripcion |
| Flechas | Eligen la linea |
| F2 | Cantidad de la linea |
| F3 | Buscar articulo |
| F4 / F5 | Descuento de la linea / de la venta, en porcentaje o monto |
| F6 / F7 | Poner en espera / recuperar |
| F12 | Cobrar |
| Supr | Quitar la linea |
| Esc | Borrar lo escrito; sin nada escrito, cancelar la venta |

Un codigo que no existe avisa en la misma pantalla, sin abrir un dialogo que se coma el proximo
codigo. Vender mas de lo que hay avisa y sigue (ver *Inventario*).

**Una venta se guarda entera o no se guarda:** venta, lineas, pago, movimientos de stock y numero van
en una sola transaccion. La numeracion es correlativa por caja, sin saltos ni repetidos entre puestos.
El IVA se calcula linea por linea y el descuento de la venta se reparte entre las lineas de modo que
las partes sumen exacto. El total de una venta es **exacto**: solo lo que se paga en efectivo se
redondea a 50 Gs., la moneda mas chica, y ese redondeo queda guardado. Sin eso, una venta por peso no
podria cuadrar en el arqueo.

**Tarjeta y transferencia:** se cobran fuera del programa, en la terminal del banco o en la cuenta, y
AguaraPOS no se conecta con ellas: en `Cobrar` (F12), F2 debito, F3 credito o F4 transferencia piden el
codigo del comprobante y el monto, que por defecto es lo que falta. Cobran el monto exacto, al guarani,
y nunca dan vuelto. Se pueden combinar entre si y con efectivo: el efectivo se calcula al final y solo
esa parte se redondea. Un codigo que ya usa otra venta no anulada necesita autorizacion
(`ventas.comprobante_repetido`, queda en la auditoria), porque las terminales pueden repetir numeros
de un lote a otro. No entran en el arqueo: la planilla de cierre los lista en *Comprobantes por
conciliar*, para compararlos con el cierre de lote de la terminal y con el extracto. Anular una venta
saca de la caja solo su efectivo; el programa recuerda que la tarjeta o transferencia se revierte aparte.

**Cierre:** el cajero cuenta por denominacion sin ver lo esperado (fondo + ventas en efectivo ±
movimientos). Si coincide al guarani, la caja se cierra. Si no, solo se le dice que no coincide,
nunca por cuanto: vuelve a contar, o un supervisor ve las cifras y cierra con la diferencia y un motivo
obligatorio. Cada intento fallido queda en la auditoria. La planilla de cierre se reimprime en
`Caja > Planillas anteriores`.

**Permisos:** `ventas.anular`, `ventas.descuento_libre`, `caja.abrir`, `caja.cerrar`,
`caja.movimientos`, `caja.cerrar_diferencia`, `caja.cerrar_olvidada`, `caja.planillas` y
`caja.configurar`. El descuento maximo sin supervisor se define por rol en la pantalla de roles
(de fabrica: Cajero 5 %, Encargado 20 %) y vale tambien para la suma de los descuentos de una venta.

**En un modulo:** `host->service<SalesService>()` y `host->service<CashService>()`.

# Apariencia
El tema esta en `aguara_interfaces.dll` (`src/interfaces/aguarastyle.h`), asi que el core y todos
los modulos se ven igual:
* Estilo Fusion, que se ve igual en Windows 7, 10 y 11.
* Fuente Segoe UI.
* Fondo claro, barra lateral oscura y un solo color de acento (oxido, el del aguara guazu) para la
  accion principal, la seleccion y el foco.
* Contraste de texto de al menos 4,5:1.

Los colores salen de `AguaraStyle::color()`; el estilo esta en
`src/interfaces/theme/style/aguara.qss`.

Un modulo **no define colores propios**: le da un *rol* a cada widget con
`AguaraStyle::setRole(widget, AguaraStyle::Primary)`, o en Qt Designer con una propiedad dinamica
`role` de tipo texto.

| Rol | Para |
|---|---|
| `primary` | la accion principal de la pagina o del dialogo (una sola) |
| `danger` | acciones que borran o dan de baja |
| `flat` | botones sin borde (iconos) |
| `title` / `subtitle` / `muted` / `section` | textos |
| `error` / `warning` / `info` / `success` | carteles de aviso |
| `pageHeader` (en un `QFrame`) | la franja blanca de arriba de cada pagina, con titulo y acciones |
| `card` (en un `QFrame`) | una caja blanca con borde |

* El boton por defecto de un dialogo ya se ve como `primary`.
* `AguaraStyle::searchField(lineEdit)` arma una caja de busqueda.
* `AguaraStyle::icon("box", color)` da los iconos del tema. Estan en
  `src/interfaces/theme/icons`, son de linea y se pintan del color pedido.
* En `moduleInfo()`, `info.iconName = "box"` hace que el core dibuje el icono del modulo con el
  color de cada lugar: barra lateral, pestañas, dialogos. `"aguara"` (la cabeza del zorro) es el
  icono generico que usan los modulos nuevos del wizard.
* El inventario (`src/modules/inventory_manager`) sirve de ejemplo de pagina completa.

Para instalar: ademas de las DLL de Qt habituales, el programa usa **Qt5Svg**, **Qt5Network** y los
plugins `imageformats/qsvg` e `iconengines/qsvgicon`, que `windeployqt` copia solo.

# Registro y errores
* El usuario ve mensajes cortos en español (`src/core/errors.h`).
* El detalle tecnico, como errores de SQL, del driver o de los modulos, queda en el registro
  `%APPDATA%\Aguara\AguaraPOS\logs\aguara.log`.
  * Se rota cada 1 MB y se conservan 5 archivos. La ruta aparece en *Ayuda > Acerca de*.
  * Con `AGUARA_DEBUG=1` incluye tambien los mensajes de depuracion.
* En el codigo, usar las categorias de `src/core/logging.h`, por ejemplo
  `qCWarning(lcDatabase) << ...`.

# Teclado
Todo se puede hacer sin mouse. El lector de codigos de barras funciona como un teclado: escribe
el codigo y presiona Enter.

| Atajo | Accion |
|---|---|
| Ctrl+1 ... Ctrl+9 | abrir el modulo N de la barra lateral |
| Ctrl+Tab / Ctrl+Shift+Tab | pagina siguiente / anterior |
| Ctrl+W | cerrar la pagina actual |
| F1 | avisos |
| Ctrl+I | importar articulos |
| Ctrl+L / Ctrl+Shift+L | bloquear la sesion / cambiar de usuario |
| Ctrl+Q | salir |
| Inventario: Ctrl+F o F3 | buscar por codigo o descripcion; Enter selecciona el articulo |
| Inventario: Ins o Ctrl+N | articulo nuevo |
| Inventario: F2 o Enter | editar el articulo |
| Inventario: Supr | dar de baja el articulo |

# Como empezar un nuevo modulo en Qt Creator
Un modulo es un plugin cuyo objeto raiz implementa `AguaraModuleInterface` (descripcion del modulo)
y una o mas interfaces de capacidad (`WidgetModuleInterface`, `ImportArticlesInterface`).
Ver `src/interfaces/interfaces.h` y los modulos existentes. El `.pro` de un modulo solo necesita
`TARGET`, sus fuentes, e `include(../module.pri)`; ademas hay que agregarlo a `src/modules/modules.pro`.

### Utilizando el wizard
La forma mas facil de empezar un modulo nuevo es utilizando el wizard personalizado "aguaramodule". Este wizard se encunetra en un archivo rar dentro de este repositorio.
Para utilizarlo, simplemente se debe extraer el contenido del rar dentro de una de las siguientes ubicaciones
* Carpeta shared dentro de la instalacion de Qt Creator:
  * En Windows: share\qtcreator\templates\wizards
  * En Linux: share/qtcreator/templates/wizards
  * En macOS: Qt Creator.app/Contents/Resources/templates/wizards
* user settings local del usuario:
  * En Windows: %APPDATA%\QtProject\qtcreator\templates\wizards
  * En Linux y macOS: $HOME/.config/QtProject/qtcreator/templates/wizards

Reiniciar Qt Creator. Luego, en el arbol del proyecto, clic derecho sobre el proyecto `modules` >
**New Subproject...** > **AguaraPOS Module**: crea el modulo en `src/modules` y lo agrega a
`modules.pro`. El wizard permite elegir si el modulo admite varias paginas abiertas a la vez y si
usa productos (`ProductService`). La pagina generada ya trae el encabezado estandar (titulo,
subtitulo y boton principal) y el icono generico del tema, sin imagenes propias.
