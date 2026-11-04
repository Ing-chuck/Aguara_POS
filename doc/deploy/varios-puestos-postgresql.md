# Varios puestos con datos compartidos (PostgreSQL)

Con un solo puesto, AguaraPOS guarda los datos en un archivo SQLite local y no requiere nada
mas. Para que **varios puestos de la misma red local** compartan productos (y luego ventas),
los datos se guardan en un **servidor PostgreSQL** al que se conectan todos los puestos.

> No compartir el archivo SQLite por una carpeta de red: SQLite no es confiable sobre
> recursos compartidos y la base de datos puede corromperse (ver
> https://www.sqlite.org/useovernet.html).

Si el servidor o la red se caen, los puestos no pueden operar hasta que se restablezca la
conexion (reiniciar AguaraPOS en cada puesto).

## 1. Servidor

Un equipo de la red que quede siempre encendido, con IP fija (ej. `192.168.1.10`).
Se recomienda un equipo de 64 bits con una version de PostgreSQL con soporte vigente
(https://www.postgresql.org/download/windows/). El servidor puede ser Windows o Linux; los
puestos solo necesitan el cliente (paso 2).

1. Instalar PostgreSQL y anotar la contraseña del usuario `postgres`.
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
     pierde con ese disco.
   - Restaurar, con AguaraPOS cerrado en todos los puestos:
     `pg_restore --clean --if-exists -h localhost -U aguara -d aguara D:\AguaraBackups\aguara-....dump`

## 2. Cada puesto

AguaraPOS es de 32 bits, asi que necesita el **cliente PostgreSQL de 32 bits**. La ultima
version publicada para Windows de 32 bits es PostgreSQL 10.x (compatible con Windows 7 y con
servidores PostgreSQL actuales):

1. Descargar `postgresql-10.23-1-windows-binaries.zip`
   (https://get.enterprisedb.com/postgresql/postgresql-10.23-1-windows-binaries.zip) y copiar
   de su carpeta `pgsql\bin` al lado de `aguara.exe`:
   `libpq.dll`, `libssl-1_1.dll`, `libcrypto-1_1.dll`, `libintl-8.dll`, `libiconv-2.dll`.
2. Instalar el **Visual C++ 2013 Redistributable (x86)** de Microsoft (lo requiere `libpq.dll`).
3. Abrir **Preferencias > Conexion a los datos** como administrador (o iniciar `aguara.exe --configure-database`), elegir **Servidor en la red**, completar
   servidor, puerto, base de datos, usuario y contraseña, y **Probar conexion**.
   La configuracion se guarda en `%APPDATA%\Aguara\AguaraPOS\aguara.ini`.
   La contraseña queda **cifrada** con DPAPI de Windows: solo el mismo usuario de Windows en el
   mismo equipo puede leerla. Si se copia `aguara.ini` a otro equipo o usuario, hay que volver a
   escribir la contraseña. (Las versiones anteriores la guardaban en texto plano; al iniciar se
   cifra sola.)

   Si se deja la contraseña vacia, se usa el archivo estandar de PostgreSQL
   `%APPDATA%\postgresql\pgpass.conf` (linea `192.168.1.10:5432:aguara:aguara:contraseña`),
   que queda en texto plano en el perfil del usuario de Windows.

Cuando un puesto modifica productos, los demas actualizan sus listas automaticamente.

## Para desarrolladores

Para que la compilacion copie las DLL del cliente junto a `aguara.exe`, definir la variable
`AGUARA_LIBPQ_DIR` (variable de entorno, o argumento de qmake en Qt Creator) con la carpeta
`pgsql\bin` del zip de PostgreSQL 10.x de 32 bits.
