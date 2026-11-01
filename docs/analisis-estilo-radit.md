# Adaptación de la interfaz de ForgeCraft a la identidad visual de Radit

**Tipo de documento:** análisis técnico. No modifica código, recursos ni configuración.
**Fecha:** 1 de noviembre de 2026
**Alcance:** `forgecraft/svn` (Qt Widgets 6.7.0, C++17, kit MinGW 11.2 de 64 bits) comparado con `radit/svn` y `radit/out/bin/radit.qss`.
**Norma aplicable:** `Proyectos Qt/AGENTS.md` §6 (identidad visual Radit) y §10 (compilar y probar).

---

## 1. Resumen

ForgeCraft pinta su interfaz **desde el código**: paleta global, fuente, colores de la forma de onda, medidores y fondo del área de trabajo están incrustados como literales. Radit, en cambio, separa **mecanismo** (hoja QSS externa + clases envoltorio con `objectName`) de **valores** (los tokens de su hoja).

Adaptar la interfaz no consiste en cambiar colores: consiste en **adoptar el mecanismo de Radit y escribir la capa QSS que ForgeCraft no tiene**, porque Radit no usa algunas de las clases que ForgeCraft emplea a fondo (barras de herramientas, MDI, combo boxes genéricos, spin boxes de doble precisión).

Dos conclusiones cuantitativas:

1. **Copiar `radit.qss` tal cual no funciona.** De sus 333 bloques, **239 están atados a identificadores de módulos de Radit** (`#Planner`, `#Capture`, `#MeteoClock`…) inexistentes en ForgeCraft. El núcleo reutilizable son ~94 bloques, de los cuales ~45 son clases estándar de Qt.
2. **ForgeCraft necesita entre 200 y 400 líneas de QSS nuevas** para las clases que Radit no estiliza, más el repintado de tres widgets propios (`Waveform`, `LevelMeter`) y la decisión estructural sobre las ventanas MDI.

---

## 2. Estado actual de ForgeCraft

Todo el aspecto vive en cuatro archivos. No hay `.qrc`, ni `.qss`, ni carpeta de recursos: el `.pro` no declara `RESOURCES`.

| Elemento | Ubicación | Detalle |
|---|---|---|
| Estilo global | `svn/main.cpp:89-96` | `QStyleFactory::create("Windows")`; paleta clara `#efefef`; `QFont("Tahoma", 8)`; **una sola** línea de `setStyleSheet` con cuatro reglas (toolbar, statusbar, MDI, dock) |
| Fondo del área de trabajo | `svn/mainwindow.cpp:119-121` | textura de 96×96 px generada en memoria (`#eeeeec`, `#ffffff`, `#d5d5d2`) |
| Forma de onda | `svn/waveform.cpp:34-71` | fondo blanco; regla de tiempo `#f0f0f0`; franja izquierda `#efefef`; onda `#000066`; selección `#0000ff`; cursor negro; cursor de reproducción rojo; `Tahoma 7`; métricas fijas (`left = 28`, regla de 27 px) |
| Medidores de nivel | `svn/mainwindow.cpp:67-82` | dos barras con degradado verde→amarillo→rojo, fondo `palette().window()`, texto `−Inf.`/dB en negro |
| Iconos | `svn/mainwindow.cpp:175-181` y 17 × `QStyle::SP_*` | dos iconos dibujados con `QPainter` (lupa, portapapeles) y el resto del estilo nativo de Windows |
| Ventanas de audio | `svn/mainwindow.cpp:62-65, 225-232` | `AudioWindow : QMdiSubWindow` con barra de título nativa; borde `#737e88` |
| Diálogos | `svn/recorddialog.cpp`, `svn/processingui.cpp` | `QDialog` + `QFormLayout` + `QComboBox` + `QDoubleSpinBox` + `QProgressDialog`; **ya asignan `objectName`** (`recordDialog`, `processDialog`, `processingProgress`, `recordDevice`, `recordRate`…) |

Cifras medidas:

