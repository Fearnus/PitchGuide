#!/usr/bin/env bash
# Builds a self-extracting Linux installer (.run) for PitchGuide, bundling the
# standalone app, the VST3 plugin, and all required models/resources.
#
# Usage: packaging/linux/build_installer.sh [build-dir]
#
# Requires: patchelf, makeself (apt install patchelf makeself)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$(cd "${1:-$ROOT_DIR/build}" && pwd)"

VERSION="$(grep -oP 'project\(PitchGuide VERSION \K[0-9.]+' "$ROOT_DIR/CMakeLists.txt")"
ARCH="x86_64"
APP_NAME="PitchGuide"
COMPANY_DIR="Fearnus"
INSTALL_PREFIX="/opt/${COMPANY_DIR}/${APP_NAME}"

STANDALONE_BIN="$BUILD_DIR/PitchGuide_artefacts/Release/PitchGuide"
VST3_BUNDLE="$BUILD_DIR/PitchGuidePlugin_artefacts/Release/VST3/PitchGuide.vst3"
ONNX_LIB_DIR="$BUILD_DIR/_deps/onnxruntime-1.19.2-src/lib"

for f in "$STANDALONE_BIN" "$VST3_BUNDLE" "$ONNX_LIB_DIR"; do
    if [ ! -e "$f" ]; then
        echo "Missing build artifact: $f (build the project first: cmake --build \"$BUILD_DIR\" --config Release)" >&2
        exit 1
    fi
done

command -v patchelf >/dev/null || { echo "patchelf is required: sudo apt install patchelf" >&2; exit 1; }
command -v makeself >/dev/null || { echo "makeself is required: sudo apt install makeself" >&2; exit 1; }

STAGE="$BUILD_DIR/installer_stage"
rm -rf "$STAGE"
PAYLOAD="$STAGE/payload"
APP_DIR="$PAYLOAD/opt/${COMPANY_DIR}/${APP_NAME}"

mkdir -p "$APP_DIR/lib"
mkdir -p "$PAYLOAD/vst3"
mkdir -p "$PAYLOAD/usr/share/applications"
mkdir -p "$PAYLOAD/usr/share/icons/hicolor/512x512/apps"

echo "==> Staging standalone app"
cp "$STANDALONE_BIN" "$APP_DIR/PitchGuide"
chmod 755 "$APP_DIR/PitchGuide"

echo "==> Staging ONNX Runtime"
cp -P "$ONNX_LIB_DIR/libonnxruntime.so" "$ONNX_LIB_DIR/libonnxruntime.so.1" "$APP_DIR/lib/"
cp "$ONNX_LIB_DIR"/libonnxruntime.so.1.*.* "$APP_DIR/lib/"
cp "$ONNX_LIB_DIR/libonnxruntime_providers_shared.so" "$APP_DIR/lib/"

echo "==> Staging models and resources"
cp -r "$ROOT_DIR/Resources/models" "$APP_DIR/models"
cp -r "$ROOT_DIR/Resources/lang" "$APP_DIR/lang"
cp -r "$ROOT_DIR/Resources/fonts" "$APP_DIR/fonts"

echo "==> Fixing standalone RPATH"
patchelf --set-rpath '$ORIGIN/lib' "$APP_DIR/PitchGuide"

echo "==> Staging VST3 plugin"
cp -r "$VST3_BUNDLE" "$PAYLOAD/vst3/PitchGuide.vst3"
PLUGIN_SO="$PAYLOAD/vst3/PitchGuide.vst3/Contents/x86_64-linux/PitchGuide.so"
patchelf --set-rpath "${INSTALL_PREFIX}/lib" "$PLUGIN_SO"

echo "==> Staging desktop integration"
cp "$ROOT_DIR/Resources/images/icon.png" "$PAYLOAD/usr/share/icons/hicolor/512x512/apps/pitchguide.png"
cat > "$PAYLOAD/usr/share/applications/pitchguide.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=PitchGuide
Comment=Vocal pitch editor
Exec=/usr/local/bin/pitchguide %f
Icon=pitchguide
Terminal=false
Categories=AudioVideo;Audio;Music;
EOF

echo "==> Writing install script"
# Fully-quoted template: nothing here is expanded by *this* shell. Build-time
# values are substituted afterward via sed, at the __PLACEHOLDER__ tokens.
cat > "$STAGE/install.sh" <<'INSTALL_EOF'
#!/usr/bin/env bash
set -euo pipefail
umask 022

APP_NAME="__APP_NAME__"
COMPANY_DIR="__COMPANY_DIR__"
VERSION="__VERSION__"
INSTALL_PREFIX="__INSTALL_PREFIX__"

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer must be run as root, e.g.: sudo ./$(basename "$0")" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PAYLOAD="$SCRIPT_DIR/payload"

