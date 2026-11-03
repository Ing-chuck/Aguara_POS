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
compila todo en orden: primero `interfaces`, luego el core y los modulos.
No abrir los sub-proyectos por separado: sin el proyecto raiz no comparten la carpeta de salida.

Estructura de la salida dentro de la carpeta de compilacion:

    bin/aguara.exe
    bin/aguara_interfaces.dll
    bin/modules/*.dll          <- modulos, cargados al iniciar
    lib/                       <- solo para enlazar

Los modulos `template_SI` y `template_MI` solo se compilan en Debug.

# Base de datos
Dos opciones, elegidas en `aguara.exe --configure-database` (o automaticamente si la base de
datos no se puede abrir). La eleccion se guarda en `%APPDATA%\Aguara\AguaraPOS\aguara.ini`.

* **Un solo puesto** (por defecto): un archivo SQLite creado al iniciar, sin servidor:
  `%APPDATA%\Aguara\AguaraPOS\aguara.db`. Para usar otro archivo:
  `aguara.exe --database C:\ruta\otra.db`. Para respaldar, copiar `aguara.db` con el programa
  cerrado.
* **Varios puestos en la misma red**: un servidor PostgreSQL compartido. Ver
  [doc/deploy/varios-puestos-postgresql.md](doc/deploy/varios-puestos-postgresql.md).

* El **core** es dueño de la base de datos: crea y actualiza el esquema con migraciones
  (`src/core/database.cpp`, tabla `schema_migrations`). Un cambio de esquema se agrega como
  una migracion nueva, con su version para SQLite y para PostgreSQL; las ya publicadas no se
  modifican. Las consultas de los servicios son las mismas para ambas bases de datos.
* Los **modulos no usan SQL**: piden servicios al core (`AguaraHost::service<ProductService>()`)
  en `initialize()`. Ver `src/interfaces/aguarahost.h` y `productservice.h`.
* **Montos** en guaranies enteros (ISO 4217: PYG no tiene decimales), con IVA incluido.
  Ver `src/interfaces/money.h`.

# Como empezar un nuevo modulo en Qt Creator
Un modulo es un plugin cuyo objeto raiz implementa `AguaraModuleInterface` (descripcion del modulo)
y una o mas interfaces de capacidad (`WidgetModuleInterface`, `ImportArticlesInterface`).
Ver `src/interfaces/interfaces.h` y los templates. El `.pro` de un modulo solo necesita
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

### Utilizando el projecto template
Otra forma simple de empezar un projecto nuevo, es duplicando el projecto "template" que ya existe en la carpeta de modulos, solo debes tener quidado al renombrar loas archivos o clases, asegurandote de hacerlo en todos los archivos, incluyendo el .ui si lo necesitas.
