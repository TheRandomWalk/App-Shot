# App-Shot

App-Shot es una herramienta de línea de comandos para Windows que captura una ventana de una aplicación abierta y la guarda como PNG. Permite redimensionar la ventana antes de capturarla para obtener imágenes con dimensiones específicas, útiles para documentación, informes o comparaciones de interfaces.

La aplicación se adapta al nuevo tamaño antes de la captura: **no se reescala la imagen después**. Se captura una sola ventana, no todo el escritorio ni un vídeo.

El ejecutable se llama `window-shot.exe`.

## Qué permite hacer

- Listar las ventanas visibles con su índice, PID, ejecutable y título.
- Seleccionar una ventana por índice, PID o parte de su título.
- Capturarla con su tamaño actual o solicitar un tamaño de salida en píxeles.
- Guardar el resultado en un archivo PNG, por defecto `screenshot.png`.
- Capturar sin incluir otras ventanas superpuestas, solicitando a la ventana seleccionada que dibuje su propio contenido mediante la API Win32 `PrintWindow`.

## Requisitos y compilación

- Windows 10 u 11 con una sesión de escritorio.
- CMake 3.20 o posterior.
- Visual Studio o Build Tools con la carga de trabajo **Desarrollo para el escritorio con C++** y el Windows SDK; se requiere soporte de C++20.

Desde PowerShell, en la carpeta del proyecto:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Con el generador de Visual Studio, el ejecutable queda en `build\Release\window-shot.exe`. La captura utiliza Win32, GDI+, GDI y DWM; no requiere bibliotecas externas.

## Uso

Abre primero la aplicación cuya ventana quieres capturar.

```powershell
# Listar las ventanas disponibles
.\build\Release\window-shot.exe --list

# Capturar una ventana por parte de su título, sin redimensionarla
.\build\Release\window-shot.exe --title "Mi aplicación" --out captura.png

# Redimensionar la ventana para obtener un PNG de 1280 × 720 píxeles
.\build\Release\window-shot.exe --title "Mi aplicación" --size 1280x720 --out captura-720p.png

# Seleccionar por el índice mostrado en --list
.\build\Release\window-shot.exe --index 2 --out ventana.png

# Seleccionar por PID; sustituir 1234 por el PID de la ventana
.\build\Release\window-shot.exe --pid 1234 --out ventana.png

# Combinar PID y título para distinguir ventanas del mismo proceso
.\build\Release\window-shot.exe --pid 1234 --title "Documento" --out documento.png
```

### Opciones

| Opción | Comportamiento |
| --- | --- |
| `--list` | Muestra las ventanas visibles con título, excluyendo ventanas secundarias y de herramientas. No realiza una captura. |
| `--index N` | Selecciona una fila de la lista, empezando en 1. La lista se ordena por ejecutable, título y PID; los índices pueden cambiar si se abren o cierran ventanas. Tiene prioridad sobre los otros selectores. |
| `--pid PID` | Busca el PID completo o una subcadena numérica del PID. Puede combinarse con `--title`. |
| `--title TEXT` | Busca una parte del título, sin distinguir mayúsculas y minúsculas. |
| `--size WIDTHxHEIGHT` | Solicita las dimensiones finales del PNG, en píxeles. Cambia el tamaño real de la ventana antes de capturarla. |
| `--out PATH` | Ruta del PNG de salida. Si se omite, escribe `screenshot.png` en el directorio de trabajo. Usa comillas si la ruta contiene espacios. |

Se necesita un selector salvo al usar `--list`. Si no hay coincidencias o hay varias, la herramienta muestra las ventanas pertinentes y termina sin capturar; usa un selector más específico.

## Tamaño y comportamiento de la captura

- La imagen conserva el borde superior, incluida la barra de título cuando exista, y recorta **8 píxeles a izquierda, derecha y abajo**. No es una captura exclusiva del área de contenido.
- Para `--size 1280x720`, se solicita una ventana exterior de **1296 × 728 píxeles** para compensar ese recorte. Sin `--size`, el PNG mide 16 píxeles menos de ancho y 8 menos de alto que el rectángulo exterior de la ventana.
- Si la ventana ya tiene el tamaño exterior solicitado, no se redimensiona ni se fuerza un repintado. Si cambia de tamaño, se solicita un repintado y se espera brevemente antes de capturar.
- La ventana debe estar abierta. Si está minimizada, se restaura antes de capturarla.
- El cambio de tamaño afecta a la aplicación real y **no se revierte** después de la captura.

## Limitaciones

- Algunas aplicaciones imponen tamaños mínimos o no admiten redimensionamiento; en esos casos, el PNG puede no tener las dimensiones solicitadas.
- El recorte es fijo: no se calcula individualmente según el tema, los bordes o el escalado de cada ventana.
- Ventanas con contenido protegido, renderizado exclusivo o ciertas tecnologías de aceleración gráfica pueden fallar o producir una captura incompleta con `PrintWindow`. No se garantiza compatibilidad con todas las aplicaciones.
- La carpeta de salida debe existir y permitir escritura. Un archivo existente en la ruta de salida puede sobrescribirse.

### Códigos de salida

| Código | Significado |
| --- | --- |
| `0` | Lista mostrada o captura guardada. |
| `1` | Fallo al inicializar GDI+. |
| `2` | Argumentos inválidos o falta un selector. |
| `3` | Ninguna ventana coincide con el selector. |
| `4` | Fallo al capturar o guardar el PNG. |
| `5` | Varias ventanas coinciden con el selector. |
