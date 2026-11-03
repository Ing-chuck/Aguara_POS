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
usa productos (`ProductService`).
