#!/bin/sh
# Checks that a Nimecraft release archive holds exactly the expected files, inside one top folder
# named like the archive (Nimecraft-<version>-Linux/ or Nimecraft-<version>-win64/). Files under
# assets/ are allowed; anything else extra, or anything missing, fails. On the Linux archive the
# game must also be executable. Lists the archive first. Exit 0 = good, 1 = bad, 2 = usage.
#
# usage: sh packaging/check_package.sh <Nimecraft-x.y.z-Linux.tar.gz | Nimecraft-x.y.z-win64.zip>
set -eu

if [ $# -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: sh packaging/check_package.sh <archive.tar.gz|archive.zip>" >&2
    exit 2
fi
archive=$1
name=$(basename "$archive")

case "$name" in
    *-Linux.tar.gz)
        top=${name%.tar.gz}
        expected="nimecraft README.txt install.sh uninstall.sh nimecraft.desktop.in"
        listing=$(tar -tvzf "$archive")
        entries=$(tar -tzf "$archive")
        ;;
    *-win64.zip)
        top=${name%.zip}
        expected="nimecraft.exe README.txt"
        # The first Python that really runs (on Windows "python3" can be a Store stub that does not).
        py=""
        for candidate in python3 python py; do
            if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import zipfile' >/dev/null 2>&1; then
                py=$candidate
                break
            fi
        done
        if [ -z "$py" ]; then
            echo "check_package: python3 is needed to read zip files" >&2
            exit 2
        fi
        entries=$("$py" -c 'import sys, zipfile; print("\n".join(zipfile.ZipFile(sys.argv[1]).namelist()))' "$archive" | tr -d '\r')
        listing=$entries
        ;;
    *)
        echo "check_package: $name is neither a *-Linux.tar.gz nor a *-win64.zip release" >&2
        exit 2
        ;;
esac

echo "== $name"
echo "$listing"

status=0
found=""
for entry in $entries; do
    case "$entry" in
        "$top"/) continue ;;  # the top folder itself
        "$top"/*) rel=${entry#"$top"/} ;;
        *)
            echo "FAIL: $entry is outside the top folder $top/"
            status=1
            continue
            ;;
    esac
    case "$rel" in
        */) continue ;;  # a folder entry
        assets/*) continue ;;
    esac
    ok=0
    for want in $expected; do
        [ "$rel" = "$want" ] && ok=1
    done
    if [ $ok -eq 0 ]; then
        echo "FAIL: unexpected file $rel"
        status=1
    fi
    found="$found $rel"
done

for want in $expected; do
    case " $found " in
        *" $want "*) ;;
        *)
            echo "FAIL: missing $want"
            status=1
            ;;
    esac
done

case "$name" in
    *-Linux.tar.gz)
        mode=$(echo "$listing" | awk -v f="$top/nimecraft" '$NF == f { print substr($1, 1, 4) }')
        if [ "$mode" != "-rwx" ]; then
            echo "FAIL: $top/nimecraft is not an executable file (mode ${mode:-missing})"
            status=1
        fi
        ;;
esac

if [ $status -eq 0 ]; then echo "OK: $name holds exactly the expected files"; fi
exit $status
