#!/bin/bash
# Gera o Paint.AppImage a partir do binário nativo compilado.
#
# Estratégia "thin": empacota apenas o binário (sem embutir o Qt), usando
# o Qt6 do sistema. No CachyOS (glibc novo) embutir o Qt via linuxdeploy
# causa crash na inicialização; usar o Qt do sistema é estável e gera um
# AppImage muito menor (~2 MB).
#
# Requisito para rodar o AppImage: ter qt6-base/qt6-svg instalados.

set -e
cd "$(dirname "$0")/.."

ROOT="$PWD"
APPDIR="$ROOT/AppDirThin"
TOOL="$ROOT/packaging/appimagetool-x86_64.AppImage"

echo ">> Compilando (Release)..."
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

echo ">> Montando AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/512x512/apps"
cp build/gnome-paint "$APPDIR/usr/bin/gnome-paint"

# .desktop: tanto na raiz (lido pelos gerenciadores) quanto no caminho padrão.
cp packaging/gnome-paint.desktop "$APPDIR/gnome-paint.desktop"
cp packaging/gnome-paint.desktop "$APPDIR/usr/share/applications/gnome-paint.desktop"

# Ícone: na raiz (.DirIcon + <icon-name>.png) e no tema hicolor.
cp packaging/gnome-paint.png "$APPDIR/gnome-paint.png"
cp packaging/gnome-paint.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/gnome-paint.png"
cp packaging/gnome-paint.png "$APPDIR/.DirIcon"

cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
exec "${HERE}/usr/bin/gnome-paint" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo ">> Empacotando..."
rm -f "$ROOT/Paint.AppImage"
ARCH=x86_64 "$TOOL" "$APPDIR" "$ROOT/Paint.AppImage" >/dev/null 2>&1
chmod +x "$ROOT/Paint.AppImage"
rm -rf "$APPDIR"

echo ">> Pronto: $ROOT/Paint.AppImage"
ls -la "$ROOT/Paint.AppImage"
