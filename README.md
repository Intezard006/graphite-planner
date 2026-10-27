# Graphite

A native Qt 6 notebook that reads and writes Logseq's file-based markdown format, so an
existing graph opens as it is and can still be opened in Logseq afterwards.

Done so far: journals and pages, a block outliner, saving in Logseq's format, Ctrl+T
transparent mode, colours taken from your wallpaper, inline TeX with `\ce{}`, links,
backlinks, `#tag` pages, Ctrl+K search, a tasks view, a blur slider for transparent mode,
a live formula preview with molar masses, snippets, pasted images, PDF export, a `/` command
menu, the NERV panel, one text-size setting for everything, a Settings card, find in page,
focus mode, editing several blocks at once with undo across blocks, and the Evangelion
touches: unit palettes, title cards, MAGI, Pattern Blue, the entry-plug boot and the
activity-limit alarm.

**New to Graphite?** Press **F2** for the field manual: nine short drills, each with a working
copy of the editor, in the browser (the page is `docs/field-manual.html`, published as a
private claude.ai artifact).

## Trees, books and pages

Everything is plain Markdown in ordinary folders. A **tree** is a folder anywhere on disk;
a **book** is a folder in it; a **page** is a `.md` file in a book; the tree's **journals**
are in its `journals/` folder. (A Logseq graph opens as a tree: its `pages/` folder is the
book **Unsorted**, and `logseq/`, `assets/`, `whiteboards/` are not books.)

The sidebar is the explorer: Journals, then each book with its pages. Click a page to open
it, click a book to fold it; **F2** renames (links to a renamed page are updated on every
page of the tree); **Delete** deletes (pages, journals, whole books: to the Trash); drag a
page onto a book to move it; right-click for New page here, New book, Rename, Move to and
Delete. New pages go into the current book (Alt+←/→ in the New page card picks another), and
a title like `Organic/Lecture 7` puts it straight into that book. `[[Page]]` finds a page
anywhere in the tree, `[[Book/Page]]` in that book. Folders made outside Graphite show up by
themselves.

Click the tree's name at the top of the sidebar for your recent trees and **Open or start a
tree…** (an empty folder becomes a new tree). Each tree reopens where you left it: running
`graphite` with no folder opens the last tree at its last page.

## Links, tags and search

`[[Page]]`, `#tag`, `#[[two words]]` and `tags:: a, b` all link to a page; click one to go
there (a page that does not exist yet is created once you type in it). Under every page,
**Linked references** lists the blocks on other pages that point to it, newest journal
first; click one to jump to that block. A tag page is just a page, so `#exam` shows every
block tagged exam. **Ctrl+K** searches page titles and the text of every block; Enter opens
the best match, and the last row creates a page with that name.

The index behind this keeps only the text of each block and re-reads a file only when its
modification time changed (on save, page switch, or when the window gets focus again). It
never polls.

## Tasks

