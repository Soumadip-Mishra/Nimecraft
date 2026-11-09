#!/bin/sh
# Adds Nimecraft to your desktop's app menu, for your user only (no sudo). Run it from the unpacked
# release folder (sh install.sh); run it again from a newer release to upgrade in place.
#   game:       ~/.local/opt/nimecraft/
#   icon:       ~/.local/share/icons/hicolor/256x256/apps/nimecraft.png
#   menu entry: ~/.local/share/applications/nimecraft.desktop
# Your worlds and settings stay in ~/.local/share/nimecraft; uninstall.sh never touches them.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${HOME:-}" ]; then
    echo "install.sh: HOME is not set" >&2
    exit 1
fi
for f in nimecraft README.txt nimecraft.desktop.in; do
    if [ ! -f "$here/$f" ]; then
        echo "install.sh: $f is missing; run this script from the unpacked release folder" >&2
        exit 1
    fi
done

dest="$HOME/.local/opt/nimecraft"
icons="$HOME/.local/share/icons/hicolor"
icon_dir="$icons/256x256/apps"
apps="$HOME/.local/share/applications"

# The menu entry quotes the path, so spaces are fine; these characters would need escaping that
# desktop environments handle unevenly, so refuse them instead.
case "$dest" in
    *'"'* | *'`'* | *'$'* | *'\'* | *'%'*)
        echo "install.sh: the install folder $dest contains \" \` \$ \\ or %, which a menu entry cannot hold" >&2
        exit 1
        ;;
esac

mkdir -p "$dest" "$icon_dir" "$apps"

# The game: copied beside the old one and renamed over it, so a running copy is not disturbed.
cp "$here/nimecraft" "$dest/nimecraft.new"
chmod 755 "$dest/nimecraft.new"
mv -f "$dest/nimecraft.new" "$dest/nimecraft"
cp "$here/README.txt" "$dest/README.txt"
rm -rf "$dest/assets"
if [ -d "$here/assets" ]; then cp -R "$here/assets" "$dest/assets"; fi

# The icon, drawn by the game itself.
"$dest/nimecraft" --export-icon "$icon_dir/nimecraft.png"

# The menu entry, with @EXEC@ replaced by the absolute path to the game.
exec_value="\"$dest/nimecraft\""
tmp="$apps/nimecraft.desktop.tmp"
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        *@EXEC@*) printf '%s%s%s\n' "${line%%@EXEC@*}" "$exec_value" "${line#*@EXEC@}" ;;
        *) printf '%s\n' "$line" ;;
    esac
done <"$here/nimecraft.desktop.in" >"$tmp"
chmod 644 "$tmp"
mv -f "$tmp" "$apps/nimecraft.desktop"

# Refresh the menu and icon caches where those tools exist.
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t -q "$icons" >/dev/null 2>&1 || true
fi

echo "Nimecraft is installed in $dest"
echo "Find it in your app menu by searching \"Nimecraft\"."
echo "Your worlds and settings stay in ~/.local/share/nimecraft. To remove the game: sh uninstall.sh"