- **34 literales de color** hexadecimales en 4 archivos.
- **1** llamada a `setStyleSheet` en todo el proyecto.
- **3** usos de `QFont` (Tahoma 8 global; Tahoma 7 en onda y medidores).
- **17** iconos tomados del estilo nativo (`QStyle::SP_*`).
- **0** archivos `.qss` y **0** `.qrc`.

Punto de partida favorable: los `objectName` de los diálogos ya existen, de modo que la capa QSS puede escribirse con ámbito sin renombrar casi nada.

---

## 3. Identidad visual Radit

### 3.1 Mecanismo (lo que hay que copiar)

- `radit/svn/main.cpp:66-73` carga `radit.qss` desde disco con `QFile` y `qApp->setStyleSheet()`. El tema es **texto externo**, no compilado: permite iterar el diseño sin recompilar.
- `radit.qrc` aporta los iconos como SVG (`:/icons/...`); el `.pro` declara `QT += svg`.
- Clases envoltorio mínimas cuyo único cometido es ponerse el nombre para el QSS:

  | Clase | Base | Constructor |
  |---|---|---|
  | `Frame` | `QFrame` | `setObjectName("Frame")` |
  | `Button` | `QPushButton` | `setObjectName("Button")`, fuente 12 pt, cursor de mano, `SetIcon()` a 25×25 |
  | `Label` | `QLabel` | `setObjectName("Label")`, fuente 12 pt negrita, `wordWrap` |
  | `Container` | `QScrollArea` | `setObjectName("Container")`, barras `ScrollBar` propias |
  | `Tab`, `TabBar`, `Slider`, `ScrollBar`, `Menu`, `TreeView`, `ListView`, `VuMeter`, `LevelMeter` | — | mismo patrón |

- Cada módulo de la aplicación se identifica con un id (`#Capture`, `#Planner`, `#FileExplore`, `#MeteoClock`) y el QSS se escribe por ámbito: `#Capture Button { … }`.
- Las barras de título no son nativas: `Frame#framebarra` + `QLabel#PanelTitle` + `QPushButton#RaditModuleCloseButton`, más `Frame#framecenter` / `Frame#frametop` / `Frame#framedown`.
- Los medidores son `QFrame` con fondo y borde definidos por QSS y relleno plano pintado en `paintEvent` (`radit/svn/widgets/levelmeter.cpp:82-100`), no degradados.

### 3.2 Tokens (valores medidos en `radit/out/bin/radit.qss`, 1.918 líneas / 333 bloques)

| Rol | Valores |
|---|---|
| Fondo de ventana | `rgb(82, 82, 82)`; marcos propios `#262c3b` |
| Paneles y barras | `#262c3b` (marco), `#181e2c` (barra de título, toolbar, `framecenter`), `#252b39`, `#20242e`, `#333b4f` |
| Bordes | `#686b74`, `#535b6a`, `#333b4f`, `#4e4d7a` |
| Texto | `#8a8d96` (secundario), `#dfdbd2` (principal), `#9AA0A6` (campos) |
| Acento | `#80A4AE` (53 + 9 usos); estado marcado `rgba(0, 172, 252, 80)`; pestaña activa `#39385b` |
| Menús | fondo `#39385b`, hover `#4e4d7a`, texto `#dfdbd2`; barra de menús `#1e1e1e` |
| Tipografía | `"Segoe UI", sans-serif`; 11–14 px en controles, 8 px en etiquetas pequeñas |
| Radios | 2–3 px dominante (4 px puntual) |
| Scroll | vertical de **5 px** de ancho; handle `#77769d` |
| Tooltip | texto `#7b8d96` sobre `rgb(51, 59, 79)`, borde `#686b74`, 14 px |
| Medidores | relleno plano del color de acento + marca de pico de 2 px |

Nota: la hoja conserva bloques comentados del tema gris antiguo (`QPushButton` con gradientes, `QTabWidget`, `QMenu::item:selected`). No son tokens vigentes; conviene ignorarlos al extraer la paleta.

---

## 4. Brecha entre ForgeCraft y Radit

### 4.1 Cobertura de `radit.qss` sobre las clases que usa ForgeCraft

Apariciones de cada clase en la hoja de Radit:

