# sampler5

Plugin VST3 para Windows: un audio, cortado en hasta 16 pads.
Hecho con [JUCE 9](https://juce.com) y [Signalsmith Stretch](https://signalsmith-audio.co.uk/code/stretch/).

## Que hace (v1.0)

- Carga WAV, AIFF, FLAC, MP3 u OGG (boton **Cargar** o arrastrando el archivo). Hasta 5 minutos.
- Detecta golpes y estima el BPM (usa tambien el largo del loop y el tempo del nombre del archivo).
- Corta por golpes (con control de sensibilidad), por grilla, en partes iguales, al azar o a mano.
- Los cortes van en cruces por cero y terminan con un fundido corto antes del golpe siguiente.
- Waveform: arrastrar un marcador lo mueve, doble clic crea un corte, clic derecho lo borra.
- 16 pads por MIDI (desde C5 de FL Studio, nota 60) o con el mouse.
- Por pad: volumen, paneo, tono, ataque, release, reversa y mantener (gate, activado por defecto).
- **SYNC**: sigue el tempo del proyecto. **STRETCH**: lo hace sin cambiar el tono.
- **Teclas**: el pad elegido se toca cromatico desde la nota base.
- **Aprender MIDI**: asigna el pad elegido a la proxima nota que llegue.
- Mono / poly, sensibilidad a la velocidad, master.
- El proyecto guarda cortes, notas y parametros. El audio se guarda como ruta al archivo.

- **SYNC** usa el tempo del proyecto; si el host no lo informa (o en la version standalone), usa el
  tempo que se escribe en el campo TEMPO.

- **DETECTAR** vuelve a poner el BPM detectado. **PITCH** (perilla y botones -1 / +1) cambia el tono
  general por semitonos; con STRETCH activado no cambia la duracion.

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
