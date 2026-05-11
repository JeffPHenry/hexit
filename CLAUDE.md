# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HexIt is a terminal hex viewer/editor in C++ using ncurses + libtermkey. It has two modes: a non-interactive print mode (`./hexit file`) that streams the file as hex+ASCII to stdout or `-o` file, and an interactive edit mode (`./hexit file -e`) that drives a four-window TUI via ncurses.

## Build / Run

```
brew install ncurses libtermkey   # one-time, macOS
make                              # produces ./hexit
make clean                        # removes *.o and the binary
./hexit -h                        # usage
```

The `Makefile` has no working `hexit.objs` step — `make` relies on the implicit `.cpp -> .o` rule to build `main.o` and `hexit.o`, then links with `-lcurses -ltermkey`. There is no test suite.

## Architecture

Two-translation-unit layout:

- `main.cpp` — argv parsing → bitmask of `SWITCH_*` flags (see `hexit_def.h`) → constructs one `HexIt` and dispatches to `print()` (stream out) or `editMode()` (TUI).
- `hexit.{h,cpp}` — the entire editor lives in class `HexIt`.
- `hexit_def.h` — all `#define`s, color-pair IDs, layout math, and `SWITCH_*` flag bits. Most "magic" lives here; check it before adding new constants.

Key invariants worth knowing before editing:

- **The whole file is slurped into `m_buffer` (a `stringstream`) on entering edit mode.** All edits mutate this in-memory buffer; nothing is written back to disk yet — `cmdOutputFile` / `cmdCloseFile` are stubs. `m_bBufferDirty` tracks unsaved state.
- **Word = 2 bytes, big-endian on disk.** `m_cursor.word` is always even-aligned; `m_cursor.editWord` holds the 16-bit value being edited as 4 nibbles. `toggleEdit(save=true)` writes the word back to the buffer in two `sputc` calls, swapping byte order to preserve endianness (`hexit.cpp:765-766`).
- **Nibble indexing is inverted:** nibble 0 is the most-significant of the word. `NIBBLE_SHIFT(x) = (3-x)*4`, `NIBBLE_MASK(x) = 0xF << NIBBLE_SHIFT(x)`. `editKey()` clears then ORs into `m_cursor.editWord`.
- **Render path:** `renderScreen()` paints four `WINDOW*`s (title / edit / status / command). Per-byte color is decided by `textColor()`, which checks whether the byte sits inside `m_cursor.word` and whether `m_cursor.editing` is true. `print()` (non-interactive) uses `renderLine()` instead and shares no rendering code with the TUI.
- **Input is dispatched in `editMode()`'s `termkey_waitkey` loop** with three branches: arrow keys (move cursor / nibble), Ctrl+letter (commands — most are still empty stubs at the bottom of `hexit.cpp`), and bare hex digits (`editKey`).
- **`maxFilePos()` clamps scrolling so the last screenful sits flush with EOF.** `checkCursorOffscreen()` is what actually scrolls — `moveCursor` calls it after updating `m_cursor.word`.

## Status of unfinished work

Several command handlers at the bottom of `hexit.cpp` are empty stubs and the keybindings in `editMode()` already route to them: `cmdPageDn`, `cmdPageUp`, `cmdCopyByte`, `cmdPasteByte`, `cmdFindByte`, `cmdFillWord`, `cmdInsertWord`, `cmdInsertWordAt`, `cmdOutputFile`, `cmdCursorWord`, `cmdCloseFile`. The status and command-area UI also still hardcode placeholder text ("STATUS", "HexIt", "COMMAND").

## Gotchas

- `HexIt::operator=(HexIt*)` does a raw `memcpy` of the whole object — using it would alias `m_pFile` and the `WINDOW*` pointers. Don't copy `HexIt` instances.
- `m_inputFilename` / `m_outputFilename` are fixed 128-byte buffers populated via `strcpy`/`sprintf`; long paths will overflow.
- The default-on switches are set in `main()` via `default_on = UPPER | SHOW_BYTE_COUNT | SHOW_ASCII | COLOR`; flipping a default means editing both that line and the corresponding `-x t|f` parser branch.
- Color is hard off if the terminal reports `!has_colors()` even when `-c t` is passed.
