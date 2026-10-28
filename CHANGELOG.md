# Changelog

What changed in each version of Graphite. The [user guide](docs/guide.md) describes every
feature in full.

## 0.25.1

- **F2 opens the field manual from disk**: it is installed with Graphite (and inside the macOS
  app), so it works offline and for everyone.
- MicroTeX, the formula typesetter, is now a git submodule (a fork with one Qt patch) instead of
  a copied folder: clone with `git clone --recursive`, or run `git submodule update --init`.
  `install.sh` fetches it when it is missing.
- Sources are grouped by topic (`src/ui`, `notes`, `planner`, `chem`, `maths`, `render`,
  `desktop`). The README was rewritten with step-by-step install guides, and the feature
  reference moved to `docs/guide.md`.

## 0.25.0

- **Sidebar art from you**: Graphite ships no Evangelion pictures. Settings › Sidebar unit
  can download the official artwork after you agree, or show a PNG of your own.
- **macOS app** (Apple Silicon): battery readout through macOS, ⌘/⌥ keys, bundled fonts,
  built and tested in CI.
- **Arch Linux package**: a tarball with a PKGBUILD and a one-command installer.
- Released under the MIT licence.

## 0.24.0

- **Progress bars**: `/progress` adds `{progress 0/10}`. Alt+= / Alt+− step it, and the task's
  marker follows.

## 0.23.0

- **Headings follow their subtasks**: they start, finish, fold and reopen by themselves, and
  show a `SYNC 2/5` count. Marking a heading done closes its subtasks, and one Ctrl+Z brings
  them back.
- **Day scrap** (Ctrl+Shift+Backspace): wipe today's journal to plan the day again. One undo
  restores it.
- Formatting snippets (`;b`, `;i`, `;hl`…) and Ctrl+B / Ctrl+I.
- The field manual (F2), an interactive tutorial.

## 0.22.0

- **Function graphs**: `=graph{…}` with roots, turning points, intersections, asymptotes and
  units.

## 0.16.0 – 0.21.0

- **Lab tables** with worked-out columns, `=plot{y vs x}` with straight-line fits.
- Numbered formulas and `[[eq:…]]` references; definitions (`Term:: meaning`) with hover;
  oxidation states and half equations.
- **Day planning**: time blocks, the Ctrl+D timeline, a weekly timetable (Ctrl+Shift+D), a
  NOW / NEXT row.
- The **Graphite Essentials** book: guides and a timetable supplied to every tree.

## 0.14.0 – 0.15.0

- **Quick maths** with units (`= …` lines), molar masses with `M(formula)`.
- **Equation balancing** (`/balance`).
- Updates from inside Graphite for source installs.

## 0.10.0 – 0.13.0

- Focus timers (`/timer`), exam countdowns, Pattern Orange for tasks due soon.
- **Trees, books and pages** with an explorer: rename (links follow), move, delete.
- Deadlines with times and countdowns, subtasks in the Tasks panel.

## 0.7.0 – 0.9.0

- Selecting, copying, pasting and undoing several blocks at once; find in page; focus mode.
- Immersion: unit palettes, title cards, MAGI, Pattern Blue.
- The Settings page; deleting a page to the Trash.

## 0.6.0

- One text-size setting for everything (Ctrl+= / Ctrl+− / Ctrl+0), the A.T. Field modes, a pulse
  on save.

## 0.5.0

- The `/` command menu and the NERV panel (clock, battery, A.T. Field).

## 0.4.0

- Live formula preview, molar masses, snippets, pasted images, PDF export.

## 0.3.0

- Links, backlinks, search (Ctrl+K), tasks, the blur meter, title and body fonts, `install.sh`.

## 0.2.0

- The outliner, byte-exact Logseq files, wallpaper colours, inline TeX with `\ce{}`.
