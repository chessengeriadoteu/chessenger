# Sedar (Chessenger 0.1)

Sedar is a console chess program written by Dariusz Domagała in 2002 in C (Borland C++ Builder era).
It is published here as version 0.1 of Chessenger, its predecessor. **It shares no code with Chessenger.**
Sedar was written entirely by the author, by hand. Chessenger (0.2 and later) is a separate program, started from
scratch in 2025 by AI coding assistants under the author's direction.

- Code date: 2002-07-05
- Strength: not measured. Sedar does not speak UCI; it plays in the console (type moves such as `e2e4`).
- The source is published as written in 2002, with Polish identifiers and messages. The only change is that all
  comments were removed.

## Build

```
make
./sedar
```

The code needs `-std=gnu89` (set in the `Makefile`).

## License

GPL-3.0-or-later, see `LICENSE`.

Sedar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version. Sedar is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.
