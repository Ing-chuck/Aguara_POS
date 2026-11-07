# Aguara POS
 sistema modular de gestion para punto de venta, utilizando el framework Qt 5.15, diseñado con el objetivo de ser:
 
 * facil de usar
 * confiable
 * facil de expandir

# Arquitectura
El sistema se basa en la arquitectura plug-in que permite la creacion de modulos independientes para realizar diferentes tareas
### Los modulos son:

| Modulo | Archivo | Para que |
|---|---|---|
| Inventario | `modulo_control_inventario.dll` | articulos, compras, proveedores y recuentos |
| Clientes | `modulo_customers.dll` | clientes, con RUC o cedula |
| Importar | `modulo_importar.dll` | importar articulos, clientes y proveedores desde CSV (sin pagina propia) |
| Ventas | `modulo_sales_module.dll` | la caja: vender, cobrar, cerrar |
| Reportes | `modulo_stats.dll` | reportes para ver, imprimir y exportar |
| Usuarios | `modulo_usuarios.dll` | usuarios, roles, permisos y auditoria |
| Promociones | `modulo_ventas_promociones.dll` | descuentos con fecha sobre los articulos elegidos |

Se cargan en orden alfabetico de archivo, y ese orden da los atajos Ctrl+1, Ctrl+2...: por eso
Reportes se llama `modulo_stats`, para que Ventas siga en Ctrl+3, y Promociones va ultimo.

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
| `scripts` | el respaldo del servidor, y `find-python.sh`, que busca Python 3 para los demas scripts |
| `installer` | el instalador y los parches (Inno Setup) |
| `doc` | manuales (`manual`), instalacion en red (`deploy`), diagramas (`arquitectura`, `gui`) y hoja de ruta (`roadmap.md`) |
| `.githooks` | el control de la API antes de cada commit, ver [Versiones](#versiones) |

Estructura de la salida dentro de la carpeta de compilacion:

    bin/aguara.exe
    bin/aguara_interfaces.dll
    bin/modules/*.dll          <- modulos, cargados al iniciar
    lib/                       <- solo para enlazar
    tests/bin/tst_*.exe        <- pruebas (no se instalan)


**Instalador**: `installer/build.sh CARPETA_RELEASE PGSQL_BIN` arma el instalador, y con `patch` al final,
el parche, en `installer/output/`. `PGSQL_BIN` es la carpeta `pgsql/bin` del zip de PostgreSQL 10 de 32
bits, con `bin`, `lib` y `share` descomprimidas: de ahi sale tambien el servidor. Necesita Qt y MinGW en el PATH, Python 3 e Inno Setup 6: si `ISCC.exe` no esta en el PATH, indicar
su ruta en la variable `ISCC`. Sin Inno Setup deja lista la carpeta `stage` y termina. Primero copia lo
que hace falta en un puesto: Qt, el runtime de MinGW, el cliente de PostgreSQL con sus DLL y el runtime
de Visual C++ 2013 (`msvcr120.dll` y `msvcp120.dll`). Estas dos las toma de `installer/redist/` o, si no
estan, de Windows: en un equipo sin ellas, ponerlas en esa carpeta.

El **servidor PostgreSQL** es una tarea opcional del instalador, para la caja que guarda los datos. Va en su
propia carpeta (`stage-pg`) sin pgAdmin, simbolos ni documentacion, y `installer/pg-setup.cmd` lo prepara. Los
parches no lo llevan. Al ser codigo que el instalador no prueba por si solo, revisar esta tarea en una
maquina limpia cada vez que se cambie.

El parche reemplaza todos los programas y modulos, no toca los datos y comprueba que AguaraPOS este
instalado. Como aplicarlo: [doc/deploy/varios-puestos-postgresql.md](doc/deploy/varios-puestos-postgresql.md).

**Manuales**: `python doc/manual/build.py` arma los PDF desde los `.html` (necesita PyMuPDF). Revisar
el numero de paginas: una frase de mas puede dejar una pagina casi vacia.

**Diagramas**: `python doc/arquitectura/build.py ruta/a/una.db` dibuja los de `doc/arquitectura` y `doc/gui`,
en SVG y PNG (necesita PyMuPDF). Lee las tablas de esa base SQLite y las interfaces de los modulos desde el
codigo: sirve cualquier base migrada, por ejemplo una creada con `aguara.exe --database prueba.db`. Una tabla
nueva hay que ubicarla en `GROUPS`, dentro del script: sin eso no termina. `db/test_database.graphml` es el
diseño original y no se genera: conserva las tablas previstas para la fase 7.

**Pruebas**: `mingw32-make check` en la carpeta de compilacion, o el panel *Test Results* de
Qt Creator. Cada push las ejecuta en GitHub Actions (`.github/workflows/ci.yml`), con SQLite y con
un servidor PostgreSQL de prueba.

# Versiones

Hay dos numeros, y `autoVersion.py` se encarga de ambos (solo necesita Python 3 y git). Ojo: el MinGW
8.1 de Qt trae un Python 2 llamado `python`; si aparece primero en el PATH, usar `python3`.

**Version del programa** (`src/interfaces/version.h`), `MAYOR.MENOR.PARCHE`: la que ve la gente y lleva
el instalador. Cambia solo a pedido, nunca por compilar. Parche: correcciones. Menor: funciones nuevas.
Mayor: un cambio que hay que planificar con el comercio. Una version todavia no definitiva lleva una
etiqueta, como `1.0.0-beta`: `pre` la pone, `bump` la conserva y `final` la quita. Cada beta publicada
con cambios necesita otra etiqueta (`pre beta.2`), porque dos programas no pueden compartir version.

**Nivel de API** (`src/interfaces/apiversion.h`): un solo numero contra el que se comprueban los modulos.
Todos los IID terminan en el. Un modulo compilado con otro nivel no pasa el control de sus metadatos y se
descarta, con un aviso, sin cargar su codigo. Cubre todo `src/interfaces`, tambien los tipos de datos que
pasan entre el core y los modulos. El archivo guarda una huella de los encabezados publicos, sin contar
comentarios ni espacios: si cambian, `check` falla hasta subir el nivel. Subirlo solo cuesta recompilar,
porque el instalador y cada parche llevan todos los modulos.

    python autoVersion.py                  estado, no cambia nada
    python autoVersion.py check            falla si la API cambio sin subir el nivel
    python autoVersion.py api              sube el nivel de API
    python autoVersion.py bump patch       o minor, o major
    python autoVersion.py pre beta         pone una etiqueta: X.Y.Z-beta
    python autoVersion.py final            quita la etiqueta
    python autoVersion.py tag              etiqueta el commit con vX.Y.Z, sin hacer push

Para publicar: `bump`, commit, compilar, `installer/build.sh`, probar en una caja real, `tag` y push.
La etiqueta va al final: hasta entonces se puede corregir y recompilar con el mismo numero, y despues
cualquier cambio necesita una version nueva. `build.sh` usa `check --release`: no compila si la API no
esta al dia, ni si esa version ya esta etiquetada sobre otro codigo.

Para que git frene un commit que cambia la API sin subir el nivel, activar una vez en cada clon el hook
de `.githooks`: `git config core.hooksPath .githooks`.

# Base de datos
Dos opciones, elegidas en **Preferencias > Conexion a los datos** (permiso `datos.conexion`, solo
administradores salvo que un rol lo reciba), en `aguara.exe --configure-database` o automaticamente si
la base de datos no se puede abrir. La eleccion se guarda en `%APPDATA%\Aguara\AguaraPOS\aguara.ini`.
Desde el menu solo se guarda una conexion que funciona, y se aplica al reiniciar el programa, que
ofrece hacerlo en el momento. **Los datos no se copian de una base a otra.**

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

### Unidades de venta
`unidades_basicas` se administra desde la ficha del articulo. `removeUnit()` solo oculta la unidad (`activo = 0`) y no quita `Unidad` (id 1) ni una unidad que use un articulo activo. Las ventas guardan la unidad como texto, asi que no dependen de esta tabla. Sus ids se escriben a mano: la tabla no tiene secuencia.

### Lotes y vencimiento
Un lote es un articulo y una fecha (`lotes`, una fila por par). Guarda solo el stock con fecha: el resto de `productos.stock` no tiene fecha, asi que no hay un saldo que pueda desincronizarse. Todo pasa por `StockLedger::move()`:

| Movimiento | Lotes |
|---|---|
| Venta | deja la fecha vigente mas proxima, y despues el stock sin fecha. Un lote vencido queda retenido: solo cede si no hay otra cosa |
| Otra salida (ajuste, recuento) | deja la fecha mas proxima, vencida o no, y lo baja de `inicial`: una devolucion no lo rellena |
| Entra con fecha (alta, saldo inicial) | suma al lote de esa fecha |
| Devolucion o anulacion | rellena el lote mas proximo con lugar: `inicial` es lo que entro, y asi se deshace lo que tomo la venta |
| Otra entrada (ajuste, recuento) | queda sin fecha |

Si el stock estaba bajo cero, lo que entra cubre primero eso: los lotes nunca suman mas que el stock. La cantidad de cada lote es una estimacion, porque el escaner no distingue lotes. `Product::expires` es la fecha mas proxima, solo de lectura; `Product::stockExpires` fecha, al guardar, el stock que no tiene fecha.

`ExpiryWatcher` mantiene dos avisos y mira los lotes cuando cambia un articulo y una vez por hora, porque una caja puede quedar abierta de noche:

| Aviso | Nivel | Cuando |
|---|---|---|
| `vencidos` | `PriorityVeryHigh` (rojo) | la fecha ya paso |
| `por-vencer` | `PriorityHigh` (advertencia) | vence dentro de `inventario.dias_aviso_vencimiento` dias (30 por defecto, en `configuracion`) |

Solo cuentan los lotes con stock de articulos activos, y solo los ve quien puede abrir Inventario. El ultimo dia todavia es vigente. En la caja, `addScan()` muestra `ExpiryAlertDialog` si el articulo tiene un lote vencido, porque el codigo de barras no dice de que lote es la unidad. **OK** sigue la venta, y **Dar de baja** pregunta cuantos descartar: propone el menor entre lo vencido y lo escaneado, admite hasta lo escaneado, pide `ventas.baja_vencido` (un supervisor, para el Cajero), hace un ajuste por ese numero y deja el resto en la venta. Si no se autoriza, vuelve la alerta. El dialogo no responde a Enter ni a Esc, porque el escaner termina cada codigo con Enter. Sin conexion no pregunta: la venta sigue.

# Promociones
Un descuento con fechas (`promociones` y `promocion_reglas`, migracion 11). `PromotionService` es del core y la caja lo usa directamente; `modulo_ventas_promociones` es solo la pagina para administrarlas, con el permiso `promociones.editar`. Lo que decide es codigo puro en `AguaraPromotion` (`src/interfaces/promotionservice.h`), sin base de datos.

| Tipo | `tipo` | Lo que descuenta |
|---|---|---|
| Porcentaje | `porcentaje` | sobre el bruto de la linea, tambien el de una etiqueta de balanza |
| Importe menos | `monto` | `valor` por unidad |
| Precio | `precio` | hasta dejar cada unidad en `valor`. Si ya cuesta menos, nada |
| Lleva y paga | `lleva_paga` | las unidades de regalo de cada juego completo |

**Cuando:** `runsOn()`: activa, entre `desde` y `hasta` inclusive, en un dia de `dias` (bit 0 lunes). `minimo` es por linea.

**A que articulos**, en este orden (`covers()`):

1. Lo elegido: `todos`, o las reglas que incluyen. Las de articulo siempre suman. Las de rubro y de marca suman entre si, salvo `rubro_y_marca`, que pide las dos. Un rubro lleva sus subrubros.
2. `invertir` da vuelta lo elegido.
3. Las reglas `excluir` sacan lo que sea: tienen la ultima palabra.

**En la caja:** `SalesModule::applyPromotion()` corre al escanear, al cambiar la cantidad y al recuperar una venta en espera, y elige con `best()` la que mas descuenta. No se suman; entre iguales gana la mas vieja. El descuento va en `SaleLine::promotionDiscount`, aparte de `discount`: no usa el limite del rol, y los descuentos manuales se calculan sobre `linePromoted()`. Sin conexion, la linea se recalcula con las ofertas ya leidas. `ventas_items` guarda el id, el nombre y el descuento, asi que la venta no cambia si la promocion cambia despues; `descuento` sigue siendo el total de la linea. La caja no revalida la promocion al cobrar: vale lo que se mostro en pantalla.

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

**Con la caja abierta, Ventas es la ventana principal:** la pagina se abre sola al ingresar, queda
primera y no se puede cerrar hasta cerrar la caja, tambien si la sesion viene de otro dia. Se puede
seguir usando el resto de las paginas. Al cambiar de usuario sigue abierta, con la venta en curso,
siempre que el nuevo usuario pueda usar Ventas.

**Con la caja abierta no se sale del programa:** Salir, la X, Alt+F4 y Ctrl+Q piden cerrar la caja o la
autorizacion de un supervisor (`caja.salir_abierta`, que queda en la auditoria). Desde la pantalla de
bloqueo no se puede salir: hay que ingresar primero, porque quien bloqueo sigue con su sesion y sus
permisos. Si Windows se apaga, el programa no lo impide, pero queda en la auditoria
(`caja.salida_windows`): la sesion y la cola de ventas estan a salvo y todo vuelve al iniciar. Sin
conexion, un supervisor no puede autorizar, porque validar su clave necesita el servidor.

**Tickets y cajon:** en **Preferencias > Impresora y comercio** se elige la impresora de cada equipo
(se guarda en su `aguara.ini`) y los datos del comercio (en la base, compartidos entre cajas). Una
termica recibe ESC/POS directo por la cola de Windows en modo RAW: 80 mm (48 columnas) o 58 mm (32),
corte de papel y pulso al cajon conectado a la impresora. Una impresora comun imprime una pagina por
su driver, sin cajon. Cada venta imprime su ticket, y el cobro con efectivo abre el cajon; ambas cosas
se pueden desactivar. **Si la impresora falla, la venta queda cobrada igual** y la pantalla lo avisa.
El primer ticket de una venta es el original, aunque salga mas tarde; los siguientes dicen `COPIA`,
piden `ventas.reimprimir` y quedan en la auditoria (`Caja > Ventas de la sesion`, tambien en PDF).
Abrir el cajon a mano (`Caja > Abrir cajon`) pide `caja.abrir_cajon` y queda en la auditoria. Con una
termica configurada, la planilla de cierre sale en el ticket; si no, en una pagina con vista previa.
El ticket es **interno** y lo dice ("No valido como factura"): no lleva timbrado hasta la fase 8.

**Ninguna venta se pierde:** cada venta se escribe primero en una cola en el disco de este equipo
(`%APPDATA%\Aguara\AguaraPOS\ventas-pendientes-aguara.db`, un archivo SQLite con escritura forzada) y
recien despues se envia a la base; al confirmarse, sale de la cola. Un corte de luz, un cierre
inesperado o una caida de la red pueden dejar una venta en la cola, nunca perderla. Sin conexion **no se
pueden iniciar ventas nuevas**: la venta en pantalla se termina de cobrar, con su numero, ticket y cajon,
y espera en la cola. Cada equipo es su propia caja, asi que sigue su numeracion sin chocar con otro. Al
volver la conexion las ventas suben solas, en orden, con su fecha y su usuario, y descuentan el stock en
ese momento. Cada venta tiene un `uuid`: subirla dos veces la guarda una sola vez, aunque la conexion se
corte justo en el COMMIT. La caja **no cierra** con ventas en cola, porque el esperado no coincidiria.
`Caja > Ventas por subir` muestra lo que espera y, si la base rechazo alguna, por que; esa queda en la
cola, sin bloquear a las demas, y se puede reintentar. Mientras no hay conexion no se bloquea la sesion
por inactividad: para volver a entrar haria falta el servidor. Tampoco se puede anular ni reimprimir una
venta que siga en la cola.

**Medios de pago:** efectivo siempre, y en `Caja > Medios de pago` (`caja.medios_pago`) los demas: debito,
credito, transferencia y QR vienen cargados, y se agregan otros (Tigo Money, cheque). Cada uno indica si
pide codigo. En `Cobrar` toman las teclas F2, F3, F4 y F6 a F10, en orden. Todos se concilian aparte: en el
arqueo solo se cuenta efectivo, porque se cuenta billete por billete. Una venta ya cobrada conserva su
medio aunque despues se saque de uso.

**Clientes:** pagina propia, con RUC (verifica el digito, modulo 11 de la SET) o cedula; un documento no se
repite y los clientes se dan de baja, no se borran. En la venta, F8 elige el cliente o crea uno; sin elegir,
es consumidor final. El ticket lo muestra, y la venta guarda su nombre y documento de ese dia.

**Devoluciones:** `Caja > Devolucion` (`ventas.devolver`) busca la venta por numero, de cualquier caja o dia.
Cada linea devuelve su parte de lo cobrado, con descuentos, y la ultima unidad lo que falta: una venta
devuelta en partes suma exacto. El dinero sale por un medio de la venta, hasta lo que entro por el, o como
nota de credito. Vuelve el stock al costo de la venta, sale de la sesion actual e imprime su ticket. Necesita
conexion y la caja abierta. La venta con devoluciones ya no se puede anular. Una **nota de credito** es
interna (la fiscal es de la fase 8): su numero `NC-000001` se usa como medio de pago, tambien en parte, hasta
agotar el saldo; anular esa venta devuelve el saldo. Sin conexion no se usa, porque no se ve su saldo.

**Reportes:** pagina propia para ventas (por dia, hora, articulo, rubro, marca, cajero y medio de pago, y
margen con el costo del momento de la venta), caja (sesiones, movimientos, anulaciones y devoluciones),
inventario (stock valorizado, bajo el minimo, sin movimiento, kardex, compras, ajustes), IVA, un libro de
ventas en borrador y la auditoria. Filtros por fechas, caja, usuario y rubro segun el reporte; cada uno se
imprime y se exporta a PDF o a CSV para Excel en espanol. *Ventas por dia* coincide al guarani con las
planillas de cierre. Las devoluciones cuentan el dia que ocurrieron. Costos y margenes piden
`reportes.costos`, la auditoria `reportes.auditoria`, y una sesion abierta no muestra lo esperado, para no
romper el arqueo a ciegas. Un modulo aporta sus reportes con `ReportProvider`.

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

Ademas de las DLL de Qt habituales, el programa usa **Qt5Svg**, **Qt5Network** y los plugins
`imageformats/qsvg` e `iconengines/qsvgicon`. `installer/build.sh` los incluye con `windeployqt`.

# Avisos
La pagina **Avisos** junta las notificaciones del programa y de los modulos. Siempre existe: cerrar su
pestaña solo la oculta, y los avisos siguen llegando.

* **Punto amarillo:** la campana de la barra lateral lo muestra si hay avisos sin ver. Visto significa
  Avisos al frente de la ventana activa, y ver los avisos no los descarta. Si el programa tapa Avisos al
  iniciar sesion, porque Ventas debe quedar abierta, lo que habia en ella cuenta como no visto.
* **Cerrar la pestaña:** solo se puede mientras una pagina deba quedar abierta. Si no, es la pagina a
  la que vuelve la ventana: nunca queda vacia.
* **Stock bajo:** `LowStockWatcher` (`src/core`) mira la lista de articulos en su minimo cada vez que
  `SqlProductService` refresca sus listas, y eso ocurre con cualquier cambio de stock, en esta caja o en
  otra. Avisa solo si la lista cambia: un articulo nuevo en la lista es un aviso nuevo, si quedan menos
  el texto se actualiza sin marcarlo como nuevo, y si no queda ninguno el aviso desaparece. Sin
  conexion no hace nada: no poder consultar no significa que se haya repuesto. Lo ve quien puede abrir
  Inventario.

**En un modulo:** `AguaraWidget::notify()` desde una pagina, o `host->notify()` si no tiene pagina. El
remitente y el `id` identifican el aviso: repetirlos actualiza el mismo, sin agregar otro. Un dato que
cambia se avisa cuando cambia, no al iniciar el programa, y solo si hay algo nuevo que decir: `CoreHost`
ofrece ademas `updateNotification()`, que no lo marca como nuevo ni lo trae de vuelta si fue descartado,
y `dismissNotification()`, para retirarlo. Estas dos son del core, no de `AguaraHost`.

**Todo cambio de stock debe avisar:** despues del commit, `SqlProductService::refreshAfterChange()` o,
donde no se tiene ese servicio, `SqlSalesService::stockChanged()`. Eso refresca las listas y, con
PostgreSQL, avisa a las demas cajas con `NOTIFY`.

# Registro y errores
* El usuario ve mensajes cortos en español (`src/core/errors.h`).
* El detalle tecnico, como errores de SQL, del driver o de los modulos, queda en el registro
  `%APPDATA%\Aguara\AguaraPOS\logs\aguara.log`.
  * Se rota cada 1 MB y se conservan 5 archivos. La ruta aparece en *Ayuda > Acerca de*.
  * Con `AGUARA_DEBUG=1` incluye tambien los mensajes de depuracion.
* En el codigo, usar las categorias de `src/core/logging.h`, por ejemplo
  `qCWarning(lcDatabase) << ...`.

# Registros locales

Siempre activos, y no salen del equipo. Estan en `%APPDATA%\Aguara\AguaraPOS\logs`, y **Ayuda > Carpeta de registros** abre esa carpeta.

- `aguara.log`: errores tecnicos, ver [Registro y errores](#registro-y-errores).
- `actividad-AAAA-MM.log` (`src/core/activitylog.cpp`): hora y nombre de cada evento, un mes por archivo, los ultimos 12. Pasa por `Telemetry::count()`, el mismo filtro de la telemetria: solo identificadores simples. El detalle de quien hizo que esta en la auditoria, en la base.
- `crashes\crash-<id>.json`: lo que deja `CrashHandler`, activo aunque la telemetria este apagada. Si se envian, quedan como `.enviado`. Se guardan los ultimos 50.
- `programa-en-uso`: existe mientras el programa corre. Si sigue ahi al arrancar, la vez anterior no termino bien, y queda `programa.cierre_inesperado` en la actividad.

# Telemetria

Apagada hasta que un administrador la activa en **Preferencias > Telemetría**, equipo por equipo. Envía a un servidor propio el uso de módulos y funciones, los cierres inesperados y los reportes de **Ayuda > Reportar un problema**. El servidor es un proyecto aparte, `Aguara_Telemetry`, y se instala en un equipo Linux.

- `src/core/telemetry.cpp`: `build()` es el único lugar que decide qué sale del equipo, y **Ver lo que se envía** muestra exactamente eso.
- Los servicios llaman a `Telemetry::count("evento")`. Sin telemetría activa no hace nada. Un nombre que no sea un identificador simple se descarta, así que un texto no puede pasar como evento.
- Casi todo el uso sale del registro de auditoría, pero solo el nombre de la acción: nunca su detalle, que puede nombrar clientes o montos.
- `src/core/crashhandler.cpp`: deja un archivo al caerse el programa y se envía al próximo inicio. Solo código de error y lugares `modulo+0xdesplazamiento`: nada de memoria ni de registros, que pueden tener datos reales.
- `installer/build.sh` saca los simbolos a `installer/output/aguarapos-simbolos-<version>.json.gz` y limpia los binarios: el instalador no los lleva. Por eso el enlace de release no usa `-s`, y `bin/` conserva los simbolos.
- `scripts/upload-symbols.py` los carga en el servidor, que muestra funciones en vez de direcciones. Guarde ese archivo con cada build que llegue a una caja: un build sin simbolos queda en direcciones para siempre.
- Cada build se reconoce por la marca de tiempo PE de cada modulo, no por la version. `strip` la borra y `scripts/export-symbols.py` la repone.

# Importar
Archivo > Importar carga un CSV de Excel, separado por `,` o `;`, en UTF-8 o Windows-1252. Las columnas se reconocen por su nombre, sin importar acentos, mayusculas ni espacios, y las desconocidas se ignoran. Una celda vacia conserva lo guardado. Las lineas invalidas se saltan y el aviso final las nombra; un error de la base de datos no guarda nada.

| Archivo | Columnas | Permiso | Se reconoce por |
|---|---|---|---|
| Articulos | Codigo*, Descripcion*, Marca, Rubro, PrecioVenta o Precio, Costo, Stock, StockMin, IVA, F_Compra, Vencimiento | `articulos.importar` | codigo |
| Clientes | Nombre*, Cliente o Razon Social; RUC; Cedula o CI; Documento y Tipo Documento; Direccion; Telefono o Celular; Correo o Email | `clientes.importar` | documento; sin documento, nombre |
| Proveedores | Nombre*, Proveedor o Razon Social; Direccion; Telefono o Celular; Correo o Email; Contacto; Web; Notas u Observaciones | `inventario.proveedores` | nombre |

- `clientes.importar` es aparte de `clientes.editar`, que tiene el Cajero: un archivo puede cambiar toda la lista.
- Un cliente sin documento solo se actualiza si un unico cliente activo tiene ese nombre; si hay varios, la linea se salta.
- Un documento nunca se asigna por el nombre: una linea con un documento desconocido crea un cliente nuevo.
- Un cliente dado de baja se actualiza por su documento y sigue de baja.
- Los proveedores no tienen RUC en la base: una columna RUC se ignora.

# Teclado
Todo se puede hacer sin mouse. El lector de codigos de barras funciona como un teclado: escribe
el codigo y presiona Enter.

| Atajo | Accion |
|---|---|
| Ctrl+1 ... Ctrl+9 | abrir el modulo N de la barra lateral |
| Ctrl+Tab / Ctrl+Shift+Tab | pagina siguiente / anterior |
| Ctrl+W | cerrar la pagina actual, salvo la que debe quedar abierta (Ventas con la caja abierta) |
| F1 | abrir Avisos, tambien si su pestaña estaba cerrada |
| Ctrl+I | importar articulos (clientes y proveedores, en Archivo > Importar) |
| Ctrl+L / Ctrl+Shift+L | bloquear la sesion / cambiar de usuario |
| Ctrl+Q | salir |
| Inventario: Ctrl+F o F3 | buscar por codigo o descripcion; Enter selecciona el articulo |
| Inventario: Ins o Ctrl+N | articulo nuevo |
| Inventario: F2 o Enter | editar el articulo |
| Inventario: Supr | dar de baja el articulo |

# Como empezar un nuevo modulo en Qt Creator
Un modulo es un plugin cuyo objeto raiz implementa `AguaraModuleInterface` (descripcion del modulo)
y una o mas interfaces de capacidad: `WidgetModuleInterface` (una pagina), `ImportInterface` (importar archivos),
`PermissionProvider` (sus permisos) y `ReportProvider` (sus reportes).
Ver `src/interfaces/interfaces.h` y los modulos existentes. El `.pro` de un modulo solo necesita
`TARGET`, sus fuentes, e `include(../module.pri)`; ademas hay que agregarlo a `src/modules/modules.pro`.

Una pagina que debe seguir abierta responde `true` en `keepPageOpen()` y emite `keepOpenChanged()`
cuando la respuesta cambia: el core la pone primera y le quita la X.

**Compatibilidad:** un modulo solo carga con el nivel de API con el que se compilo (ver
[Versiones](#versiones)). Uno que no carga no detiene el programa: el core lo salta y lo muestra en
Avisos. Si se cambia un encabezado de `src/interfaces`, subir el nivel con `python autoVersion.py api`.

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
