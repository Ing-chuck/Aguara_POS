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

# Como empezar un nuevo modulo en Qt Creator
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