| Clase | Radit | Situación en ForgeCraft |
|---|---|---|
| `QToolBar` | **0** | dos barras (archivo/edición y transporte) quedarían sin estilo |
| `QToolButton` | 3 (solo dentro de `TabBar`) | botones de transporte de 23×19 px sin regla propia |
| `QDockWidget` | **0** | dock «Niveles» de 68 px con título `#c6d6e5` sin equivalente |
| `QMdiArea` / `QMdiSubWindow` | **0** | Radit no usa MDI; no hay patrón que copiar |
| `QComboBox` | **0** genérico (solo `#Combo`, `#FileExplorePath`) | 4 combos en el diálogo de grabación |
| `QDoubleSpinBox` | **0** (hay `QSpinBox` y `QTimeEdit`) | parámetros de los 37 procesos |
| `QFileDialog`, `QDialogButtonBox` | **0** | diálogos nativos de archivo no heredarían el tema |
| `QTabWidget`, `QGroupBox`, `QRadioButton` | **0** | no se usan hoy; necesarios si se añaden |
| `QStatusBar` | 1 | barra de estado segmentada de ForgeCraft |
| `QMenuBar` / `QMenu` / `QMessageBox` / `QInputDialog` | 3 / 4 / 5 / 7 | cubiertas |
| `QLineEdit`, `QLabel`, `QCheckBox`, `QProgressBar`, `QScrollBar`, `QSplitter`, `QToolTip` | 26 / 59 / 14 / 4 / 12 / 4 / 1 | cubiertas |

Matiz importante: varios «0» no significan carencia de diseño, sino que Radit usa una subclase propia (`Slider`, `Tab`, `TabBar`, `TreeView`, `ListView`, `Menu`). Las reglas existen, pero bajo nombres de clase que ForgeCraft no tiene. Reutilizarlas exige crear esas subclases o replicar sus reglas para las clases estándar.

### 4.2 Equivalencias funcionales

| ForgeCraft | Equivalente en Radit | Observación |
|---|---|---|
| `QMdiArea` + `QMdiSubWindow` | módulos con `Frame#framebarra` | decisión estructural, ver §6 |
| `QToolBar` + `QToolButton` | `Frame#framebarra` + `Button` | Radit no usa barras de herramientas de Qt |
| `LevelMeter` (degradado) | `LevelMeter` (relleno plano + pico) | se puede adoptar el patrón de pintado |
| `Waveform` | `CueWaveformFrame` / `Planner` | Radit pinta sobre fondo oscuro con acento claro |
| Iconos `QStyle::SP_*` | SVG en `.qrc`, 25×25 en `Button` | requiere `QT += svg` |
| `QProgressDialog` | `LoadingDialog` / `LoadingProgress` | hay patrón de QSS para `QProgressBar#LoadingProgress` |
| Título de diálogo nativo | `Frame#framebarra` + `PanelTitle` + botón de cierre | afecta a `recorddialog.cpp` y a los 37 diálogos de proceso |

### 4.3 Dependencias que arrastra la hoja de Radit

Cinco referencias `url()` a cuatro SVG: `:/icons/Close.svg`, `:/icons/Close-hover.svg`, `:/icons/MoveRight.svg`, `:/icons/MoveLeft.svg`. Si se reutiliza la hoja, hay que aportar esos recursos o eliminar esos bloques.

---

## 5. Plan de adaptación por capas

Sin tocar audio, DSP, pruebas ni el sistema de construcción.

### Fase 0 — Decisiones previas

Ver §6. Bloquean el contraste y la estructura.

### Fase 1 — Adoptar el mecanismo

1. Crear `svn/style/forgecraft.qss` **versionado en el repositorio**. La hoja de Radit vive solo en `out/bin`, lo que no es reproducible.
2. En `main.cpp`: eliminar la línea 96, sustituir la paleta clara y `Tahoma 8` por la paleta Radit y `Segoe UI`, y cargar la hoja con ruta absoluta:

   ```cpp
   const QString path = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("forgecraft.qss"));
   ```

   Radit usa una ruta relativa al directorio de trabajo (`main.cpp:66`), lo que deja la aplicación sin tema si se arranca desde otro directorio. No conviene heredarlo.
