#!/bin/sh
# Removes what install.sh added, and nothing else:
#   ~/.local/opt/nimecraft/
#   ~/.local/share/icons/hicolor/256x256/apps/nimecraft.png
#   ~/.local/share/applications/nimecraft.desktop
# Your worlds and settings in ~/.local/share/nimecraft are kept.
set -eu

if [ -z "${HOME:-}" ]; then
    echo "uninstall.sh: HOME is not set" >&2
    exit 1
fi

icons="$HOME/.local/share/icons/hicolor"
apps="$HOME/.local/share/applications"

rm -rf "$HOME/.local/opt/nimecraft"
rm -f "$icons/256x256/apps/nimecraft.png"
rm -f "$apps/nimecraft.desktop"

if command -v update-desktop-database >/dev/null 2>&1 && [ -d "$apps" ]; then
    update-desktop-database "$apps" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -d "$icons" ]; then
    gtk-update-icon-cache -f -t -q "$icons" >/dev/null 2>&1 || true
fi

echo "Nimecraft was removed from your app menu."
echo "Your worlds and settings are still in ~/.local/share/nimecraft (delete that folder to remove them too)."
