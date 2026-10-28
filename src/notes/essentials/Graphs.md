description:: Graphs of functions: write a function, get its curve, roots, peaks and asymptotes

- ## A first graph
	- Write `=graph{` then the function, a comma and its range (no space after the `=`). `/graph` types it for you. Click away to see it drawn:
	- =graph{x^2 - 2, x from -3 to 3}
	- Under the graph, the roots (where it crosses zero), the lowest and highest points, and anything that runs off to infinity are worked out and listed. Click the graph to see its text again.
- ## Writing functions
	- Anything quick maths reads works: `+ - * /`, powers `x^2`, `x^0.5`, `2^x`, `x^(1/3)`, brackets, `sqrt`, `ln`, `log`, `exp`, `sin`, `cos`, `tan`, and `pi`.
	- Use `exp(x)` for e to the power x (`e` alone is the charge of an electron).
	- Several curves: separate them with `;`. Where they meet is marked too:
	- =graph{sin(x); cos(x), x from 0 to 2*pi}
	- Without a range, x goes from −10 to 10. Add `y from … to …` to choose the height yourself, useful when something runs off to infinity:
	- =graph{tan(x), x from 0 to 2*pi, y from -6 to 6}
- ## Units and constants
	- The variable can have any name and carry units; so can the answer. Give constants after the range as `name = value`, and name the curve with `name =` in front:
	- =graph{k = A*exp(-Ea/(R*T)), T from 250 K to 400 K, A = 1e10 s^-1, Ea = 50 kJ/mol}
	- R, F, NA, kB and molar masses `M(NaCl)` are known already.
	- The Boltzmann population of an upper level against temperature is just a formula:
	- =graph{ratio = exp(-dE/(kB*T)), T from 50 K to 2000 K, dE = 2e-21 J}
- ## Theory against your data
	- Put a graph under a lab table, in the same block. When the table's columns are named like the graph's variable and curve, the measured points are drawn on the curve:
	- | T / K | k / s^-1 |
	  |-------|----------|
	  | 300   | 19.7     |
	  | 320   | 68.94    |
	  | 340   | 205      |
	  =graph{k = A*exp(-Ea/(R*T)), T from 280 K to 360 K, A = 1e10 s^-1, Ea = 50 kJ/mol}
- ## Summary
	- `=graph{f(x), x from a to b}`: one curve. `f; g`: several.
	- `y from a to b`: the height. `name = value` after the range: a constant.
	- `name = f(x)`: a named curve (its name labels the axis).
	- The marks: target 01, 02… are roots; apex and nadir are the highest and lowest points; contact is where curves meet; Pattern Blue is where a curve runs off to infinity.
