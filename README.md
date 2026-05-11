# HexIt

A terminal hex viewer / editor in C++ on ncurses + libtermkey.

## Install (macOS)

```bash
brew install ncurses libtermkey
```

## Build

```bash
make
```

Produces `./hexit`. `make clean` removes the binary and objects.

## Usage

```
hexit file [-h][-e][-o output][-u t|f][-a t|f][-b t|f][-c t|f]

  -h   show this help
  -e   open in interactive edit mode (default is non-interactive print)
  -o   path to write the result to (also used as save target in edit mode)
  -u   uppercase hex output (default: true)
  -a   show ASCII column (default: true)
  -b   show byte-count column (default: true)
  -c   use color (default: true, when supported)
```

## Edit-mode keys

| Key             | Action |
|-----------------|--------|
| Arrows          | Move cursor (one word per left/right, one row per up/down) |
| Shift+Arrows    | Extend a selection from the cursor anchor |
| Enter           | Toggle edit on the current word (4 nibbles) |
| 0–9, A–F        | While editing, write a hex nibble |
| Esc             | Cancel an in-flight edit |
| Ctrl-O          | Save: writes to `-o` path if set, otherwise overwrites the input file |
| Ctrl-X          | Save (if dirty) and quit |
| Ctrl-Y          | Page up |
| Ctrl-B          | Page down |
| Ctrl-C          | Copy the byte at the cursor |
| Ctrl-V          | Paste the clipboard byte at the cursor |
| Ctrl-W          | Find a byte (prompts for `XX`) |
| Ctrl-F          | Fill the current word (prompts for `XXXX`) |
| Ctrl-I          | Insert two `00` bytes at the cursor |
| Ctrl-Shift-I    | Insert a prompted word at the cursor |
| Ctrl-Shift-F    | Fill the current word and insert a copy after it |
| Ctrl-A          | Analyze the current selection with OpenAI (requires `OPENAI_API_KEY`) |

### Analyze with AI

`Ctrl-A` sends the selected bytes (as a hex string) to the OpenAI Chat
Completions API using the `gpt-4o-mini` model. The API key is read from
the `OPENAI_API_KEY` environment variable at request time and is never
written to disk or to the command line. The request is made via the
`curl` binary on `$PATH`. The response is displayed in a scrollable
modal; press any non-arrow key to close it.

## Architecture

See [`CLAUDE.md`](./CLAUDE.md) for an overview of the code layout, the
in-memory edit buffer, the per-byte rendering path, and how key input is
dispatched.
