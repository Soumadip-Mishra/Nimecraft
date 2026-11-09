#!/bin/sh
# Install check for the Linux release (M8 Task 7), run locally and in CI. In a temporary HOME
# whose path contains a space: install.sh puts the game, icon, and menu entry in place with an
# absolute Exec= path; running it again upgrades in place (one copy); uninstall.sh removes exactly
# those three and keeps the saves. Exit 0 = all good.
#
# usage: sh packaging/test_install.sh [Nimecraft-x.y.z-Linux.tar.gz]   (default: the one in build/release)
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ $# -ge 1 ]; then
    archive=$1
else
    archive=$(ls "$repo"/build/release/Nimecraft-*-Linux.tar.gz 2>/dev/null | head -n 1)
fi
if [ -z "${archive:-}" ] || [ ! -f "$archive" ]; then
    echo "test_install: no release archive (build one with: cmake --build --preset release --target package)" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
home="$work/home with space"
mkdir -p "$home" "$work/release"
tar -xzf "$archive" -C "$work/release"
rel=$(find "$work/release" -mindepth 1 -maxdepth 1 -type d | head -n 1)

failures=0
fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

game="$home/.local/opt/nimecraft"
icon="$home/.local/share/icons/hicolor/256x256/apps/nimecraft.png"
entry="$home/.local/share/applications/nimecraft.desktop"
save="$home/.local/share/nimecraft/saves/x/world.meta"
mkdir -p "$(dirname "$save")"
printf 'version=1\n' >"$save"

check_installed() {
    [ -x "$game/nimecraft" ] || fail "$game/nimecraft is missing or not executable"
    [ -f "$game/README.txt" ] || fail "$game/README.txt is missing"
    [ -f "$icon" ] || fail "icon $icon is missing"
    if [ -f "$icon" ]; then
        head -c 8 "$icon" | od -An -tx1 | tr -d ' \n' | grep -q '^89504e470d0a1a0a$' || fail "the icon is not a PNG"
    fi
    [ -f "$entry" ] || {
        fail "menu entry $entry is missing"
        return
    }
    exec_line=$(grep '^Exec=' "$entry" || true)
    [ "$(grep -c '^Exec=' "$entry")" = 1 ] || fail "the menu entry needs exactly one Exec= line"
    exec_path=${exec_line#Exec=}
    exec_path=${exec_path#\"}
    exec_path=${exec_path%\"}
    case "$exec_path" in
        /*) ;;
        *) fail "Exec= is not an absolute path: $exec_line" ;;
    esac
    [ "$exec_path" = "$game/nimecraft" ] || fail "Exec= points at $exec_path, not $game/nimecraft"
    [ -x "$exec_path" ] || fail "Exec= target is not an executable file: $exec_path"
    grep -q '^Icon=nimecraft$' "$entry" || fail "Icon=nimecraft missing"
    grep -q '^Name=Nimecraft$' "$entry" || fail "Name=Nimecraft missing"
    grep -q '@EXEC@' "$entry" && fail "@EXEC@ was not replaced"
    if command -v desktop-file-validate >/dev/null 2>&1; then
        desktop-file-validate "$entry" || fail "desktop-file-validate reports errors"
    fi
}

echo "== install"
(cd "$rel" && HOME="$home" sh ./install.sh) || fail "install.sh exited with an error"
check_installed

echo "== install again (upgrade)"
(cd "$rel" && HOME="$home" sh ./install.sh) || fail "install.sh (second run) exited with an error"
check_installed
[ -z "$(find "$game" -mindepth 2 -name nimecraft)" ] || fail "a nested copy of the game appeared under $game"
[ "$(find "$home/.local/share/applications" -name '*.desktop' | wc -l)" -eq 1 ] || fail "more than one menu entry"
[ "$(find "$home/.local/share/icons" -name 'nimecraft*.png' | wc -l)" -eq 1 ] || fail "more than one icon"
leftovers=$(find "$home" -name '*.tmp' -o -name '*.new')
[ -z "$leftovers" ] || fail "temporary files left behind: $leftovers"

echo "== run from another folder"
(cd "$work" && HOME="$home" sh "$rel/install.sh") || fail "install.sh run by path from another folder failed"
check_installed

echo "== uninstall"
(cd "$work" && HOME="$home" sh "$rel/uninstall.sh") || fail "uninstall.sh exited with an error"
[ ! -e "$game" ] || fail "$game is still there"
[ ! -e "$icon" ] || fail "$icon is still there"
[ ! -e "$entry" ] || fail "$entry is still there"
[ -f "$save" ] || fail "the save $save was removed"
(cd "$work" && HOME="$home" sh "$rel/uninstall.sh") || fail "uninstall.sh twice is not harmless"
[ -f "$save" ] || fail "the save was removed by the second uninstall"

if [ "$failures" -eq 0 ]; then
    echo "OK: install, upgrade, and uninstall behave"
    exit 0
fi
echo "$failures check(s) failed"
exit 1