3. Añadir `RESOURCES` al `.pro` con la hoja y los iconos; copiar la hoja en `build.ps1 -Deploy` para poder ajustarla sin recompilar.

### Fase 2 — QSS de la carcasa (lo que Radit no aporta)

Con los tokens de §3.2: `QToolBar` y `QToolButton` (fondo `#181e2c`, borde `#333b4f`, hover `#4e4d7a`), `QDockWidget` (título `#262c3b`, texto `#8a8d96`), `QStatusBar` y `QStatusBar::item`, `QMdiArea` (fondo `#181e2c`, sin textura), `QComboBox`, `QDoubleSpinBox`, `QDialog`, `QFormLayout`, `QProgressDialog`, `QScrollBar`.

### Fase 3 — Ventanas

- **Opción A (fiel a Radit):** subventanas sin marco nativo, con `Frame#framebarra`, `Label#PanelTitle` y botón de cierre propio; el `QMdiArea` pasa a ser contenedor de módulos. Más trabajo, resultado coherente.
- **Opción B (mínima):** mantener `QMdiSubWindow` y estilizarlo con reglas propias. Rápido, pero la barra de título sigue siendo la del sistema.

### Fase 4 — Widgets propios

- `Waveform`: extraer los colores a constantes (fondo, rejilla, onda, selección, cursor), cambiar la fuente y **corregir el contraste de la regla de tiempo**, hoy negro sobre `#f0f0f0`. Métricas fijas (`left = 28`, banda de 27 px) conviene convertirlas en constantes con nombre.
- `LevelMeter`: sustituir el degradado por relleno plano del acento con marca de pico, al estilo `radit/svn/widgets/levelmeter.cpp`. No es necesario copiar su animación por temporizador de 16 ms.
- Botones de transporte de 23×19 px: iconos SVG a 16 px y `padding` según tokens; es lo que más «de los 90» queda en la ventana.

### Fase 5 — Iconografía

Sustituir los 17 `SP_*` y los iconos pintados a mano por SVG monocromos (`#8a8d96` normal, `#80A4AE` activo) en un `.qrc`. Fase independiente: las fases 1–4 pueden completarse sin ella.

### Fase 6 — Diálogos

Aprovechar los `objectName` existentes: `#recordDialog QComboBox`, `#processDialog QDoubleSpinBox`, `#processingProgress`. Convertir los `QFileDialog::getOpenFileName`/`getSaveFileName` a diálogos no nativos (`DontUseNativeDialog`) o asumir que no heredarán el tema.

---

## 6. Decisiones pendientes

1. **Identidad en conflicto.** El `README.md` define la estética como «fondo gris con textura, ondas azul oscuro sobre blanco», es decir Sound Forge 4.5. Radit es oscuro con acento `#80A4AE`. Adaptar a Radit renuncia a esa referencia. Afecta a todas las decisiones de contraste.
2. **Zona de onda clara u oscura.** Oscura (`#181e2c` con onda `#80A4AE`) es lo coherente con Radit; híbrida (carcasa oscura, lienzo claro) conserva la lectura clásica. Debe elegirse antes de la fase 4.
3. **MDI o módulos.** Mantener MDI y estilizarlo es rápido pero deja un injerto; eliminarlo afecta al flujo de trabajo (cascada, mosaico, arrastrar archivos sobre la onda).
4. **Escalado.** La hoja de Radit mezcla px y pt y usa medidas pequeñas (scroll de 5 px, etiquetas de 8 px). Copiarlas sin más da un resultado apretado y poco usable en pantallas 4K.

---

## 7. Riesgos y trampas verificadas

