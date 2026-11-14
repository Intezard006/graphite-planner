# Changelog

What changed in each version of Graphite. The [user guide](docs/guide.md) describes every
feature in full.

## Unreleased

- **NERV-HQ wallpaper**: the colours follow the image named in `$XDG_STATE_HOME/nerv/wall.toml`
  (`mode = "image"`) first and re-read when it changes. `mode = "live"` falls through to the
  older sources; the ML4W / matugen paths remain only as last fallbacks.

## 0.26.1

- **Exams in the timeline**: Ctrl+D lists today's exams (a red EXAM chip, the course heading on
  the right, *All day* when untimed) and every upcoming exam after them. Exams were missing
  because the timeline only took timed blocks from other pages when they had a TODO marker;
  now any timed block scheduled today shows.
- **Exams finish themselves** once their time has passed (end of a range, else the start,
  else the end of the day), one line rewritten, with a flash. Reopened by hand, an exam stays
  open. Settings › Tasks › Finish exams. Today's timed exams also feed the NEXT row.

## 0.26.0

- **Carryover**: today's journal lists the tasks left open in earlier journals (last 7 days by
  default; 3, 14 or all in Settings › Tasks), a group per day, with how long each has been
  open and a warm edge from three days on. Click a marker (or Ctrl+Enter over the row) to move
  the task on in its own journal; click the row to open that day. Nothing is copied.
- The installer no longer deletes your sidebar art (`~/.local/share/graphite/art`) when it
  replaces or removes an older version.
- Field manual: a Carryover drill.

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
