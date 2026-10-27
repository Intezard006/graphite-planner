Graphite - install on Arch Linux
================================

1. Download graphite-planner-<version>-arch.tar.gz.
2. Unpack it:      tar -xzf graphite-planner-<version>-arch.tar.gz
                   (x = extract, z = gunzip, f = from this file)
3. Install it:     cd graphite-planner-<version>-arch && ./install.sh

The script checks you are on Arch and not root, offers to install base-devel if it is
missing, then runs "makepkg -si": it builds Graphite, runs its tests, installs the
dependencies with pacman (-s) and installs the package (-i). sudo asks for your password.
The first build takes a few minutes.

What is installed
-----------------
  /usr/bin/graphite                         the program
  /usr/share/graphite/tex/                  fonts and data for formulas
  /usr/share/applications/graphite.desktop  launcher entry for rofi / app menus
  /usr/share/licenses/graphite-planner/     the MIT licence

Start it:  graphite ~/Notes     (any folder; a plain folder of .md files works)

Uninstall
---------
  sudo pacman -R graphite-planner
Your notes and settings (~/.config) are never touched.

Good to know
------------
- If you also installed Graphite by hand into ~/.local (install.sh from the source
  repository), ~/.local/bin/graphite comes earlier in PATH and shadows /usr/bin/graphite.
  Remove the old one (that install.sh --uninstall) or call /usr/bin/graphite explicitly.
- Evangelion sidebar art is not included (it is copyrighted). In Settings > Sidebar unit
  you can download it with your consent, or pick your own PNG.
- The in-app updater is off in this package: update by installing a newer tarball.
- Optional extras: ttf-inter (body font), libnotify (timer notifications),
  hyprland (blur meter).