echo "Installing PitchGuide ${VERSION}..."

mkdir -p "/opt/${COMPANY_DIR}"
chmod 755 "/opt/${COMPANY_DIR}"
rm -rf "${INSTALL_PREFIX}"
cp -r "$PAYLOAD/opt/${COMPANY_DIR}/${APP_NAME}" "${INSTALL_PREFIX}"
chmod -R a+rX "${INSTALL_PREFIX}"
chmod 755 "${INSTALL_PREFIX}/PitchGuide"

if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
    VST3_USER="${SUDO_USER}"
else
    VST3_USER="$(logname 2>/dev/null || echo root)"
fi
VST3_HOME="$(getent passwd "${VST3_USER}" | cut -d: -f6)"
if [ -z "${VST3_HOME}" ]; then
    echo "Warning: could not resolve home directory for user '${VST3_USER}'; skipping VST3 install." >&2
else
    VST3_DIR="${VST3_HOME}/.vst3"
    mkdir -p "${VST3_DIR}"
    rm -rf "${VST3_DIR}/PitchGuide.vst3"
    cp -r "$PAYLOAD/vst3/PitchGuide.vst3" "${VST3_DIR}/PitchGuide.vst3"
    chown -R "${VST3_USER}:$(id -gn "${VST3_USER}")" "${VST3_DIR}/PitchGuide.vst3"
    chmod -R u+rwX,go+rX "${VST3_DIR}/PitchGuide.vst3"
fi

mkdir -p /usr/share/applications /usr/share/icons/hicolor/512x512/apps
chmod 755 /usr/share/applications /usr/share/icons/hicolor/512x512/apps
cp "$PAYLOAD/usr/share/applications/pitchguide.desktop" /usr/share/applications/pitchguide.desktop
cp "$PAYLOAD/usr/share/icons/hicolor/512x512/apps/pitchguide.png" /usr/share/icons/hicolor/512x512/apps/pitchguide.png
chmod a+r /usr/share/applications/pitchguide.desktop /usr/share/icons/hicolor/512x512/apps/pitchguide.png

ln -sf "${INSTALL_PREFIX}/PitchGuide" /usr/local/bin/pitchguide

cat > "${INSTALL_PREFIX}/uninstall.sh" <<UNINSTALL_EOF
#!/usr/bin/env bash
set -euo pipefail
if [ "\$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo ${INSTALL_PREFIX}/uninstall.sh" >&2
    exit 1
fi
rm -rf "${INSTALL_PREFIX}"
if [ -n "${VST3_HOME}" ]; then
    rm -rf "${VST3_HOME}/.vst3/PitchGuide.vst3"
fi
rm -f /usr/local/bin/pitchguide
rm -f /usr/share/applications/pitchguide.desktop
rm -f /usr/share/icons/hicolor/512x512/apps/pitchguide.png
echo "PitchGuide uninstalled."
UNINSTALL_EOF
chmod +x "${INSTALL_PREFIX}/uninstall.sh"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null 2>&1 || true

for lib in libasound.so.2 libfontconfig.so.1 libfreetype.so.6; do
    found=""
    for dir in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /lib/x86_64-linux-gnu /lib; do
        if [ -e "$dir/$lib" ]; then
            found=1
            break
        fi
    done
    if [ -z "$found" ]; then
        echo "Warning: $lib not found on this system - install your distro's ALSA/fontconfig/freetype packages." >&2
    fi
done

echo ""
echo "PitchGuide ${VERSION} installed."
echo "  Standalone: run 'pitchguide' or find PitchGuide in your applications menu."
if [ -n "${VST3_HOME}" ]; then
    echo "  VST3 plugin: ${VST3_HOME}/.vst3/PitchGuide.vst3"
fi
echo "  Uninstall:   sudo ${INSTALL_PREFIX}/uninstall.sh"
INSTALL_EOF

sed -i \
    -e "s|__APP_NAME__|${APP_NAME}|g" \
    -e "s|__COMPANY_DIR__|${COMPANY_DIR}|g" \
    -e "s|__VERSION__|${VERSION}|g" \
    -e "s|__INSTALL_PREFIX__|${INSTALL_PREFIX}|g" \
    "$STAGE/install.sh"
chmod +x "$STAGE/install.sh"

echo "==> Packaging with makeself"
mkdir -p "$ROOT_DIR/dist"
OUT="$ROOT_DIR/dist/PitchGuide-Linux-${ARCH}.run"
makeself --gzip "$STAGE" "$OUT" "PitchGuide ${VERSION} Installer" ./install.sh

echo ""
echo "Installer created: $OUT"
