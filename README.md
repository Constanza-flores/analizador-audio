# Analizador de audio en tiempo real

Captura el micrófono y muestra tres vistas en una ventana: la señal en el tiempo, el espectro por bandas y el espectrograma.

Además:

- **Detector de nota:** muestra arriba a la derecha la nota que suena (por ejemplo `LA4 440 Hz`).
- **Juego de afinación:** pide una nota y hay que cantarla o silbarla, en cualquier octava, hasta centrar el indicador. Cada partida dura 45 segundos y guarda el récord.
- **Captura:** guarda una imagen PNG de la ventana para llevarse "la foto de su voz".

Todo el programa está en un único archivo, `analizador_audio.cpp`.

## Requisitos

| Qué | Detalle |
|---|---|
| Compilador | C++17 (`g++` o `clang++`). En Windows, MinGW-w64 de 64 bits |
| Librería | SDL2 (2.x). Es la única dependencia |
| Hardware | Un micrófono u otro dispositivo de captura de audio |

Probado en Windows 11 con `g++` 14.2 (MSYS2 UCRT64) y SDL2 2.32.10. Las instrucciones de Linux y macOS son las estándar de SDL2, pero no se han probado con este proyecto.

## Windows

### Opción A: usar la SDL2 incluida en `deps/`

Sirve si el proyecto trae la carpeta `deps/`. Solo hay que instalar el compilador.

**1. Instalar el compilador (solo la primera vez)**

