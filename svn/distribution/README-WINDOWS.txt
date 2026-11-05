ForgeCraft 0.1.2 — Windows de 64 bits
=====================================

1. Descomprime forgecraft_0_1_2.zip.
2. Ejecuta forgecraft_0_1_2\forgecraft.exe.

No requiere instalación ni permisos de administrador. Se puede mover la carpeta completa a otra ubicación o a una unidad extraíble, pero hay que mantener juntos el ejecutable, las DLL, las carpetas de plugins y la hoja forgecraft.qss.

Contenido del paquete
---------------------
- forgecraft.exe            Editor.
- forgecraft.qss            Hoja de estilo del tema (se puede ajustar sin recompilar).
- Qt6*.dll                  Bibliotecas de Qt 6.7.
- libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll
                            Runtime de MinGW 11.2.
- libmpg123-0.dll           Decodificación MP3 (LGPL 2.1).
- libmp3lame-0.dll          Codificación MP3 (LGPL 2 o posterior).
- platforms\                qwindows y qoffscreen (Qt).
- iconengines\, imageformats\
                            Motor de iconos SVG y lector de SVG (Qt).
- licenses\                 Textos legales y avisos de terceros.
- README.txt                Este archivo.
- THIRD_PARTY_NOTICES.txt   Avisos de componentes de terceros.

Requisitos
----------
Windows 10 u 11 de 64 bits. La reproducción usa waveOut y la grabación waveIn, ambas del propio sistema. Para MP3 basta con las DLL incluidas: no hace falta instalar códecs, FFmpeg ni ningún reproductor.

Si el programa no arranca, comprueba que la carpeta platforms esté junto al ejecutable: sin ella Qt no puede crear la ventana.

Licencia
--------
ForgeCraft se distribuye bajo GNU General Public License versión 3. El texto completo está en licenses\ForgeCraft-GPL-3.0.txt y en el repositorio del proyecto.
