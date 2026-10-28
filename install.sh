#!/usr/bin/env bash
# Graphite installer: builds this checkout, removes older installed copies, installs the new one.
#
#   ./install.sh              build, test, replace any old install, install to ~/.local
#   ./install.sh --yes        same, without asking before removing old files
#   ./install.sh --skip-tests don't run the test suite (faster)
#   ./install.sh --uninstall  remove Graphite (your notes and settings are never touched)
#
# Only program files are removed: the binary, its TeX fonts, the launcher entry. Your graph,
# ~/.config/graphite (settings) and any fonts you installed yourself stay where they are.
set -euo pipefail

PREFIX="${GRAPHITE_PREFIX:-$HOME/.local}"   # GRAPHITE_PREFIX: install somewhere else (testing)
SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SRC/build"
SHARE="$PREFIX/share/graphite"
DESKTOP="$PREFIX/share/applications/graphite.desktop"
ASSUME_YES=0
SKIP_TESTS=0
UNINSTALL=0

for arg in "$@"; do
    case "$arg" in
        --yes|-y) ASSUME_YES=1 ;;
        --skip-tests) SKIP_TESTS=1 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)"; exit 1 ;;
    esac
done

say()  { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*"; }
ask()  { # ask "question" -> 0 for yes
    [ "$ASSUME_YES" = 1 ] && return 0
    read -r -p "$1 [y/N] " reply
    [[ "$reply" =~ ^[Yy] ]]
}

# The version of an installed copy, without ever running it: builds before 0.3.0 have no
# --version flag and would open a window. Installs from this script leave a VERSION file.
installed_version() {
    local bin="$1" share="$(dirname "$(dirname "$1")")/share/graphite"
    if [ -f "$share/VERSION" ]; then cat "$share/VERSION"; else echo "older than 0.3.0"; fi
}

# ---------------------------------------------------------------- find old copies
find_old() {
    OLD_FILES=()
    for bin in "$PREFIX/bin/graphite" /usr/local/bin/graphite; do
        [ -e "$bin" ] && OLD_FILES+=("$bin")
    done
    # Anything a previous run of this script recorded (exact file list from CMake).
    if [ -f "$SHARE/install-manifest.txt" ]; then
        while IFS= read -r f; do
            [[ "$f" == "$SHARE"/* ]] && continue  # covered by removing $SHARE itself
            [ -e "$f" ] && OLD_FILES+=("$f")
        done < "$SHARE/install-manifest.txt"
    fi
    for d in "$SHARE" /usr/local/share/graphite; do [ -d "$d" ] && OLD_FILES+=("$d"); done
    [ -e "$DESKTOP" ] && OLD_FILES+=("$DESKTOP")
    # Unique, in order.
    if [ ${#OLD_FILES[@]} -gt 0 ]; then
        mapfile -t OLD_FILES < <(printf '%s\n' "${OLD_FILES[@]}" | awk '!seen[$0]++')
    fi
}

remove_old() {
    [ ${#OLD_FILES[@]} -eq 0 ] && return 0
    for f in "${OLD_FILES[@]}"; do
        if [[ "$f" == /usr/* ]]; then sudo rm -rf -- "$f"; else rm -rf -- "$f"; fi
    done
}

# A copy installed by pacman belongs to pacman: never delete those files by hand.
if pacman -Qqo /usr/bin/graphite >/dev/null 2>&1; then
    warn "/usr/bin/graphite belongs to the package '$(pacman -Qqo /usr/bin/graphite)'; remove it with: sudo pacman -R $(pacman -Qqo /usr/bin/graphite)"
fi

if pgrep -x graphite >/dev/null; then
    warn "Graphite is running. It keeps working, but close and reopen it afterwards to get the new version."
fi

# ------------------------------------------------------------------------ uninstall
if [ "$UNINSTALL" = 1 ]; then
    find_old
    if [ ${#OLD_FILES[@]} -eq 0 ]; then say "Nothing to remove."; exit 0; fi
    say "This removes:"; printf '    %s\n' "${OLD_FILES[@]}"
    ask "Remove these files?" || { echo "Nothing changed."; exit 0; }
    remove_old
    say "Graphite removed. Your notes and ~/.config/graphite were not touched."
    exit 0
fi

# ---------------------------------------------------------------- 1. dependencies
say "Checking build tools"
need=(qt6-base qt6-wayland cmake ninja gcc)
missing=()
for p in "${need[@]}"; do pacman -Qq "$p" >/dev/null 2>&1 || missing+=("$p"); done
if [ ${#missing[@]} -gt 0 ]; then
    warn "Missing packages: ${missing[*]}"
    if ask "Install them with 'sudo pacman -S --needed ${missing[*]}'?"; then
        sudo pacman -S --needed "${missing[@]}"
    else
        echo "Cannot build without them."; exit 1
    fi
fi

# --------------------------------------------------------------------- 2. build
# MicroTeX (the formula typesetter) is a git submodule: a clone made without --recursive has an
# empty third_party/microtex. Fetch it (git submodule update --init downloads the recorded commit).
if [ ! -f "$SRC/third_party/microtex/src/latex.cpp" ]; then
    if [ -d "$SRC/.git" ] || [ -f "$SRC/.git" ]; then
        say "Fetching MicroTeX (git submodule)"
        git -C "$SRC" submodule update --init third_party/microtex
    else
        echo "third_party/microtex is empty and this is not a git checkout. Clone with:"
        echo "  git clone --recursive https://github.com/Intezard006/graphite-planner.git"
        exit 1
    fi
fi

say "Building (Release)"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD"
NEW_VERSION="$("$BUILD/graphite" --version | awk '{print $2}')"

if [ "$SKIP_TESTS" = 0 ]; then
    say "Running the tests"
    ctest --test-dir "$BUILD" --output-on-failure || { warn "Tests failed: not installing. (--skip-tests to force)"; exit 1; }
fi

# ------------------------------------------------------------ 3. old versions
find_old
if [ ${#OLD_FILES[@]} -gt 0 ]; then
    say "Found an installed Graphite ($(installed_version "$PREFIX/bin/graphite")), replacing it with $NEW_VERSION:"
    printf '    %s\n' "${OLD_FILES[@]}"
    ask "Remove these old files?" || { echo "Nothing changed."; exit 0; }
    remove_old
fi
for d in "$HOME"/graphite_0.* ; do
    [ -d "$d" ] && warn "Old source folder $d left alone (delete it yourself if you no longer need it)."
done

# ------------------------------------------------------------------- 4. install
say "Installing Graphite $NEW_VERSION to $PREFIX"
cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null
mkdir -p "$SHARE" "$(dirname "$DESKTOP")"
echo "$NEW_VERSION" > "$SHARE/VERSION"
cp "$BUILD/install_manifest.txt" "$SHARE/install-manifest.txt"

# A launcher entry, so it shows up in rofi / the app menu.
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=Graphite
Comment=Notes and journals (Logseq graphs)
Exec=$PREFIX/bin/graphite %f
Icon=accessories-text-editor
Terminal=false
Categories=Office;Education;
StartupWMClass=graphite
EOF
echo "$DESKTOP" >> "$SHARE/install-manifest.txt"

# -------------------------------------------------------------------- 5. check
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) warn "$PREFIX/bin is not on your PATH. In fish, run once:  fish_add_path ~/.local/bin" ;;
esac
say "Done: $("$PREFIX/bin/graphite" --version). Start it with:  graphite ~/Notes  (any folder of notes)"