1. Descarga e instala MSYS2 desde <https://www.msys2.org> en la ruta por defecto (`C:\msys64`).
2. Abre la terminal **MSYS2 UCRT64** desde el menú Inicio y ejecuta:

   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```

3. Añade el compilador al PATH. En PowerShell:

   ```powershell
   [Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path','User') + ';C:\msys64\ucrt64\bin', 'User')
   ```

4. Cierra y vuelve a abrir PowerShell. Comprueba que funciona:

   ```powershell
   g++ --version
   ```

**2. Compilar**

En PowerShell, dentro de la carpeta del proyecto:

```powershell
g++ -O2 -std=c++17 analizador_audio.cpp -o analizador.exe "-Ideps/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2" "-Ldeps/SDL2-2.32.10/x86_64-w64-mingw32/lib" -lmingw32 -lSDL2main -lSDL2
```

Las comillas son necesarias: sin ellas PowerShell corta las rutas en el punto de `2.32.10` y el compilador no encuentra `SDL.h`.

**3. Copiar la DLL junto al ejecutable**

```powershell
Copy-Item deps\SDL2-2.32.10\x86_64-w64-mingw32\bin\SDL2.dll .
```

**4. Ejecutar**

```powershell
.\analizador.exe
```

#### Variante: todo desde la terminal MSYS2 UCRT64

No requiere tocar el PATH de Windows ni usar PowerShell.

1. Abre **MSYS2 UCRT64** (el prompt debe decir `UCRT64`, no `MSYS`) y ejecuta:

   ```bash
   pacman -Syu
   pacman -S mingw-w64-ucrt-x86_64-gcc
   g++ --version
   ```

   Si `pacman -Syu` pide cerrar la terminal, ábrela de nuevo y repítelo hasta que no queden actualizaciones.

2. Ve a la carpeta del proyecto (las unidades de Windows están en `/c/`, `/d/`, etc.):

   ```bash
   cd /c/ruta/al/proyecto
   ```

3. Compila. Se enlazan de forma estática solo `libgcc` y `libstdc++`; SDL2 se queda como DLL:

   ```bash
   g++ -O2 -std=c++17 -static-libgcc -static-libstdc++ analizador_audio.cpp -o analizador.exe \
     -Ideps/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2 \
     -Ldeps/SDL2-2.32.10/x86_64-w64-mingw32/lib \
     -lmingw32 -lSDL2main -lSDL2
   ```

4. Comprueba que `SDL2.dll` está junto al `.exe` (`ls SDL2.dll`). Si falta:

   ```bash
   cp deps/SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll .
   ```

5. Activa el permiso de micrófono (Configuración → Privacidad y seguridad → Micrófono → aplicaciones de escritorio) y ejecuta:

   ```bash
   ./analizador.exe        # o ./analizador.exe N para elegir dispositivo
   ```

#### Si falta la carpeta `deps/`

1. Descarga `SDL2-devel-2.32.10-mingw.zip` desde <https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10>.
2. Crea una carpeta `deps` en el proyecto y extrae el zip dentro, de modo que quede `deps\SDL2-2.32.10\x86_64-w64-mingw32\`.

Si usas otra versión de SDL2, cambia `2.32.10` en los comandos anteriores.

### Opción B: instalar SDL2 con MSYS2

No necesita la carpeta `deps/`. En la terminal **MSYS2 UCRT64**:

```bash
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
cd /c/ruta/al/proyecto
g++ -O2 -std=c++17 analizador_audio.cpp -o analizador.exe $(sdl2-config --cflags --libs)
./analizador.exe
```

Este comando solo funciona en la terminal de MSYS2, no en PowerShell.

## Linux

Debian / Ubuntu:

```bash
sudo apt install g++ libsdl2-dev
g++ -O2 -std=c++17 analizador_audio.cpp -o analizador $(sdl2-config --cflags --libs)
./analizador
```

En Fedora el paquete es `SDL2-devel` y en Arch es `sdl2`.

## macOS

```bash
xcode-select --install
brew install sdl2
g++ -O2 -std=c++17 analizador_audio.cpp -o analizador $(sdl2-config --cflags --libs)
./analizador
```

## Uso

```text
analizador        micrófono por defecto
analizador 2      usa el dispositivo de captura número 2
```

Al iniciar, el programa lista en la consola los dispositivos de captura disponibles con su número.

| Tecla | Acción |
|---|---|
| Espacio | Pausar / reanudar |
| J | Empezar o reiniciar el juego de afinación |
| S | Guardar una captura PNG |
| Esc | Volver del juego al analizador; fuera del juego, salir |

### Juego de afinación

1. Pulsa `J`. Aparece la nota pedida (DO, RE, MI, FA, SOL, LA o SI).
2. Canta o silba. El indicador muestra qué tan lejos estás: `SUBE` si vas grave, `BAJA` si vas agudo. Cada marca es un semitono.
3. Cuando el indicador entra en la zona verde, sostén la nota hasta llenar la barra. Sirve cualquier octava.
4. Cada nota lograda da entre 100 y 200 puntos: más puntos cuanto más rápido.

A los 45 segundos termina la partida y se muestra el puntaje.

### Archivos que genera

| Archivo | Contenido |
|---|---|
| `capturas/captura_FECHA_HORA.png` | Capturas hechas con `S` |
| `record.txt` | Mejor puntaje del juego. Bórralo para reiniciar el récord |

Ambos se crean en la carpeta desde la que se ejecuta el programa.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| `SDL.h: No such file or directory` | En PowerShell, comprueba que las rutas `-I` y `-L` van entre comillas. Si ya lo están, falta SDL2 o la versión de la ruta no coincide con la carpeta de `deps/` |
| El juego no reacciona a la voz | Acerca el micrófono y sostén un tono limpio (una vocal o un silbido). Detecta notas entre 70 y 2500 Hz |
| `g++` no se reconoce como comando | En PowerShell o CMD el compilador no está en el PATH. Repite el paso 3 de la opción A y reabre la terminal, o usa la terminal MSYS2 UCRT64. Solo para la sesión actual: `$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path` |
| `collect2.exe: error: ld returned 1 exit status` con muchos `undefined reference` | Se compiló con `-static` completo, que enlaza la `libSDL2.a` estática y le faltan librerías de Windows. Usa `-static-libgcc -static-libstdc++` en su lugar |
| Falta `SDL2.dll` al abrir el programa | Copia `SDL2.dll` a la misma carpeta que `analizador.exe` |
| Faltan `libstdc++-6.dll`, `libgcc_s_seh-1.dll` o `libwinpthread-1.dll` | Ocurre al llevar el `.exe` a un equipo sin MSYS2. Recompila añadiendo `-static-libgcc -static-libstdc++` (no uses `-static` solo) y copia `libwinpthread-1.dll` desde `C:\msys64\ucrt64\bin\` si sigue pidiéndola |
| `No se pudo abrir el microfono` | En Windows: Configuración → Privacidad y seguridad → Micrófono → permitir el acceso a las aplicaciones de escritorio. En macOS: da permiso de micrófono a la terminal |
| `pacman` devuelve errores 404 | La base de paquetes de MSYS2 está desactualizada. Ejecuta `pacman -Syu` o usa la opción A |

## Distribuir solo el ejecutable

En otro Windows de 64 bits basta con copiar `analizador.exe` y `SDL2.dll` en la misma carpeta. Si el equipo de destino no tiene MSYS2, compila con `-static-libgcc -static-libstdc++` para no depender de las DLL del compilador (si aun así pide `libwinpthread-1.dll`, cópiala junto al `.exe`).

## Docker

Compila y ejecuta en un contenedor Linux. Necesita un host Linux con X11 y PulseAudio (o PipeWire), porque el programa abre una ventana y captura el micrófono. En macOS Docker no expone el micrófono ni la pantalla al contenedor, así que ahí conviene compilar de forma nativa. Para Windows 11 ver más abajo.

```bash
xhost +local:docker
docker compose up --build
```

`docker-compose.yml` asume que tu usuario tiene UID 1000 (`id -u`); si no, cambia `1000` en `user` y en las rutas de PulseAudio.

Las capturas y `record.txt` quedan en `./datos`. Para elegir otro dispositivo de captura:

```bash
docker compose run --rm analizador 2
```

Solo compilar, sin Compose:

```bash
docker build -t analizador .
```

### Windows 11

Docker Desktop corre en una máquina virtual, pero WSL2 con WSLg (incluido en Windows 11) comparte pantalla y micrófono con las distribuciones de WSL. El contenedor se lanza desde dentro de WSL.

1. Instala WSL2 con Ubuntu. En PowerShell como administrador:

   ```powershell
   wsl --install -d Ubuntu
   ```

   Reinicia si te lo pide y crea tu usuario de Ubuntu.
2. Instala Docker Desktop desde <https://www.docker.com/products/docker-desktop> con la opción "Use WSL 2 based engine".
3. En Docker Desktop: Settings → Resources → WSL Integration → activa Ubuntu → Apply.
4. Abre Ubuntu desde el menú Inicio, clona el proyecto dentro de WSL (mejor en `~`, no en `/mnt/c`) y ejecuta:

   ```bash
   docker compose -f docker-compose.wsl.yml up --build
   ```

   La ventana aparece en el escritorio de Windows. Las capturas y `record.txt` quedan en `./datos`.
5. Si no detecta el micrófono: Configuración de Windows → Privacidad y seguridad → Micrófono → permite el acceso a las aplicaciones de escritorio. Con `docker compose -f docker-compose.wsl.yml run --rm analizador 1` eliges otro dispositivo.

No se ha probado en Windows; si falla, comprueba primero que `echo $DISPLAY` en Ubuntu devuelve `:0` y que existe `/mnt/wslg/PulseServer`.
