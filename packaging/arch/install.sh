#!/usr/bin/env bash
# Installs Graphite on Arch Linux by building a pacman package and installing it.
set -euo pipefail    # stop at the first error

# Work from the folder this script lives in (where PKGBUILD is).
cd "$(dirname "${BASH_SOURCE[0]}")"

# This only works on Arch (or a derivative): pacman must exist.
command -v pacman >/dev/null || { echo "pacman not found: this installer is for Arch Linux." >&2; exit 1; }

# makepkg refuses to run as root (the build would be unsafe), so stop early with a clear message.
if [ "$(id -u)" -eq 0 ]; then
    echo "Do not run this as root. Run it as your normal user; sudo is asked for when needed." >&2
    exit 1
fi

# base-devel holds the build tools (gcc, make, ...) that makepkg needs.
if ! pacman -Qq base-devel >/dev/null 2>&1; then
    echo "The package group 'base-devel' is missing."
    read -r -p "Install it with 'sudo pacman -S --needed base-devel'? [y/N] " answer
    case "$answer" in
        [yY]*) sudo pacman -S --needed base-devel ;;   # --needed skips what is already installed
        *) echo "Cannot continue without base-devel."; exit 1 ;;
    esac
fi

# makepkg builds the package from PKGBUILD:
#   -s  syncs dependencies: installs the missing ones with pacman (asks for sudo)
#   -i  installs the finished package with pacman (asks for sudo)
makepkg -si

echo
echo "Done. Start it with: graphite ~/path/to/your/notes"
echo "Uninstall with:      sudo pacman -R graphite-planner"
