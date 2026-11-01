# MP3 libraries shipped with ForgeCraft

These unmodified libraries are loaded dynamically as separate DLLs. They can be replaced with API-compatible builds. ForgeCraft does not use the decoding functions in LAME.

## LAME 3.100

- Encoder: `libmp3lame-0.dll`, MSYS2 package `mingw-w64-x86_64-lame 3.100-3`.
- Header: `lame.h`, copyright (c) 1999 Mark Taylor and contributors.
- GNU Library General Public License version 2 or, at your option, any later version. The included LGPL 2.1 text may be used under that later-version option.
- Project and source code: https://lame.sourceforge.io/
- Release source: https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download
- MSYS2 build recipe and patches: https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-lame

## mpg123 1.33.7

- Decoder: `libmpg123-0.dll`, MSYS2 package `mingw-w64-x86_64-mpg123 1.33.7-1`.
- Headers: `mpg123.h`, `fmt123.h`, copyright by the mpg123 project.
- Copyright (c) 1995–2020 Michael Hipp and others. Free software under the GNU Lesser General Public License version 2.1.
- Project and release source archives: https://www.mpg123.de/download/
- MSYS2 build recipe and patches: https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-mpg123

The complete license and copyright notice from mpg123, including LGPL 2.1, is in `COPYING.txt`. Both libraries are provided without warranty under their respective licenses. Their original header copyright notices are retained in `third_party/mp3/include` in the ForgeCraft source tree.
