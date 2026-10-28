# Graphite

**A fast, native day planner and outliner for plain Markdown notes, with chemistry and maths built in.**

[![Linux build](https://github.com/Intezard006/graphite-planner/actions/workflows/linux.yml/badge.svg)](https://github.com/Intezard006/graphite-planner/actions/workflows/linux.yml)
[![macOS build](https://github.com/Intezard006/graphite-planner/actions/workflows/macos.yml/badge.svg)](https://github.com/Intezard006/graphite-planner/actions/workflows/macos.yml)
[![Release](https://img.shields.io/github/v/release/Intezard006/graphite-planner?label=release)](https://github.com/Intezard006/graphite-planner/releases)
[![Licence: MIT](https://img.shields.io/badge/licence-MIT-blue.svg)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%C2%B7%20macOS%20(Apple%20Silicon)-lightgrey)
![Qt 6 · C++17](https://img.shields.io/badge/Qt%206-C%2B%2B17-41cd52)

Graphite keeps your journals and pages as ordinary `.md` files in ordinary folders, in the
format Logseq uses, so an existing Logseq graph opens as it is. It was written as a light
replacement for Electron-based note apps. It uses about 25–30 MB of memory, starts instantly
and uses no CPU while idle, so it is easy on a laptop battery. Its look takes a few cues from
*Neon Genesis Evangelion*; one switch in Settings turns those off.

- **Plan the day**: tasks (`TODO` / `DOING` / `DONE`), deadlines with countdowns, time blocks
  with a timeline, a weekly timetable, subtasks that finish their heading, progress bars and
  focus timers.
- **Write science**: inline LaTeX (`$…$`) and chemistry (`\ce{…}`), molar masses, equation
  balancing, oxidation states, quick maths with units (`= 2.5 g / M(CuSO4) to mmol`), lab tables
  with worked-out columns, plots with fits and function graphs.
- **Keep notes**: books and pages, `[[links]]`, `#tags`, backlinks, full-text search, images,
  PDF export.
- **Trust your files**: only the blocks you edit are rewritten. Everything else stays byte for
  byte, and a backup is kept before the first change.

## Contents

- [Install](#install)
  - [macOS (Apple Silicon)](#macos-apple-silicon)
  - [Arch Linux: package](#arch-linux-package)
  - [Linux: from source](#linux-from-source)
  - [Updating and uninstalling](#updating-and-uninstalling)
- [First steps](#first-steps)
- [Building for development](#building-for-development)
- [Project layout](#project-layout)
- [Contributing](#contributing)
- [Licence and credits](#licence-and-credits)

The full feature reference is the **[user guide](docs/guide.md)**, and the changes in each
version are in the **[changelog](CHANGELOG.md)**.

## Install

| You have | Do this | Time |
|---|---|---|
| A Mac with Apple Silicon (M1–M4) | [Download the app](#option-a-download-the-app), or [build it](#option-b-build-it-yourself) | 2 min / 15 min |
| Arch Linux (or Manjaro, EndeavourOS…) | [Install the package](#arch-linux-package) | 5 min |
| Another Linux | [Build from source](#linux-from-source) | 10 min |

Each command below is followed by what it does. Type the commands in a terminal; lines
starting with `#` are comments and need not be typed.

### macOS (Apple Silicon)

#### Option A: download the app

1. Open the [Releases page](https://github.com/Intezard006/graphite-planner/releases) and
   download `Graphite-<version>-macos-arm64.dmg`.
2. Open the `.dmg` and drag **Graphite** onto **Applications**.
3. The app isn't signed with a paid Apple certificate, so macOS blocks the first start.
   Right-click Graphite in Applications, choose **Open**, then **Open** again. Or run this
   once in Terminal:

       xattr -dr com.apple.quarantine /Applications/Graphite.app

   `xattr` edits a file's extended attributes: `-d` deletes the named one (the "downloaded
   from the internet" mark, `com.apple.quarantine`), and `-r` does it for every file inside
   the app.

#### Option B: build it yourself

You need Apple's command-line tools and [Homebrew](https://brew.sh).

```sh
xcode-select --install
```
Installs Apple's compiler and `git`. A window opens; click **Install**.

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
Installs Homebrew, the macOS package manager (skip this if `brew --version` already works).
`curl -fsSL` downloads the installer quietly (`-f` fails on HTTP errors, `-s` hides progress,
`-S` still shows errors, `-L` follows redirects), and `bash -c` runs it.

```sh
brew install qt cmake ninja
```
Installs Qt 6 (the toolkit Graphite is built on), CMake (which configures the build) and Ninja
(which runs it).

```sh
git clone --recursive https://github.com/Intezard006/graphite-planner.git
cd graphite-planner
```
Downloads the source. `--recursive` also downloads MicroTeX, the formula typesetter, which
lives in its own repository (a *git submodule*). `cd` moves into the new folder.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```
The first line configures: `-S .` says the source is here, `-B build` puts the build in a
`build` folder, `-G Ninja` uses Ninja, `-DCMAKE_BUILD_TYPE=Release` turns on optimisations, and
`-DCMAKE_PREFIX_PATH=…` tells CMake where Homebrew put Qt. The second line compiles, which
takes a few minutes.

```sh
open build/Graphite.app
```
Starts it. To keep it, drag `build/Graphite.app` into Applications. Use
`/opt/homebrew/opt/qt/bin/macdeployqt build/Graphite.app` first if you want the app to run
without Homebrew's Qt installed.

On a Mac, **⌘ takes the place of Ctrl** and **⌥ (Option) the place of Alt**: Ctrl+K is ⌘K.
The window blur and wallpaper colours are Linux-only; everything else works.

### Arch Linux: package

1. Download `graphite-planner-<version>-arch.tar.gz` from the
   [Releases page](https://github.com/Intezard006/graphite-planner/releases).
2. Unpack it and run its installer:

   ```sh
   tar -xzf graphite-planner-*-arch.tar.gz
   cd graphite-planner-*-arch
   ./install.sh
   ```
   `tar` unpacks archives: `-x` extracts, `-z` un-gzips, `-f` names the file. The installer
   builds a real pacman package with `makepkg -si` (`-s` installs missing dependencies with
   pacman, `-i` installs the finished package), so pacman knows about every file. sudo asks
   for your password.

3. Start it from your app launcher, or with `graphite ~/Notes` (any folder of notes).

### Linux: from source

**Arch**: one script checks the tools, builds, runs the tests and installs to `~/.local`:

```sh
git clone --recursive https://github.com/Intezard006/graphite-planner.git
cd graphite-planner
./install.sh
```
`git clone --recursive` downloads the source and MicroTeX, its formula typesetter (a git
submodule). `./install.sh` offers to install missing build tools with pacman, builds, tests,
shows any older Graphite it will replace and asks first, then installs `~/.local/bin/graphite`
and a launcher entry. Options: `--yes` skips the question, `--skip-tests` skips the tests,
`--uninstall` removes Graphite.

**Other distributions** (untested, but the same build): install a C++17 compiler, CMake ≥ 3.21,
Ninja and the Qt 6 development packages, then build with CMake.

```sh
# Debian / Ubuntu (24.04 or newer)
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-wayland git
# Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtwayland git
```

```sh
git clone --recursive https://github.com/Intezard006/graphite-planner.git
cd graphite-planner
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/.local
```
`cmake -S . -B build` configures a build of this folder into `build/`, `cmake --build build`
compiles, and `cmake --install … --prefix ~/.local` copies the program to `~/.local/bin` and its
formula fonts to `~/.local/share/graphite`. Make sure `~/.local/bin` is on your `PATH`.

Already cloned without `--recursive`? Run `git submodule update --init` in the folder. It
downloads the missing MicroTeX (`--init` sets up the submodule the first time).

**Optional extras**: the Inter font (body text), `libnotify` (desktop notifications when a
timer ends), and Hyprland (blur meter in transparent mode).

### Updating and uninstalling

| Installed with | Update | Uninstall |
|---|---|---|
| macOS app | Install the newer `.dmg` over it | Drag Graphite from Applications to the Bin |
| Arch package | Install the newer tarball the same way | `sudo pacman -R graphite-planner` |
| `install.sh` | `git pull --recurse-submodules`, then `./install.sh` | `./install.sh --uninstall` |
| CMake by hand | `git pull --recurse-submodules`, then the build commands again | delete `~/.local/bin/graphite` and `~/.local/share/graphite` |

`git pull --recurse-submodules` fetches the newest source and updates MicroTeX along with it.
A source install also notices changes by itself: once the folder it was built from is newer
than the program, the sidebar shows **Refit ready** and Settings › Updates can rebuild and
restart it.

Your notes and settings are never touched by updating or uninstalling. Settings live in
`~/.config/graphite/` on Linux and `~/Library/Preferences/` on macOS.

## First steps

1. **Pick a folder.** At the first start Graphite asks for a folder of notes: an empty one
   starts a new *tree*, a Logseq graph opens as it is. Each subfolder becomes a *book*, each
   `.md` file a *page*, and `journals/` holds one page per day. `graphite FOLDER` opens a
   folder directly; with no folder, Graphite reopens the last one.
2. **Press F2** for the field manual: short interactive drills with a working copy of the
   editor, in your browser.
3. **Press Ctrl+/** (or F1) for every key. The essentials:

   | Key | Does |
   |---|---|
   | Enter / Tab / Shift+Tab | new block / indent / outdent |
   | Ctrl+Enter | cycle TODO → DOING → DONE |
   | Ctrl+K | search pages and notes |
   | Ctrl+J | today's journal |
   | Ctrl+D | today's timeline |
   | Ctrl+Shift+T | every open task |
   | `/` | command menu (headings, dates, formulas, tables, timers…) |
   | Ctrl+, | settings |

4. **Read the [user guide](docs/guide.md)** when you want to know everything a feature can do.

## Building for development

```sh
git clone --recursive https://github.com/Intezard006/graphite-planner.git
cd graphite-planner
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/graphite ~/Notes
```

`ctest` runs five test programs: `core` (the Markdown parser and writer, including fuzzed
round trips), `palette`, `index` (links, tasks, maths, chemistry rules), `chem` (the `\ce`
converter and typesetting) and `ui`, which drives the real window headlessly
(`QT_QPA_PLATFORM=offscreen`). The tests use temporary folders for settings and data and never
touch yours on Linux. On macOS they write the real preferences, so run them in CI rather than
on your own Mac.

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `GRAPHITE_PACKAGED` | `OFF` (`ON` on macOS) | for packages: leaves out the in-app updater, which needs the source folder |
| `GRAPHITE_BUILD_UI` | `ON` | `OFF` builds only the Qt-free core and its test |

Packaging: `packaging/arch/make-tarball.sh` builds the Arch tarball from the committed HEAD
(with MicroTeX). The macOS app, `.dmg` and GitHub Release are built by
`.github/workflows/macos.yml` when a `v*` tag is pushed.

## Project layout

```
src/
  main.cpp       command line, fonts, start-up
  core/          the Markdown parser and writer: plain C++, no Qt, byte-exact round trips
  ui/            main window, outliner, panels and cards, sidebar, NERV panel, theme, snippets
  notes/         link and search index, definitions, the Graphite Essentials pages
  planner/       tasks and subtasks, progress bars, timetable, timers
  chem/          \ce conversion, molar masses, equation balancing, oxidation states
  maths/         quick maths with units, lab tables and plots, function graphs
  render/        formulas (MicroTeX) and images drawn inside the text
  desktop/       wallpaper colours, Hyprland blur, the update watcher
tests/           core, palette, index, chem and ui tests
docs/            user guide, field manual (the F2 tutorial)
packaging/       Arch PKGBUILD and tarball script, macOS Info.plist
third_party/     MicroTeX (git submodule) and tinyxml2
install.sh       build, test and install from source (Arch)
```

## Contributing

Bug reports and pull requests are welcome. Please:

- run `ctest --test-dir build --output-on-failure` before sending a change. Every suite must pass;
- keep files byte-exact: a page that is opened and not edited must never be rewritten, and
  untouched blocks must be written back exactly as they were (`tests/core_test.cpp` checks
  this);
- keep it light: no polling timers and no work while idle. Graphite should stay around 30 MB
  of memory.

## Licence and credits

Graphite is released under the [MIT licence](LICENSE).

- [MicroTeX](https://github.com/NanoMichael/MicroTeX) (MIT) typesets the formulas. It is used
  through [a fork](https://github.com/Intezard006/MicroTeX/tree/graphite) with one patch to its
  Qt backend.
- [tinyxml2](https://github.com/leethomason/tinyxml2) (zlib licence) reads MicroTeX's font data.
- The macOS app bundles [Noto](https://fonts.google.com/noto) fonts (SIL Open Font Licence).

*Neon Genesis Evangelion* and its names are the property of khara, Inc. Graphite is an
unofficial fan-made tool, not affiliated with or endorsed by them, and ships none of their
artwork. Settings › Sidebar unit can download the official unit artwork for personal use, only
after you agree, or show a picture of your own.
