# FFmpeg para ForgeCraft

FFmpeg y ffprobe 9.0.2, compilación estática de 64 bits **essentials_build** de Gyan.
Binarios originales, sin modificar. Licencia GPL versión 3, incluida en LICENSE.txt.
Configuración y dependencias: BUILD-README.txt.

- Distribuidor: https://www.gyan.dev/ffmpeg/builds/
- Archivo verificado: ffmpeg-release-essentials.7z (35.430.500 bytes).
- SHA256: `4705843ccaaf54257c16ad90f3e952ece33c17df964ecf7bfdbb0f49c7171077`.
- Código fuente correspondiente: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b
- Archivo fuente: https://github.com/FFmpeg/FFmpeg/archive/946fcce07b.tar.gz
- Recetas de compilación del distribuidor: https://github.com/GyanD/codexffmpeg

Ejecutar `setup-codecs.ps1` desde la raíz para descargar, verificar y copiar las
herramientas. También admite `-Archive ruta.7z` con una copia de la versión
verificada. Si el distribuidor actualiza el enlace, el hash impide instalar una
versión distinta sin revisión. Los binarios no se guardan en Git; se incluyen
en `codecs/` en los paquetes de Windows junto con estas licencias y avisos.

ForgeCraft usa procesos independientes con argumentos, sin intérprete de
comandos, para importar y exportar audio local. WAV PCM y MP3 conservan sus
códecs propios. No se usa FFmpeg para reproducir o grabar en tiempo real.
