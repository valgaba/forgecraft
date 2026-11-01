# out/

Resultados de compilación y distribución. **No se versiona**: los paquetes se publican como assets de la Release correspondiente.

| Carpeta | Contenido |
|---|---|
| `build/` | Makefiles, objetos, moc, recursos y capturas de las pruebas (`preview.png`, `minimized.png`, `process-dialog.png`, `process-dialog-eq.png`, `process-menu.png`, `effects-menu.png`, `recording-dialog.png`). |
| `bin/` | Ejecutable, DLL, plugins y hoja de estilo desplegados por `build.ps1 -Deploy`. |
| `packages/` | Paquetes `.zip` y `SHA256SUMS.txt` generados por `package.ps1`. |

Para reconstruirlo todo: `.\build.ps1 -Deploy -Test` y después `.\package.ps1`.
