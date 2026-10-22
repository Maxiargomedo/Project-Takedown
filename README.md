# Project Takedown: Burnout 3 Takedown (PAL) - Native PC Port

[![Burnout 3 PC Port](https://img.shields.io/badge/Burnout%203-PAL%20PC%20Port-red.svg)](https://github.com/Maxiargomedo/Project-Takedown)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64%20(Vulkan)-blue.svg)](https://github.com/Maxiargomedo/Project-Takedown)
[![Release](https://img.shields.io/badge/Release-v1.0.0-orange.svg)](https://github.com/Maxiargomedo/Project-Takedown/releases)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**Burnout 3: Takedown (PAL - SLES-52584) ejecutándose de forma nativa en PC con Windows.**  
No es emulación: el código del juego de PS2 fue traducido a C++ nativo y compilado para x64, alcanzando 60 FPS estables, soporte para pantallas panorámicas (16:9), escalado hasta 4K, menú de ajustes de PC integrado, soporte completo para teclado, ratón y mandos modernos (Xbox, PlayStation DualShock/DualSense), y un instalador autónomo listo para usar.

---

## 💾 Descarga del Instalador / Quick Download

Para descargar el instalador directo sin compilar nada:

* 🚀 **[Descarga Directa del Instalador (Instalador_Burnout3_PC.exe)](https://raw.githubusercontent.com/Maxiargomedo/Project-Takedown/main/releases/Instalador_Burnout3_PC.exe)**  
* 📂 **[Ver archivo en el Repositorio con botón Download](https://github.com/Maxiargomedo/Project-Takedown/blob/main/releases/Instalador_Burnout3_PC.exe)**  
* 🏷️ **[Página de Releases / Versiones](https://github.com/Maxiargomedo/Project-Takedown/releases)**

### Pasos rápidos:
1. Descarga y ejecuta `Instalador_Burnout3_PC.exe`.
2. Elige la carpeta de instalación (por defecto: `C:\Juegos\Burnout 3 Takedown PC`).
3. El instalador configurará los accesos directos y los archivos necesarios.
4. Al abrir el juego por primera vez, selecciona tu copia de la ISO de **Burnout 3: Takedown (PAL / SLES-52584)**. ¡Y a correr!

---

## 🌟 Créditos y Reconocimientos Especiales

> Este proyecto fue posible gracias a la extraordinaria base y trabajo pionero de:
> - **[danielgomesvieira2000/burnout3-pc](https://github.com/danielgomesvieira2000/burnout3-pc)** y el motor **ps2-port-engine**.  
> Nuestro más sincero agradecimiento a Daniel Gomes Vieira por hacer posible la ingeniería inversa y traducción de Burnout 3 a C++. Este repositorio amplía su legado adaptándolo a la versión europea PAL (multilenguaje), corrigiendo fallos de navegación, menús de PC, controles y empaquetándolo en un instalador fácil para la comunidad.

---

## ✨ Características de esta versión (Project Takedown PAL)

- **Edición PAL Europea completa**: Soporte para idiomas Español, Inglés, Francés, Alemán e Italiano.
- **Instalador Nativo Win32 integrado**: Con detección inteligente de instalaciones previas, reparación automática y desinstalación limpia.
- **Menú de Ajustes PC corregido y ampliado**:
  - Navegación impecable con flechas arriba/abajo, Enter para confirmar y Esc para volver atrás.
  - Drenaje anti doble-click para evitar activaciones accidentales.
  - Opción de **Saltar Videos Intro** de forma automática o con botón Start.
  - Opciones de video ordenadas: Pantalla completa, VSync, Resolución interna (hasta 4x), Relación de aspecto (16:9 / 4:3), Anti-Aliasing (FXAA, SMAA, SSAA 2x).
  - Persistencia asegurada para el desenfoque de velocidad (*Speed Blur*) en `pc_settings.ini`.
- **Soporte de Controles Modernos y Clásicos**:
  - Modos de conducción: **Clásico** (Cruz/Cuadrado o A/X) y **Moderno** (Aceleración y freno con gatillos RT/LT o R2/L2).
  - Navegación con Mouse: Selección con clic izquierdo, retroceso con clic derecho y scroll con rueda del ratón.
  - Reasignación de teclas para teclado con interfaz traducida al español.
  - Soporte de vibración (*rumble*) para mandos Xbox y PlayStation.
- **Motor integrado**: El motor `engine/` viene incorporado directamente en el repositorio sin dependencias rotas de submódulos.

---

## 🎮 Controles por Defecto

### Menús y Navegación
| Acción | Teclado / Ratón | Mando (Xbox / PlayStation) |
|---|---|---|
| Moverse por opciones | Flechas Arriba / Abajo / Izq / Der | Cruceta (D-Pad) o Stick Izquierdo |
| Aceptar / Seleccionar | Enter / Clic Izquierdo | A (Xbox) / Cruz (PS) |
| Retroceder / Cancelar | Esc / Clic Derecho | B (Xbox) / Círculo (PS) |
| Pausa | Esc / Enter | Start |

---

### Conducción: Modo Moderno (Gatillos)
*Diseñado para la experiencia de conducción moderna en PC con mandos de Xbox o PlayStation.*

| Acción | Teclado | Mando Xbox | Mando PlayStation |
|---|---|---|---|
| **Acelerar** | W / Flecha Arriba | **Gatillo Derecho (RT)** | **Gatillo R2** |
| **Frenar / Reversa** | S / Flecha Abajo | **Gatillo Izquierdo (LT)** | **Gatillo L2** |
| **Dirección** | A / D o Flechas Izq / Der | Stick Izquierdo | Stick Izquierdo |
| **Boost (Turbo)** | Espacio / Shift Izquierdo | Botón A | Botón Cruz (✖) |
| **Freno de Mano** | Espacio / Ctrl | Botón X | Botón Cuadrado (■) |
| **Mirar Atrás** | Tecla C | Botón Y | Botón Triángulo (▲) |
| **Cambiar Cámara** | Tecla V | Botón B | Botón Círculo (●) |
| **Siguiente Canción** | Tecla B | Botón LB | Botón L1 |

---

### Conducción: Modo Clásico (Original de PS2)
*Distribución idéntica a la consola PlayStation 2 original.*

| Acción | Teclado | Mando Xbox | Mando PlayStation |
|---|---|---|---|
| **Acelerar** | W / Flecha Arriba | **Botón A** | **Botón Cruz (✖)** |
| **Frenar / Reversa** | S / Flecha Abajo | **Botón X** | **Botón Cuadrado (■)** |
| **Dirección** | A / D o Flechas Izq / Der | Stick Izquierdo | Stick Izquierdo |
| **Boost (Turbo)** | Espacio / Shift Izquierdo | **Botón RB / RT** | **Botón R1 / R2** |
| **Freno de Mano** | Espacio / Ctrl | Gatillo Izquierdo (LT) | Botón L1 |
| **Mirar Atrás** | Tecla C | Botón LB | Botón L1 |
| **Cambiar Cámara** | Tecla V | Botón Y | Botón Triángulo (▲) |
| **Siguiente Canción** | Tecla B | Botón Blanco / View | Botón L2 |

*(Nota: En cualquier momento puedes alternar entre Modo Clásico y Modo Moderno o reasignar las teclas desde el menú `AJUSTES PC > CONTROLES` dentro del juego).*

---

## 🛠️ Compilación desde el Código Fuente

### Requisitos
- Windows 10 / 11 (64-bit)
- Visual Studio 2022 con C++ (o MSVC Build Tools)
- CMake 3.24 o superior
- Vulkan SDK 1.3
- Python 3.10+ (opcional para herramientas de análisis)

### Pasos de Compilación
```powershell
# 1. Clonar el repositorio
git clone https://github.com/Maxiargomedo/Project-Takedown.git
cd Project-Takedown

# 2. Generar el proyecto CMake
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 3. Compilar el ejecutable
cmake --build build --config Release
```

El ejecutable compilado estará disponible en `build/games/burnout3_pal/port/Release/burnout3_pal.exe`.

Para recompilar el instalador Win32:
```powershell
cd installer
build_installer.bat
```

---

## ⚖️ Nota Legal

Este repositorio **no contiene material con derechos de autor**, archivos binarios de la BIOS ni imágenes ISO originales de PlayStation 2. Para jugar se requiere una copia legítima del disco **Burnout 3: Takedown (PAL / SLES-52584)**.  
*Burnout* es una marca registrada de Electronic Arts Inc. Este proyecto con fines de preservación histórica no está afiliado ni respaldado por Electronic Arts ni Criterion Games.
