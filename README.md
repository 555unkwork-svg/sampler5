# sampler5

Plugin VST3 para Windows: un audio, cortado en hasta 16 pads.
Hecho con [JUCE 9](https://juce.com) y [Signalsmith Stretch](https://signalsmith-audio.co.uk/code/stretch/).

## Que hace (v1.1)

- Carga WAV, AIFF, FLAC, MP3 u OGG (boton **CARGAR** o arrastrando el archivo). Hasta 2 minutos.
- Detecta golpes y estima el BPM (usa tambien el largo del loop y el tempo del nombre del archivo).
- Corta por golpes (con sensibilidad), por grilla, en partes iguales, al azar o a mano. **REGENERAR** vuelve a cortar.
- Tres vistas de la pantalla:
  - **FUENTE**: el audio entero, para elegir la zona que se usa (bordes rojos).
  - **CORTES**: arrastrar un marcador lo mueve, doble clic crea un corte, la manija roja mueve el final
    del corte elegido, clic derecho abre el menu (renombrar, favorito, fin automatico, borrar).
  - **ANALISIS**: los golpes detectados y cuales entran con la sensibilidad actual.
- Zoom con la rueda del mouse o los botones + / - de **MUESTRA**; la mini vista muestra y mueve la parte visible.
- **AUTOAJUSTE**: iman al mover cortes (Auto, Golpe, Cruce por cero o Grilla).
- Los cortes **favoritos** no se pierden al regenerar. Cada corte puede tener nombre.
- 16 pads por MIDI (desde C5 de FL Studio, nota 60) o con el mouse.
- Pestana **PAD**: VOL, PAN, TONO, ATAQUE, LIBERACION, INICIO, FINAL, INVERTIR y REPRODUCCION (MANTENER / DISPARO).
- Pestana **GLOBAL**: MASTER, VEL, MONO y pitch global por semitonos (con ESTIRAR no cambia la duracion).
- Pestana **MIDI**: TECLAS (el pad elegido se toca cromatico), NOTA BASE y APRENDER.
- **SINCRO** sigue el tempo del proyecto (o el campo TEMPO si el host no lo informa); **ESTIRAR** lo hace sin cambiar el tono.
- El proyecto guarda cortes, nombres, favoritos, notas y parametros. El audio se guarda como ruta al archivo.

La interfaz tiene estilo de aparato: cuerpo claro, teclas con relieve, pantalla negra y acentos rojos,
con una tipografia propia (Sampler5 Sans, generada por `Tools/make_font.py`).

### Escudo

El escudo de abajo a la izquierda es `Assets/escudo.png` (uso personal). Para cambiarlo, reemplazar ese archivo
(PNG con fondo transparente, cuadrada, 256 px o mas) y subirla al repositorio: la proxima compilacion la incluye.

## Como conseguir el plugin

Cada vez que se sube codigo, GitHub lo compila solo:

1. Pestana **Actions** del repositorio, ultimo "Compilar plugin (Windows)".
2. Abajo, en **Artifacts**, bajar `Sampler5-VST3-Windows`.
3. Descomprimir y copiar la carpeta `sampler5.vst3` a `C:\Program Files\Common Files\VST3\`.
4. En el DAW, volver a escanear plugins.

`Sampler5-Standalone-Windows` es el mismo plugin como programa suelto, para probar sin DAW.

## Estructura

- `Source/Engine/` motor sin interfaz: analisis y cortes, voces, stretch.
- `Source/PluginProcessor.*` parametros, carga en segundo plano, sync, estado.
- `Source/PluginEditor.*` interfaz.
- `Tests/` pruebas automaticas del motor y del plugin.

Nombre y marca se cambian arriba de todo en `CMakeLists.txt`.

## Compilar a mano

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release
```

## Licencias

JUCE es AGPLv3 o comercial: para vender el plugin cerrado hace falta una licencia de JUCE
(hay un plan gratuito con tope de ingresos; ver juce.com). Signalsmith Stretch es MIT.
