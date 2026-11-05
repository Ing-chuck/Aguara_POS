# Pruebas automáticas

Pruebas con **Qt Test**, una por ejecutable, que se compilan con el resto del proyecto (`tests` es
un subproyecto de `Aguara_POS.pro`). Los ejecutables quedan en `<compilación>/tests/bin`.

| Suite | Qué prueba |
|---|---|
| `tst_money` | Montos en guaraníes, cantidades con decimales, EAN-13 y códigos de balanza |
| `tst_csvreader` | Lectura de CSV: comillas (RFC 4180), separador, UTF-8 y Windows-1252 |
| `tst_logging` | Archivo de registro: niveles, categorías y rotación |
| `tst_settings` | `aguara.ini`: valores guardados y contraseña cifrada (DPAPI), nunca en texto plano |
| `tst_backup` | Copia de seguridad de SQLite: copia en caliente, copia diaria y retención |
| `tst_database` | Apertura y migraciones *(SQLite y PostgreSQL)* |
| `tst_productservice` | `ProductService`, lo que usan los módulos *(SQLite y PostgreSQL)* |
| `tst_users` | Usuarios, roles, permisos, autorización de supervisor y auditoría *(SQLite y PostgreSQL)* |
| `tst_inventory` | Movimientos, compras, recuentos, precios, proveedores y lector *(SQLite y PostgreSQL)* |
| `tst_print` | Tickets ESC/POS byte por byte, impresoras comunes, copias y cajón, sin imprimir de verdad *(SQLite y PostgreSQL)* |
| `tst_customers` | Clientes, RUC con su dígito verificador y cédula *(SQLite y PostgreSQL)* |
| `tst_returns` | Devoluciones, notas de crédito y medios de pago, con su efecto en la caja y el stock *(SQLite y PostgreSQL)* |
| `tst_reports` | Los reportes, sus totales y la exportación a CSV *(SQLite y PostgreSQL)* |
| `tst_lowstock` | El aviso de stock bajo sigue al stock: ventas, compras, ajustes, recuentos, devoluciones, sin conexión *(SQLite y PostgreSQL)* |
| `tst_sales` | Caja y ventas: totales exactos, sesión, numeración, anulación, arqueo y cierre *(SQLite y PostgreSQL)* |
| `tst_modules` | Los plugins reales: importación de CSV, inventario, compra y recuento por pantalla, etiquetas, autorización, usuarios y un día de caja por pantalla |
| `tst_reconnect` | Varias cajas en un servidor: avisos entre cajas, reinicio y caída del servidor *(solo PostgreSQL)* |
| `tst_mainwindow` | Ventana principal: avisos, reloj, menús, atajos, y lo que ve cada usuario, con los módulos reales. Con los módulos de `fixtures/`, que un módulo de otra API no llega a ejecutarse y que uno que no abre se avisa |
| `test_autoversion.py` | `autoVersion.py` sobre un repositorio temporal. Lo ejecuta `run-tests.sh`, junto con `autoVersion.py check` |

## Ejecutar

- **Qt Creator**: panel *Test Results*, o *Herramientas > Pruebas > Ejecutar todas*. También se
  puede ejecutar cada suite como cualquier programa.
- **Línea de comandos**, en la carpeta de compilación:
  - `mingw32-make check` ejecuta todas las suites.
  - `tests/run-tests.sh <carpeta de compilación>` las ejecuta, guarda los resultados en archivos
    (`test-results/*.txt` y JUnit `*.xml`) y muestra un resumen. Si una consola no muestra la
    salida de Qt Test, los archivos siempre la tienen. Al final ejecuta `autoVersion.py check` y
    `test_autoversion.py`. Sin Python 3 los omite con un mensaje, salvo en CI, donde fallan.
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
- `fixtures/` tiene módulos que no deben arrancar, para probar la carga: `old_api` declara una API
  vieja y deja rastro si su código se ejecuta, y `wont_load` tiene la API correcta pero Windows se
  niega a cargarlo. Se compilan en `tests/fixture_modules`, fuera de la carpeta de los módulos reales.
- En una ventana real, Windows decide quién tiene el foco. Una prueba que dependa de cambiar de
  ventana activa debe correr solo con `QT_QPA_PLATFORM=offscreen`, como en CI. Conviene correr también
  sin esa variable: hay carreras de activación que solo aparecen en la plataforma real.