Start a block with `TODO`, or press **Ctrl+Enter** in it to cycle TODO → DOING → DONE → plain
(LATER → NOW → DONE for Logseq's NOW workflow). `/scheduled` or `/deadline` opens a date list
(a blank row to type your own first); a deadline then asks for a time (none first, then one to
type, or morning / noon / evening / before midnight). `SCHEDULED: <2026-10-30 Fri>` and
`DEADLINE: <2026-10-30 Fri 14:00>` can sit anywhere in the block: they always show on a line
under the task. A deadline with a time counts down, `T−03H 12M`, amber, red in the last hour,
`LIMIT +00H 20M` once past.

**Time blocks**: start a block in today's journal with a time, `09:00 TODO organic problem
set`, `14:00-15:30 lab` (the marker may also come first: `TODO 09:00 …`). The time shows as a
small label, the page gets a timeline in its left margin (done and past blocks small dots, the
running one filled, later ones hollow, a pointer where now is) and the NERV panel shows what is
next. Nothing polls: Graphite wakes once at the next start, end or deadline. A timed task is a
task like any other (Ctrl+Enter, the Tasks list); in a journal, without a date of its own, it is
due that day, so it shows under Today.

**Ctrl+D** opens today's timeline: every time block of the day (subtasks under a heading
included, with the heading on the right), timed tasks scheduled today on other pages, and
today's deadlines with a time (diamonds), in order. A line marks now and how long until the
next one; half an hour or more between a block's end and the next start shows as free time.
Enter jumps to the block, Ctrl+Enter marks it done (it stays, struck through).

**Ctrl+Shift+D** opens the week's timetable, `Graphite Essentials/Timetable` (a block per
day to fill in). Write each lecture or lab under its day:

    tags:: timetable
    - Monday
        - 09:00-10:00 Organic chemistry lecture
        - 14:00-17:00 Physical chemistry lab
    - Tue, Thu
        - 11:00-12:00 Spectroscopy
    - Fri 10:00-11:00 Seminar

Days can be written in full or short (Mon, Tue/Tues, Thu/Thurs), listed (`Mon, Wed / Fri`),
as a range (`Mon-Fri`), or as `Weekdays`, `Weekends`, `Daily`. Any page tagged
`tags:: timetable` (or titled Timetable) counts. Today's entries join the Ctrl+D timeline
(marked TIMETABLE; Enter goes to the entry, Ctrl+Enter leaves it, since it repeats), the
NERV panel's NOW / NEXT row and the wake-up at their start and end. An entry you have also
written in today's journal (same time, same text) is shown once.

**Subtasks**: a task indented (Tab) under another task is its subtask. The parent shows its
sync ratio, `SYNC ▮▮▯▯▯ 2/5` (done subtasks count too), and its subtasks hang under it.

**Ctrl+Shift+T** (or Tasks in the sidebar)
lists open tasks from every page under Overdue, Today, Upcoming and No date; type to filter,
Enter jumps to the block, Ctrl+Enter marks it done right there (the page gets its usual
one-time backup and only that line changes). The list is built when it opens and freed when
it closes, so it costs nothing the rest of the time.

**Headings that follow their subtasks** (a subtask is a task indented under another task):
starting a subtask turns a TODO heading DOING; when every subtask is done the heading turns
DONE, folds its subtasks away (`collapsed:: true`) and the status line says *Target neutralised
· name* (*Done · name* without immersion); a new or reopened subtask opens it again (DOING when
work has started, else TODO). Marking a heading DONE yourself closes its open subtasks, and one
Ctrl+Z brings them back. The heading shows how many are done: `SYNC 2/5` (`2/5` without
immersion). Only the blocks that change are rewritten, and only when you change something:
opening a page never does. Settings › Tasks switches each part off: Finish headings, Start
headings, Fold finished, Progress. The Tasks panel and the timeline go through the same rules.

## Progress bars

`/progress` puts a bar in the block, `{progress 0/10 by 1}`, with the end selected to type
over. **Alt+=** moves it a step forward, **Alt+−** a step back, in the block you are editing,
on selected blocks, or on a task in the Tasks panel (Ctrl+Shift+T). The status line shows where
it is: `SYNC 38%  ▮▮▮▯▯▯▯▯▯▯  45 / 120 PAGES`. It can start somewhere other than 0 and carry a
unit: `{progress 45/120 pages from 30 by 5}` (pages 30 to 120, five at a time). In the page it
is a segmented gauge, orange while running and green at the end.

On a task the marker follows, with the Tasks settings: the first step forward turns TODO into
DOING, reaching the end turns it DONE (*Sync ratio 100% · Read chapter 12*), and stepping back
from the end opens it again. The heading above it follows in turn. In the Tasks panel the bar
shows as its percentage.

## Day scrap

**Ctrl+Shift+Backspace** (or `/scrap`) wipes the open journal so the day can be planned
again. It asks first; with immersion on, a sea of LCL (random characters, orange) rises up the
window, THIRD IMPACT stands on it while the page empties, and it drains away (a click or a key
skips it). One Ctrl+Z brings the day back, and a copy of the file goes to `.graphite/bak/`
first. Pages are never scrapped, only journals.

## Transparency and blur

Ctrl+T makes the background transparent; Hyprland blurs what is behind it. While transparent,
a **Blur** meter appears in the bottom bar: click, drag or scroll it, or press Ctrl+[ / Ctrl+].
Hyprland only has one blur setting for all windows, so while transparent mode is on, other
blurred windows follow it too; your own blur values come back when you press Ctrl+T again or
quit (and on the next start, if Graphite was ever killed).

## Sidebar art

Graphite ships no pictures of Evangelion units. Settings › Sidebar unit offers two ways to put
one faintly behind the page list:

- **Custom**: "Your own image" lets you pick a PNG (a transparent background looks best); it is
  cut to its visible part, scaled to at most 800 px tall and kept in the art folder.
- **Download Eva art**: after you agree on a card that says so, fetches the official artwork of
  Units 00, 01 and 02 from the Evangelion Fandom wiki (evangelion.fandom.com). The images are
  copyright Khara / Gainax and are for personal use on your computer only. Nothing is
  downloaded without that click, and never at start-up.

The art folder is `~/.local/share/graphite/art` (Linux) or `~/Library/Application Support/graphite/art`
(macOS); without images the sidebar simply shows none.

## Fonts

**Ctrl+Shift+F** picks the title font and the body font (Tab switches between them). Type to
search the installed fonts; moving through the list previews on the page itself; Enter keeps
both, Esc puts both back. The defaults are v0.1's heavy serif for titles and Inter for text.

## Colours

Graphite follows your wallpaper. It looks, in order, at `~/.config/ml4w/colors/colors.json`,
`~/.cache/wal/colors.json`, then the wallpaper picture itself (found through ML4W's
`current_wallpaper`, `awww`/`swww`, or hyprpaper) and falls back to built-in violet and green.
It re-reads when the wallpaper changes. `graphite --palette-info` shows what it picked;
`--wallpaper FILE` and `--palette builtin` override it.
Ctrl+P switches between the wallpaper colours and the original violet and green, and
remembers the choice.

## Maths and chemistry

`$E = mc^2$` is typeset inline, `$$...$$` on its own line is centred. `\ce{2H2 + O2 -> 2H2O}`,
`\ce{SO4^2-}`, `\ce{A ->[H2O][heat] B}`, `\ce{CuSO4.5H2O}` and `\pu{25 kJ.mol-1}` work.
While you edit, the formula under the cursor is shown typeset in a small card under the line,
so typos show up at once; for `\ce{...}` the card also gives the molar mass of every species
(`M: H2SO4 98.07 · NaOH 40.00 g/mol`), and so does hovering over a typeset formula. Masses use
the IUPAC standard atomic weights; coefficients, charges and state symbols are ignored, `D`
is deuterium. Typesetting is MicroTeX (MIT, in `third_party/`);
`\ce` is a built-in converter that covers common mhchem, not all of it. Fonts load on the
first formula, so pages without maths pay nothing.

**Numbered formulas.** `$$k = A e^{-E_a/RT}$$ {#arrhenius}` on its own line is centred with
its number on the right, (1), (2)… down the page. `[[eq:arrhenius]]` anywhere on the page
reads **Eq. (1)** and jumps to it when clicked; a label that does not exist shows as
`Eq. (name?)`. Equation links are not page links: no "eq:…" page appears anywhere.

**Oxidation states.** The card (and the hover) for a single species gives its oxidation
states: `$\ce{KMnO4}$` → `Ox · K +1 · Mn +7 · O −2`; `Fe3O4` gives Fe +8/3 (an average).
For a reaction it says what changes, and writes the half equations (acidic, with H⁺, H₂O
and e⁻) when they balance:

    Redox     · Mn +7 → +2 (reduced) · Fe +2 → +3 (oxidised)
    Reduction · MnO4- + 8H+ + 5e- -> Mn^2+ + 4H2O
    Oxidation · Fe^2+ -> Fe^3+ + e-

The usual rules, in order: F −1, group 1 +1, group 2 +2, Al +3, Zn +2, H +1, O −2, halogens
−1, then S −2 and N −3; the last element takes what the charge needs (so H is −1 in NaH and
O is −1 in H2O2). Where the rules can't separate two elements (C and Fe in K4[Fe(CN)6]),
those two are left out rather than guessed.

## Definitions

A line `Term:: what it means` anywhere in the tree is a definition. It shows as
**Term** — what it means, and from then on the term is underlined with dots wherever it is
written, on any page (plurals too: "enthalpies" finds Enthalpy); hover it for the meaning
and the page it is defined on. Terms may have spaces (`Le Chatelier's principle:: …`).
Graphite's own properties (`tags::`, `description::`, `id::`, `alias::`…) are not
definitions.

## Graphite Essentials

Every tree gets a book called **Graphite Essentials** the first time Graphite opens it: pages
Graphite supplies, written once and then yours to edit.

- **Lab tables**: a guide to lab tables, with examples to try (columns with units, worked-out
  columns, quick maths in headers, plots with fits, a table to fill in yourself).
- **Graphs**: a guide to `=graph{…}`, from a parabola to an Arrhenius curve over your data.
- **Timetable**: the week's timetable (Ctrl+Shift+D). A tree that already has a timetable
  keeps its own.

A page you delete is not brought back; Settings › Graph › Graphite Essentials restores the
missing ones (pages you edited are left as they are). New guides in later versions are added
to the book when they arrive.

## Lab tables and plots

A Markdown table renders as a table. Headers written `name / unit` (or `name (unit)`) make a
column a variable, and a header with `=` is worked out for every row with quick maths
(`/table` starts one):

    | m / g | V / mL | ρ = m/V to g/mL |
    |-------|--------|-----------------|
    | 2.50  | 10.0   |                 |     ρ column shows 0.25
    | 7.80  | 10.0   |                 |     … 0.78

Leave the computed cells empty: they are shown, not written into the file. A computed column
can use the ones before it; without `to unit` its unit is the first row's. Cells may carry
their own unit (`0.5 mol` in an `n / mmol` column). A row missing a value stays blank; a
formula that can't be worked out says why under the table.

`=plot{y vs x}` under a table (in the same block, no space after `=`) draws it with a
straight-line fit (`/plot` types it for you):

    =plot{A vs c}          → slope 1.14 M⁻¹ · intercept 0.005 · R² 0.9993
    =plot{ln(k) vs 1/T}    → … · Eₐ = −R·slope = 49.99 kJ/mol
    =plot{}                → the first two number columns, x first

The older `= plot A vs c` still works.

Axes can be expressions of the columns; inside `ln`/`log` a column counts as its number in
the column's unit. For `ln(…)` against 1/T the activation energy is worked out too.

## Graphs

`=graph{…}` on its own line draws a function (`/graph` types it):

    =graph{x^2 - 2, x from -3 to 3}
    =graph{sin(x); cos(x), x from 0 to 2*pi}                 several curves
    =graph{tan(x), x from 0 to 2*pi, y from -6 to 6}         a height of your own
    =graph{k = A*exp(-Ea/(R*T)), T from 250 K to 400 K, A = 1e10 s^-1, Ea = 50 kJ/mol}

The function is anything quick maths reads (powers like `x^0.5` and `2^x` too), with units:
the axes read `T / K` and `k / s⁻¹`. After the range, `name = value` gives a constant;
`name =` in front names the curve. Under the graph, its marks are worked out and listed:
roots (target 01, 02…), the highest and lowest points (apex, nadir), where curves meet
(contact), and where a curve runs off to infinity (Pattern Blue, a red dashed line). Under a
lab table whose columns are named like the variable and the curve, the measured points are
drawn on it. A graph is worked out once (about 400 points) and kept as a picture until the
line changes; pages without graphs cost nothing.

## Quick maths

A line that starts with `= ` is worked out, units and all, and the answer shows after it
(`/maths` starts one):

    = 0.5 mol * 58.44 g/mol              →  29.22 g
    = 2.5 g / M(CuSO4.5H2O)              →  10.01 mmol
    = 10 g / 2 L / M(NaCl)               →  85.56 mM
    = R * 298 K to kJ/mol                →  2.478 kJ/mol
    = 2 atm * 3 L                        →  608.0 J
    = -log(1e-3 M)                       →  3
    = 25 °C to K                         →  298.1 K

`M(formula)` is the molar mass. Units take SI prefixes (mmol, µL, nM, kPa, kcal); also L, M,
min, h, day, cal, eV, atm, bar, mmHg / Torr, Da, Å, °C, %, ppm. Constants: `R`, `NA`, `F`,
`kB`, `c`, `e`, `hP` (Planck; `h` is the hour). `ln`, `log`, `exp`, `sqrt`; inside a log a
concentration counts in mol/L. Factors written next to each other go together, so
`J/mol K` is J/(mol K). `to` (or `in`, `->`) converts; otherwise the answer is in a unit
you used, or a common one with a sensible prefix, to four significant figures. Adding grams
to moles is an error, said in place. While you type, the card under the line shows the
answer typeset.

## Balancing equations

Put the cursor in a reaction, `$\ce{Fe + O2 -> Fe2O3}$`, and the formula card says whether it
balances, and how: `UNBALANCED · 4Fe + 3O2 -> 2Fe2O3 /balance`. Type `/balance` in the
block to write the coefficients in (the reaction around the cursor, else the block's first
one). Atoms and charge both count, so ionic and half equations work:

    Fe + O2 -> Fe2O3                          →  4Fe + 3O2 -> 2Fe2O3
    Cu + HNO3 -> Cu(NO3)2 + NO + H2O          →  3Cu + 8HNO3 -> 3Cu(NO3)2 + 2NO + 4H2O
    MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O →  MnO4- + 5Fe^2+ + 8H+ -> Mn^2+ + 5Fe^3+ + 4H2O
    Cr2O7^2- + H+ + e- -> Cr^3+ + H2O         →  Cr2O7^2- + 14H+ + 6e- -> 2Cr^3+ + 7H2O
    N2 + H2 <=>[Fe] NH3                       →  N2 + 3H2 <=>[Fe] 2NH3

States, hydrates, `^` / `v` and arrow conditions stay as written. A reaction that has no
balance, or more than one independent one (two reactions written as one), is said so.
Hovering a typeset reaction shows the same line.

## Snippets

Type `;` and a few letters (or `\ce`, `\pu`) and a list of matches appears under the cursor:
↑/↓ to choose, Tab or Enter to insert, Esc to close it. Space right after a complete name works
too. They know where they are: outside a formula `;eq` gives ⇌ and `;dH` gives `$\Delta H^\circ$`;
inside one they give `<=>` and `\Delta H^\circ`. `\ce` gives `$\ce{|}$` with the cursor inside.
There are arrows, ΔH°/ΔG°/ΔS°, Ka/pKa/Keq, fractions, roots, ± × ≈ ≤ ≥ ∞, the Greek letters and
`;today` (a link to today's journal). Ctrl+/ then Tab lists them all.

Formatting: `;b` **bold**, `;i` *italics*, `;s` ~~strikethrough~~, `;hl` ^^highlight^^, `;code`,
`;fence` (a code block), `;page` `[[…]]`, `;tag` `#[[…]]`, `;link` `[text](https://…)`, `;quote`.
The cursor lands between the marks. **Ctrl+B** / **Ctrl+I** wrap the selection (or leave an
empty pair to type into); pressing them again on a wrapped selection takes the marks off.

## Commands

Type `/` at the start of a word for the command menu; keep typing to filter (`/h2`, `/chem`,
`/due`), ↑/↓ and Tab or Enter to run, Esc or Space to close it. Headings 1–3 and plain text;
TODO / DOING / DONE / LATER / NOW (after any heading, as Logseq writes `## TODO …`);
Scheduled, Deadline (date list, then time); links to today, tomorrow and yesterday; page link, tag;
formula, display formula, `\ce`, `\pu`; code block, quote, divider; an image from a file
(copied into `assets/`); the current time; and **Lecture notes**, which turns the block into
`## Lecture · <date>` with Topics, Key equations, Examples and Questions to ask under it.

## Several blocks at once

**Esc** stops editing and leaves the block selected; **↑/↓** move the selection, **Shift+↑/↓**
extend it (from inside a block, Shift+↑ at its start or Shift+↓ at its end does the same).
**Shift+click** and **dragging** across blocks select too. With blocks selected:
**Backspace/Delete**, **Ctrl+C / Ctrl+X / Ctrl+V** (Logseq-style `- …` markdown, nesting kept,
`id::` lines left out), **Tab / Shift+Tab**, **Alt+Shift+↑/↓**, **Ctrl+Enter** (task markers),
**Ctrl+A** (every block; also a second Ctrl+A inside a block), **Enter** to edit, **Esc** to clear.
Pasting a bulleted list into a block makes real blocks of it (an empty block is replaced).

**Ctrl+Z** undoes typing inside the block first, then goes back through block changes: splits,
joins, indents, moves, folds, deletions, pastes, the lecture template; **Ctrl+Shift+Z** (or
Ctrl+Y) redoes. Up to 100 steps per page, forgotten when you open another page. Undoing after a
save is fine: the file is written again.

## Find and focus

**Ctrl+F** finds text in the page: matches are highlighted as you type, Enter / Shift+Enter
go to the next / previous one, Esc closes. It searches what you see (link labels, not the
brackets; not the inside of typeset formulas). **Ctrl+\\** is focus mode: the sidebar goes
away and the page takes the window; Ctrl+\\ again brings it back.

## Immersion

All of these follow one switch, **Immersion** in Settings (on by default); off, the same
features use plain wording and the flourishes go.

- **Unit palettes**: Ctrl+P cycles wallpaper → Unit-01 (violet/green) → Unit-00 (blue/white)
  → Unit-02 (red/orange); also in Settings. The NERV panel names the unit, and the unit's
  whole body stands faintly behind the page list, front view; with wallpaper colours it is the unit
  nearest your desktop's colours (Settings > Sidebar unit can fix one or turn it off).
- **Title cards**: New page (Ctrl+N) is laid out like an episode card: the title in a box on
  the left (it wraps, as "NEON / GENESIS / EVANGELION" does), EPISODE:N, and the caption set
  to the right (Enter or Tab between them). The caption is saved as the page's
  `description::` property, which Logseq reads too, and is shown in the page header in the
  title's serif, under the title; click it to edit. The first time a new page
  opens, its card plays for a moment (a click or a key ends it). Settings > Title cards can
  also play it each time you open a journal (numbered by the day of the year), or never.
- **MAGI**: when the open page was changed by another program while you were editing it,
  MELCHIOR-1 (overwrite), BALTHASAR-2 (reload) and CASPER-3 (wait) vote; ←/→ or 1/2/3, Enter.
  Saves read "MAGI · APPROVED".
- **Pattern Blue / Orange**: on today's journal, a red strip for tasks due today or overdue
  and an amber one for tasks due in the next three days: "2 targets · 3 cores" (subtasks are
  the Angel's cores). Settings > Pattern strips > Detailed lists each target (◆) with its
  subtasks hanging under it, sync ratios and countdowns; click a row to go to it, the heading
  for all tasks. The sidebar's Tasks row counts the first kind.
- **Angel approaching**: the nearest block tagged `#exam` (or `tags:: exam`) with a
  SCHEDULED / DEADLINE date counts down in the NERV panel ("4 DAYS", red on the day). An exam
  indented under a heading takes the heading's name and, without a date of its own, its date:
  `Quiz #exam` under `Supramolecular Chemistry` is "Quiz · Supramolecular Chemistry".
- **Next Episode**: at the bottom of today's journal, the tasks scheduled or due tomorrow.
- **Operations**: type `/timer 20m 15s` (or `/timer 25`, `1h 5m`, `90s`, `12:30`) and Enter in
  any block: the command leaves the note and the NERV panel counts down. At the end the
  eyecatch card shows (any key or click), a desktop notification goes out, and today's
  journal gets `Operation 20:15 · 14:10–14:30 #operation`, so the operation page lists them
  all. `/timer stop` ends one early; a running one survives a restart. While you type
  `/timer …` a list shows what Enter will start, with presets (25m, 50m, 1h 30m, 10m, stop).
- **Entry plug**: at start the NERV panel runs LCL PRESSURE / A10 NERVE CONNECTION / SYNC
  START, then the field comes up.
- **Activity limit**: on battery at 15 % or less the countdown replaces the clock, in red.

## Deleting a page

**Ctrl+Shift+Del** asks, then moves the open page's file to the Trash, where your file
manager can restore it (or to the graph's `logseq/.recycle/` when there is no Trash). Links to
it on other pages stay, as in Logseq.

## Settings and text size

**Ctrl+,** opens Settings as a page in place of your notes (Esc or Ctrl+, again goes back):
Appearance (colours, transparency, blur, text size, fonts), Immersion (on/off, title cards,
A.T. Field), Graph (folder, where deleted pages and backups go), Updates and About
(memory in use, keys). ↑/↓ choose, ←/→ change, Enter opens; everything applies at once.
Text size is the body font's size (9–18 pt, default 11.5); titles, headings, code, labels,
the NERV panel, formulas, the sidebar and the cards all scale from it. **Ctrl+= / Ctrl+− /
Ctrl+0** change it from anywhere.

## NERV panel

At the bottom of the sidebar: an ASCII A.T. Field, a clock and the power readout —
**EXTERNAL** (green) on the charger, the umbilical cable, or **INTERNAL** (orange) on battery,
with the time left when the battery reports it, and a 10-segment charge meter. The field
ripples for a moment when Graphite starts, when you open a page and when the charger is
plugged in or pulled, after every save, and while the mouse is over it; the rest of the time
it is a still picture, and the clock changes once a minute, so it costs one wakeup a minute.
Ctrl+Shift+A cycles **Pulse → Constant → Off**. Constant keeps it moving while the window is
active and the charger is in (about 1 % of one core); on battery it falls back to pulses
("CONSERVING"), like an Eva on internal power.

**Ramiel.** The nearest `#exam` counts down above the clock ("ANGEL APPROACHING · 12 DAYS").
In its last week the Angel appears beside the countdown with the exam's name under it, a
crystal octahedron that goes from blue to red as the day comes. It is there to look at, nothing
more:

- **Click it** and it splits apart and folds back, the way it does before it fires.
- **When a deadline goes by undone**, it fires one beam into the A.T. Field (once a day at most;
  every passed deadline is still named in the status line).
- **The day before the exam** its drill reaches down towards the clock as the day goes on; on
  the day it is through.
- **Finishing the day's last open task** sometimes (one in three, once a day) has it cross the
  field.
- **After the exam**, the first start shows "Target silenced" once, and it is gone.

With immersion off, only the plain exam line is shown.

**Next.** On a day with time blocks (see Tasks), the row above the clock shows the one running
("NOW · –15:30 lab write-up") or the next ("NEXT · 14:00 seminar").

## Dates

`/scheduled` and `/deadline` open a list: a blank date to type yourself first, then Today,
Tomorrow, In 3 days, Saturday, Next Monday, In a week / two weeks / a month. The date goes
where you typed the command, on the task's own line or a line of its own; Graphite reads
both. (Logseq itself only recognises the date on a line of its own; on the task's line it
shows as plain text there, nothing breaks.)

## Images

Ctrl+V with a screenshot on the clipboard (or copied image files) saves the picture in the
graph's `assets/` folder and inserts `![image](../assets/image_….png)`, the same as Logseq. Images
are shown at most 640×420 and only decoded when they are on screen, at that size, into a
32 MB cache, so a page full of slide photos stays light.

## PDF

Ctrl+Shift+E exports the open page as an A4 PDF in print colours (white paper, EVA purple and
green darkened for ink), with formulas at print resolution, collapsed blocks included and
page numbers.

## Install

### macOS (Apple Silicon)

Download `Graphite-<version>-macos-arm64.dmg` from the repository's **Releases** page (or, for
a build that is not released yet, from the **Actions** tab: open the latest "macOS" run and take
the `Graphite-macos-arm64` artifact). Open the dmg and drag **Graphite** onto **Applications**.

The app is not signed with an Apple developer certificate, so macOS stops it the first time.
Either right-click Graphite in Applications, choose **Open**, then **Open** again; or, once, in
Terminal:

    xattr -dr com.apple.quarantine /Applications/Graphite.app

(`xattr -dr` removes the "downloaded from the internet" mark, `-d` deleting the named
attribute and `-r` doing it for every file inside the app.)

On a Mac **⌘ takes the place of Ctrl** and **⌥ (Option) the place of Alt**: Ctrl+K is ⌘K,
Alt+= is ⌥=. The key sheet still prints the Linux names. A few things are Linux-only and are
simply absent: the transparency blur (it asks the Hyprland compositor), colours taken from the
wallpaper (the built-in palettes and the Eva units work), and the update row in Settings (a
packaged app is updated by installing a newer dmg). The NERV panel reads the battery through
macOS itself. Noto fonts are bundled in the app.

### Arch Linux

Download `graphite-planner-<version>-arch.tar.gz`, then:

    tar -xzf graphite-planner-<version>-arch.tar.gz
    cd graphite-planner-<version>-arch
    ./install.sh

It builds a pacman package with `makepkg -si` (dependencies installed by pacman) and installs
it to `/usr`. Remove it with `sudo pacman -R graphite-planner`. The tarball is made by
`packaging/arch/make-tarball.sh`. A `~/.local/bin/graphite` from a source install shadows
`/usr/bin/graphite`.

### From source (any Linux)

    ./install.sh

It checks the build tools (offering to install missing ones with pacman), builds, runs the
tests, finds an older installed Graphite (in ~/.local or /usr/local), shows what it will
remove and asks, then installs to ~/.local with a launcher entry. `--yes` skips the
question, `--skip-tests` the tests, `--uninstall` removes Graphite. Your notes, settings and
your own fonts are never touched.

### Updates from inside Graphite (source builds)

Graphite knows the folder it was built from (this checkout). A build made with
`-DGRAPHITE_PACKAGED=ON` (the macOS app, distro packages) leaves this out. When a file there (`src/`,
`CMakeLists.txt`) is newer than the installed program, the sidebar shows **Refit ready**
("Update ready" with immersion off) and Settings › Updates says what changed. **Update**
there runs `./install.sh --yes` in the background (build, tests, install; the log goes to
`~/.cache/graphite/update.log`); when it is done, **Restart now** opens the new version on
the same tree. Nothing is polled: the folder is watched with inotify and checked 1.5 s after
it changes. Settings › Watch for updates turns it off.

## Build by hand (Arch)

    sudo pacman -S --needed qt6-base qt6-wayland cmake ninja gcc
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure     # optional: runs the automated tests
    ./build/graphite /path/to/your/graph

With no path it reopens the last graph; the first run asks for a folder.

## Keys

| Key | Action |
|---|---|
| Enter | split the block at the cursor (new block below) |
| Shift+Enter | newline inside the block |
| Tab / Shift+Tab | indent / outdent |
| Alt+Shift+Up / Down | move the block (with its children) |
| Ctrl+Up / Ctrl+Down | collapse / expand children |
| Up / Down | move between blocks |
| Backspace at start | join with the previous block |
| ( [ { ` | typed in pairs; with a selection, wraps it |
| Esc | stop editing |
| Ctrl+S | save now (it also autosaves 0.7 s after you stop typing) |
| Ctrl+K | search pages and notes |
| Ctrl+Shift+T | tasks from every page |
| Ctrl+D | today's timeline |
| Ctrl+Shift+D | the week's timetable |
| Ctrl+Shift+E | export the page to PDF |
| Ctrl+Shift+A | A.T. Field: pulse / constant / off |
| Ctrl+, | settings |
| Ctrl+Shift+Del | delete this page (to the Trash) |
| Ctrl+F | find in page |
| Ctrl+\\ | focus mode (hide the sidebar) |
| Esc, Shift+↑/↓, Shift+click, drag | select blocks |
| Ctrl+Z / Ctrl+Shift+Z | undo / redo, across blocks |
| Ctrl+= / Ctrl+− / Ctrl+0 | larger / smaller / default text |
| `/` | command menu |
| `;` … / `\ce` then Tab | snippets |
| Ctrl+V | paste text, or an image into assets/ |
| Ctrl+Enter | cycle TODO → DOING → DONE |
| Ctrl+B / Ctrl+I | bold / italics around the selection |
| Alt+= / Alt+− | progress bar: a step forward / back |
| Ctrl+Shift+Backspace | scrap the day: wipe this journal (Ctrl+Z restores it) |
| F2 | the field manual (tutorial, in the browser) |
| Ctrl+[ / Ctrl+] | less / more blur (transparent mode) |
| Ctrl+J / Ctrl+N / Ctrl+O | today's journal / new page / open another graph |
| Ctrl+T | transparent background, text stays opaque |
| Ctrl+P | colours: wallpaper / Unit-01 / Unit-00 / Unit-02 |
| Ctrl+Shift+F | title and body fonts: search, arrows preview, Tab switches, Enter keeps |
| Ctrl+/ or F1 | show every key |
| Ctrl+Q | quit |

Click a block to edit it, the small arrow left of a bullet to fold it, and a `[[link]]` to
follow it.

## Your files

* Opening a page changes nothing. Saving rewrites only the blocks you edited; everything
  else is written back byte for byte (odd indentation, LOGBOOK drawers, no trailing newline).
* Before the first overwrite of a file in a session, the old version is copied to
  `.graphite/bak/<folder>/<name>.graphite-<time>.bak.md` inside the tree.
* Files are written through a temporary file and renamed, so a crash never leaves half a note.
* If a file changed on disk (for example Logseq is open too) while you had unsaved edits,
  Graphite asks before overwriting. Close Logseq while testing to avoid the question.
* A bullet-less first line (such as `## Heading` or an `id::` line) is kept as it is.
* Opening Today without typing creates no file.

## Not done yet

Unlinked references, undo of edits made from the tasks panel that spans several blocks (undo works inside one block), pasting
several bullets at once, drag and drop, queries, whiteboards, plugins.

## Layout

    src/core/    Logseq markdown parser/writer, plain C++ (no Qt), fully unit tested
    src/ui/      Qt widgets: window, outliner, theme
    third_party/ MicroTeX and tinyxml2
    tests/       core_test (parser), chem_test, palette_test and ui_test (drives the real window headlessly)
