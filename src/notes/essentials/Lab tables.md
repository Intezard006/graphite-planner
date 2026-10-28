description:: How lab tables work: try every example here, this page is yours to edit

- ## What a lab table is
	- A table you type in Markdown. Give the columns units and it becomes a small spreadsheet: columns with an `=` in the header are worked out for every row, and `=plot{y vs x}` draws a graph with a straight-line fit.
	- Type `/table` in any block to start one. Click a table to see and edit its text; click away (or press Esc) to see it worked out.
- ## 1. Columns with units
	- A header written `name / unit` (or `name (unit)`) makes the column a variable: `m / g` is a mass `m` in grams. A header with `=` is a formula, worked out from the other columns of the same row:
	- | m / g | V / mL | ρ = m/V to g/mL |
	  |-------|--------|-----------------|
	  | 2.50  | 10.0   |                 |
	  | 7.80  | 10.0   |                 |
	  | 12.6  | 25.0   |                 |
	- Click the table above: the ρ column is empty in the text. Leave computed cells empty; they are only shown, never written into the file.
	- `to g/mL` picks the unit of the answer. Without it, the answer comes in the unit the first row gives.
- ## 2. Formulas can use quick maths
	- Anything a quick maths line (`= 2.5 g / M(NaCl)`) can do works in a header: constants (R, F, N_A...), molar masses with `M(...)`, `ln`, `log`, `exp`, `sqrt`. A computed column can use the computed columns before it.
	- | m / g | V / mL | n = m/M(NaCl) to mmol | c = n/V to M |
	  |-------|--------|-----------------------|--------------|
	  | 0.585 | 100    |                       |              |
	  | 1.17  | 100    |                       |              |
	  | 2.92  | 250    |                       |              |
	- A cell can carry its own unit: `250 mL` in a `V / L` column is read as 0.25 L.
	- A row with a missing value stays blank instead of guessing. If a formula cannot be worked out (a typo, units that do not match), the reason is written under the table.
- ## 3. Plots with a fit
	- Put `=plot{y vs x}` on the line under a table, in the same block (no space after the `=`). You get the points, a straight-line fit, and the slope, intercept and R².
	- A calibration line (Beer–Lambert): the slope is ε·l.
	- | c / mM | A    |
	  |--------|------|
	  | 0.20   | 0.231 |
	  | 0.40   | 0.459 |
	  | 0.60   | 0.688 |
	  | 0.80   | 0.912 |
	  | 1.00   | 1.145 |
	  =plot{A vs c}
	- Axes can be expressions of the columns. For `ln(k)` against `1/T` the activation energy is worked out from the slope too:
	- | T / K | k / s^-1 |
	  |-------|----------|
	  | 300   | 19.7     |
	  | 310   | 37.6     |
	  | 320   | 68.94    |
	  | 330   | 121.8    |
	  =plot{ln(k) vs 1/T}
	- Just `=plot{}` takes the first two number columns, the first one as x.
- ## 4. Your turn
	- Fill in the empty rows (titration: volume of NaOH added and the pH meter reading), then add `=plot{pH vs V}` under the table:
	- | V / mL | pH |
	  |--------|----|
	  | 0      |    |
	  | 5      |    |
	  | 10     |    |
	  | 15     |    |
- ## Summary
	- `name / unit` or `name (unit)`: a column you fill in.
	- `name = formula` (and `to unit`): a column worked out for every row.
	- `=plot{y vs x}` under the table: a graph with a fit. `=plot{}`: the first two columns.
	- Computed cells stay empty in the file; the page still opens in any Markdown editor.
