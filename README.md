# sampler5

Plugin VST3 para Windows: un audio, cortado en hasta 16 pads.
Hecho con [JUCE 9](https://juce.com) y [Signalsmith Stretch](https://signalsmith-audio.co.uk/code/stretch/).

## Que hace (v1.4)

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
- Pestana **PAD**: VOL, PAN, TONO, FINO (cents), VELOCIDAD (respuesta del pad al golpe), GANANCIA, ATAQUE,
  LIBERACION, INICIO, FINAL, INVERTIR, LOOP (repite mientras se mantiene la nota), SILENCIAR y
  REPRODUCCION (MANTENER / DISPARO).
- Pestana **FILTRO**: filtro del pad elegido (pasa bajos / pasa altos), CORTE y RESONANCIA.
- Pestana **GLOBAL**: MASTER, PAN, POLI / MONO, VOCES y pitch global por semitonos. **LLEVAR A TONALIDAD**
  elige la tonalidad destino y el plugin calcula los semitonos.
- Pestana **AVANZADO**: HUMANIZAR (variacion leve de volumen y tiempo), VEL (respuesta general al golpe),
  SWING (retrasa las notas en contratiempo; necesita el proyecto en reproduccion), CHOKE (cada pad corta a los demas)
  y CANAL MIDI.
- Pestana **MIDI**: TECLAS (el pad elegido se toca cromatico), NOTA BASE, APRENDER, ENTRADA MIDI, RANGO MIDI
  y AFTERTOUCH (a volumen).
- **SEGUIR PAD** (pestana MIDI, encendido por defecto): el pad que tocas por MIDI pasa a ser el pad en edicion,
  como en una MPC. Las perillas de PAD y FILTRO siempre editan el pad elegido (el de la luz roja).
- Con el mouse, los pads suenan mas fuerte cuanto mas arriba se hace clic. CORTE y RESONANCIA encienden el filtro solos.
- **MUESTRA**: CARGAR, INV (el audio entero al reves) y SILENCIO (corta todo lo que suena).
- **TONAL**: detecta la tonalidad del audio (o la toma del nombre del archivo, ej. `_A#m`).
- **Tam.** (abajo a la derecha): tamano de la ventana, de 75 % a 200 %. Se recuerda para todas las instancias.
- **PRESET**: kits con la ruta al audio, en `Documentos/sampler5/Presets` (GUARDAR, CARPETA, y Predeterminado para vaciar).
- **SINCRO** sigue el tempo del proyecto (o el campo TEMPO si el host no lo informa); **ESTIRAR** lo hace sin cambiar el tono.
- El proyecto del DAW guarda todo, incluido el audio (comprimido sin perdida), asi no se pierde si se mueve el archivo.

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
