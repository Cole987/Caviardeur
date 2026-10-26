#!/bin/sh
# Construit dist/Caviardeur.exe pour Windows XP (32 bits), avec MuPDF lié statiquement.
#
# Prérequis : WinLibs i686 MSVCRT (winget install BrechtSanders.WinLibs.POSIX.MSVCRT --architecture x86)
# et MuPDF compilé dans ../outils/src/mupdf-1.28.5-source (voir LISEZMOI.md).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GCC_BIN="${GCC_BIN:-$HOME/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.MSVCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw32/bin}"
MUPDF="${MUPDF:-$HERE/../outils/src/mupdf-1.28.5-source}"
MUPDF_LIB="$MUPDF/build/release-tofu-tofu_cjk"
export PATH="$GCC_BIN:$PATH"

mkdir -p "$HERE/build" "$HERE/dist"
DEFS="-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D_WIN32_IE=0x0600"
CXXFLAGS="-O2 -std=gnu++17 -Wall -Wno-missing-field-initializers $DEFS -I$MUPDF/include"

cd "$HERE/src"
windres $DEFS -i app.rc -o "$HERE/build/app.o"
for f in text engine office main; do
	g++ $CXXFLAGS -c "$f.cpp" -o "$HERE/build/$f.o"
done
g++ -municode -mwindows -static -static-libgcc -static-libstdc++ -s \
	-o "$HERE/dist/Caviardeur.exe" \
	"$HERE/build/main.o" "$HERE/build/engine.o" "$HERE/build/office.o" "$HERE/build/text.o" "$HERE/build/app.o" \
	"$MUPDF_LIB/libmupdf.a" "$MUPDF_LIB/libmupdf-third.a" \
	-lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luuid -lmsimg32 -lshell32 -lgdi32 -luser32 -lkernel32
echo "OK : $HERE/dist/Caviardeur.exe"
