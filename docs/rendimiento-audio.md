# Carga y primer Play — 4 de noviembre de 2026

Se midieron archivos sintéticos estéreo a 44.100 Hz, WAV PCM16 y MP3 CBR de 192 kbps, generados en el propio equipo. Los valores son medias de tres lecturas con caché del sistema caliente. Incluyen lectura, decodificación y cálculo de picos; excluyen generación del archivo, codificación de la muestra MP3 y construcción de los controles de la ventana. No representan una medición de todas las canciones ni de discos externos o de red.

| Audio | Antes | Después |
|---|---:|---:|
| WAV de 30 segundos | 19,04 ms | 16,87 ms |
| MP3 de 30 segundos | 23,17 ms | 23,41 ms |
| WAV de 2 minutos | 65,34 ms | 51,59 ms |
| MP3 de 2 minutos | 90,08 ms | 81,35 ms |

La carga de estas muestras no muestra una demora de varios segundos. Se eliminó una copia completa de los datos WAV, se reservó el espacio para la lectura y se redujeron comprobaciones de acceso por muestra en los decodificadores. La preparación de la forma de onda continúa en segundo plano y mantiene la cancelación y el porcentaje de progreso.

El primer Play podía limitarse a detener una reproducción ya finalizada: el documento seguía asociado al reproductor hasta la siguiente actualización del temporizador. Ahora se consulta también el estado del búfer del dispositivo; una reproducción terminada se vuelve a iniciar con ese primer clic.

Para comenzar Play ya no se convierte toda la canción a PCM16 si la salida admite float32: se mantiene una referencia compartida a las muestras, válida hasta cerrar el dispositivo. Hay una ruta PCM16 de compatibilidad para salidas que rechazan float32. La medición de arranque nativo de un búfer silencioso de dos minutos fue de 98,55 ms en este equipo, incluyendo la apertura y el encolado en Windows; no es una medición de la latencia audible de los altavoces. Los medidores leen las muestras sin provocar copias de la canción. Los bucles permanecen asociados al documento de origen aunque cambie el foco, y las previsualizaciones finalizan también mientras se está grabando.

Las pruebas comprobaron salida nativa silenciosa, fin natural antes de la limpieza del temporizador, reinicio con un único Play, varios arranques consecutivos, la ruta PCM16, conservación del búfer compartido y aislamiento entre MDI. Verifican el encolado y la finalización del dispositivo; no sustituyen una comprobación auditiva del volumen o del dispositivo de salida elegido por el usuario.

Para repetir las mediciones de carga:

```powershell
Start-Process .\out\bin\forgecraft.exe -ArgumentList '--benchmark-audio', '-platform', 'offscreen' -Wait -RedirectStandardOutput .\out\build\benchmark.txt
```

`--self-test` incluye las comprobaciones nativas de reproducción con silencio; las omite explícitamente si no hay una salida disponible. Registros de esta revisión: `out/build/benchmark-before.txt`, `out/build/benchmark-after.txt` y `out/build/test-results.txt`.