| Riesgo | Detalle |
|---|---|
| Pruebas automáticas | `--smoke-test` dispara acciones por texto (`&Copiar`), busca un `QDoubleSpinBox` llamado `rate` y pulsa el botón «Cancelar» del `QProgressDialog`; genera `preview.png`, `process-menu.png`, `effects-menu.png`. Los cambios de QSS no lo rompen; sustituir barras por botones propios sí puede, si se eliminan acciones de los menús. Las acciones viven en los menús, así que la sustitución es viable, pero exige reejecutar `-Test`. |
| Paleta frente a QSS | `LevelMeter` consulta `palette().window()`: cambiar la paleta altera ese fondo sin pasar por la hoja. Hay que decidir cuál manda y no mezclar ambas fuentes. |
| Diálogos nativos | `QFileDialog` estático abre el diálogo del sistema y no hereda QSS. |
| Ruta del tema | Cargar la hoja con ruta relativa (como Radit) deja la aplicación sin tema al arrancar desde otra carpeta. |
| Reproducibilidad | `radit.qss` no está en el control de versiones de Radit; es un mal ejemplo a seguir. |
| Alcance | Las fases 1–2 no requieren recompilar si la hoja se copia a `out/bin`; sirven para revisar el diseño antes de tocar widgets. |

---

## 8. Verificación

Cada fase se cierra con el bucle existente:

```powershell
.\build.ps1 -Deploy -Test
```

y comparación de `out/build/preview.png`, `out/build/process-menu.png` y `out/build/effects-menu.png` contra la versión anterior. La referencia de partida es `out/build/preview.png` del 27/10/2026 (1500×850).

Criterios de aceptación propuestos:

1. Ningún color hexadecimal en `main.cpp`, `mainwindow.cpp`, `waveform.cpp` ni `recorddialog.cpp`.
2. Toda la apariencia proviene de `forgecraft.qss` más constantes con nombre en los dos widgets pintados a mano.
3. `-Test` pasa sin cambios en `dsp_tests.cpp`, `recording_tests.cpp` ni en el `--smoke-test`.
4. La aplicación se ve igual arrancada desde cualquier directorio de trabajo.

---

## 9. Lo que no conviene copiar de Radit

- Las 239 reglas con id de módulo: pertenecen a sus pantallas.
- El morado `#4e4d7a`/`#39385b` como color dominante de barra: en ForgeCraft debe ser acento de hover, no base.
- El scroll de 5 px y las etiquetas de 8 px.
- La carga del tema con ruta relativa y el archivo fuera del control de versiones.

---

## Anexo A — Bloques genéricos reutilizables de `radit.qss`

Clases estándar de Qt con regla genérica (no atada a un id de módulo):

`QMainWindow`, `QFrame`, `QTextEdit`, `QLineEdit`, `QLabel`, `QProgressBar` y `::chunk`, `QMenuBar` y `::item`, `QCheckBox` (`:hover`, `::indicator:checked`, `::indicator:unchecked`), `QStatusBar`, `QScrollArea`, `QSplitter` y `::handle`, `QScrollBar` (vertical, horizontal, `::handle`, `::add-line`, `::sub-line`, `::add-page`, `::sub-page`), `QToolTip`, `QMessageBox` (y `QLabel`, `QPushButton`, `:hover`, `:pressed`), `QInputDialog` (y `QLabel`, `QLineEdit`, `:focus`, `QPushButton`, `:hover`, `:pressed`), `QMenu`, `QMenu::item`, `QMenu::item:selected`, `QMenu::separator`.

Clases propias de Radit con regla genérica (requieren la subclase o una regla equivalente): `Frame`, `Button`, `Label`, `Slider`, `Tab`, `TabBar`, `TreeView`, `ListView`, `FrameView`, `ItemBase`, `AudioItem*`, `VuMeter`.

## Anexo B — Pasos de interfaz pendientes (no ejecutables desde el agente)

El agrupado del menú lateral son **workspaces** del Host: una carpeta registrada. La pertenencia de una sesión se fija por la carpeta en la que se creó y no se puede reasignar desde la interfaz.

1. `Add workspace…` → `C:\Users\victo\Desktop\proyectos qt` (crea el grupo).
2. Opcional: `Add workspace…` → `C:\Users\victo\Desktop\proyectos qt\forgecraft` y `View options → Group by → Workspace Tree` para anidarlo.
3. Renombrar el grupo con `Rename` si se quiere otro nombre.
4. Las sesiones nuevas deben abrirse **desde** ese workspace para quedar archivadas en él.
