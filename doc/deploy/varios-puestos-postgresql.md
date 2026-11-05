# Varios puestos con datos compartidos (PostgreSQL)

Con un solo puesto, AguaraPOS guarda los datos en un archivo SQLite local y no requiere nada
mas. Para que **varios puestos de la misma red local** compartan productos (y luego ventas),
los datos se guardan en un **servidor PostgreSQL** al que se conectan todos los puestos.

> No compartir el archivo SQLite por una carpeta de red: SQLite no es confiable sobre
> recursos compartidos y la base de datos puede corromperse (ver
> https://www.sqlite.org/useovernet.html).

Si el servidor o la red se caen, un puesto abierto termina y cobra la venta en pantalla, que queda en
su cola local y sube sola al volver la conexion. No se pueden iniciar ventas nuevas, y un puesto cerrado
no puede entrar hasta que vuelva el servidor.

## 1. Servidor

El servidor corre en **una de las cajas**, con IP fija (ej. `192.168.1.10`). Es la primera que se
enciende y la ultima que se apaga: sin ella, las demas cajas no pueden entrar al programa.

Esa caja tiene Windows 7 de 32 bits, asi que el servidor es **PostgreSQL 10.23 de 32 bits**, la ultima
version publicada para ese sistema. Ya no recibe correcciones de seguridad, igual que Windows 7: esa
caja y la red del local **no deben quedar expuestas a Internet**. Con un equipo de 64 bits conviene una
version con soporte vigente; el servidor tambien puede ser Linux. Los puestos solo necesitan el cliente
(paso 2).

1. Instalar PostgreSQL y anotar la contraseña del usuario `postgres`.
   En PostgreSQL 10, antes de crear el usuario, poner en `postgresql.conf`
   `password_encryption = scram-sha-256` y reiniciar el servicio. Si no, la contraseña queda en md5 y
   la regla del paso 3 rechaza a todos los puestos.
2. Crear el usuario y la base de datos de AguaraPOS (con `psql -U postgres` o pgAdmin):

   ```sql
   CREATE ROLE aguara LOGIN PASSWORD 'elegir-una-contraseña';
   CREATE DATABASE aguara OWNER aguara ENCODING 'UTF8' TEMPLATE template0;
   ```

   No hace falta crear tablas: AguaraPOS crea y actualiza el esquema al conectarse.
3. Permitir conexiones desde la red. En `postgresql.conf`:

   ```
   listen_addresses = '*'
   ```

   y al final de `pg_hba.conf` (ajustar la red a la del local):

   ```
   host    aguara    aguara    192.168.1.0/24    scram-sha-256
   ```

   Reiniciar el servicio de PostgreSQL.
4. Abrir el puerto TCP 5432 en el firewall del servidor, solo para la red local.
5. **Respaldos**: copiar `scripts\backup-postgresql.ps1` al servidor (por ejemplo a
   `C:\AguaraPOS\`). El script ejecuta `pg_dump`, deja las copias en una carpeta y conserva las
   30 más nuevas.
   - La contraseña la lee `pg_dump` de `%APPDATA%\postgresql\pgpass.conf` del usuario de Windows
     que ejecuta la tarea. Una línea: `localhost:5432:aguara:aguara:CONTRASEÑA`.
   - Programar la tarea diaria, como ese usuario:

         schtasks /Create /TN "AguaraPOS backup" /SC DAILY /ST 23:00 /TR "powershell -NoProfile -ExecutionPolicy Bypass -File C:\AguaraPOS\backup-postgresql.ps1 -Destination D:\AguaraBackups"

   - Probarla una vez con `schtasks /Run /TN "AguaraPOS backup"` y revisar que aparezca el
     archivo `aguara-AAAAMMDD-HHMMSS.dump`.
   - **Copiar también los respaldos a otro disco u otro equipo**: una copia en el mismo disco se
     pierde con ese disco. Mientras no haya copia fuera del local, copiarlos cada semana a un pendrive
     o disco externo que no quede enchufado a la caja.
   - Restaurar, con AguaraPOS cerrado en todos los puestos:
     `pg_restore --clean --if-exists -h localhost -U aguara -d aguara D:\AguaraBackups\aguara-....dump`

## 2. Cada puesto

1. Ejecutar `aguarapos-X.Y.Z.exe` como administrador. Necesita Windows 7 SP1 o mas nuevo. Instala el
   programa, los modulos, el cliente PostgreSQL de 32 bits y el runtime de Visual C++ que este
   necesita: no hay que copiar nada mas. La caja del servidor lo instala como las demas.
2. Abrir **Preferencias > Conexion a los datos** como administrador (o iniciar `aguara.exe --configure-database`), elegir **Servidor en la red**, completar
   servidor, puerto, base de datos, usuario y contraseña, y **Probar conexion**.
   La configuracion se guarda en `%APPDATA%\Aguara\AguaraPOS\aguara.ini`.
   La contraseña queda **cifrada** con DPAPI de Windows: solo el mismo usuario de Windows en el
   mismo equipo puede leerla. Si se copia `aguara.ini` a otro equipo o usuario, hay que volver a
   escribir la contraseña. (Las versiones anteriores la guardaban en texto plano; al iniciar se
   cifra sola.)

   Si se deja la contraseña vacia, se usa el archivo estandar de PostgreSQL
   `%APPDATA%\postgresql\pgpass.conf` (linea `192.168.1.10:5432:aguara:aguara:contraseña`),
   que queda en texto plano en el perfil del usuario de Windows.

Cuando un puesto modifica productos o su stock, los demas actualizan sus listas automaticamente, y el
aviso de stock bajo llega en el momento a quien pueda abrir Inventario en cualquiera de los puestos.

## Actualizar con un parche

Un parche se aplica a mano, equipo por equipo, con `aguarapos-parche-X.Y.Z.exe`. Cambia los programas,
no toca los datos ni la cola de ventas.

1. Fuera del horario de venta, **cerrar la caja y salir de AguaraPOS en todos los equipos**.
2. Hacer una copia de seguridad con la tarea del paso 1 del servidor.
3. Aplicar el parche en la caja del servidor y abrir AguaraPOS: actualiza la base de datos.
4. Aplicar el parche en las demas cajas.

Un equipo sin actualizar **no puede entrar**: avisa que los datos son de una version mas nueva, y sus
ventas en cola esperan a salvo hasta que reciba el parche. Si una caja se actualizo por error antes
que las demas, no hay daño: solo hay que aplicar el parche al resto.

## Para desarrolladores

Sin instalador, por ejemplo al probar una compilacion en otro equipo, AguaraPOS necesita el **cliente
PostgreSQL de 32 bits**: la ultima version publicada para Windows de 32 bits es la 10.x, compatible con
Windows 7 y con servidores PostgreSQL actuales.

1. Descargar `postgresql-10.23-1-windows-binaries.zip`
   (https://get.enterprisedb.com/postgresql/postgresql-10.23-1-windows-binaries.zip) y copiar
   de su carpeta `pgsql\bin` al lado de `aguara.exe`:
   `libpq.dll`, `libssl-1_1.dll`, `libcrypto-1_1.dll`, `libintl-8.dll`, `libiconv-2.dll`.
2. Instalar el **Visual C++ 2013 Redistributable (x86)** de Microsoft (lo requiere `libpq.dll`).

Para que la compilacion copie las DLL del cliente junto a `aguara.exe`, definir la variable
`AGUARA_LIBPQ_DIR` (variable de entorno, o argumento de qmake en Qt Creator) con la carpeta
`pgsql\bin` del zip de PostgreSQL 10.x de 32 bits.
