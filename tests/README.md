# Pruebas automáticas

Pruebas con **Qt Test**, una por ejecutable, que se compilan con el resto del proyecto (`tests` es
un subproyecto de `Aguara_POS.pro`). Los ejecutables quedan en `<compilación>/tests/bin`.

| Suite | Qué prueba |
|---|---|
| `tst_money` | Montos en guaraníes: lo que escribe el cajero, formato e IVA incluido |
| `tst_csvreader` | Lectura de CSV: comillas (RFC 4180), separador, UTF-8 y Windows-1252 |
| `tst_logging` | Archivo de registro: niveles, categorías y rotación |
| `tst_settings` | `aguara.ini`: valores guardados y contraseña cifrada (DPAPI), nunca en texto plano |
| `tst_backup` | Copia de seguridad de SQLite: copia en caliente, copia diaria y retención |
| `tst_database` | Apertura y migraciones *(SQLite y PostgreSQL)* |
| `tst_productservice` | `ProductService`, lo que usan los módulos *(SQLite y PostgreSQL)* |
| `tst_modules` | Los plugins reales: importación de CSV, y teclado y lector de códigos en inventario |
| `tst_reconnect` | Varias cajas en un servidor: avisos entre cajas, reinicio y caída del servidor *(solo PostgreSQL)* |
| `tst_mainwindow` | Ventana principal: avisos, reloj, menús y atajos |

## Ejecutar

- **Qt Creator**: panel *Test Results*, o *Herramientas > Pruebas > Ejecutar todas*. También se
  puede ejecutar cada suite como cualquier programa.
- **Línea de comandos**, en la carpeta de compilación:
  - `mingw32-make check` ejecuta todas las suites.
  - `tests/run-tests.sh <carpeta de compilación>` las ejecuta, guarda los resultados en archivos
    (`test-results/*.txt` y JUnit `*.xml`) y muestra un resumen. Si una consola no muestra la
    salida de Qt Test, los archivos siempre la tienen.
- **Sin las pruebas**: `qmake CONFIG+=no_tests` no las compila.

## PostgreSQL

Sin configuración, las filas `postgresql` y `tst_reconnect` se **omiten** (aparecen como SKIP,
no como error). Para incluirlas hace falta un servidor de prueba. Hay dos opciones:

**Automático, recomendado.** El script crea un servidor descartable en `localhost:54321`, corre
todas las suites y lo detiene al terminar:

    PG_BIN=/c/ruta/a/pgsql/bin BUILD_DIR=<carpeta de compilación> \
    PG_CLIENT_BIN=/c/ruta/a/pgsql10-32bits/bin  tests/run-postgresql-tests.sh

- `PG_BIN` apunta a los programas del servidor (`initdb`, `pg_ctl`, `psql`), de cualquier versión
  desde la 10. Pueden ser de 64 bits.
- `PG_CLIENT_BIN` apunta a las DLL del **cliente de 32 bits**, las mismas que usa cada puesto
  (ver [doc/deploy/varios-puestos-postgresql.md](../doc/deploy/varios-puestos-postgresql.md)).

**Manual, con un servidor propio.** Definir estas variables de entorno:

| Variable | Ejemplo |
|---|---|
| `AGUARA_TEST_PG_HOST` | `localhost` |
| `AGUARA_TEST_PG_PORT` | `5432` |
| `AGUARA_TEST_PG_USER` | `aguara` |
| `AGUARA_TEST_PG_PASSWORD` | `...` |
| `AGUARA_TEST_PG_CTL` *(solo `tst_reconnect`)* | `C:\Program Files\PostgreSQL\17\bin\pg_ctl.exe` |
| `AGUARA_TEST_PG_DATA` *(solo `tst_reconnect`)* | la carpeta de datos de ese servidor |

> ⚠️ Las pruebas usan **solamente** una base llamada `aguara_test` y **borran todo su contenido**.
> `tst_reconnect` además **detiene y reinicia** el servidor. Nunca apuntarlas a un servidor con
> datos reales.

## Escribir una prueba nueva

- Agregar una carpeta `tst_<nombre>/` con su `.pro` y agregarla a `tests.pro`:

      TARGET = tst_nombre
      include(../core_test.pri)     # o ../test.pri si no usa el core
      SOURCES += tst_nombre.cpp

- `common/testsupport.h` arma una base de datos migrada y vacía con los servicios encima, igual
  que `main()` (`TestSupport::CoreFixture`).
- Para que cada prueba corra en SQLite y en PostgreSQL, usar `addBackendRows()` en
  `initTestCase_data()` y abrir la base con `AGUARA_OPEN_FIXTURE(fixture, carpeta)`.
- Los módulos se cargan desde la carpeta `AGUARA_TEST_MODULES_DIR`, que es la de los plugins
  compilados.
