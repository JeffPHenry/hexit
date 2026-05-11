# Finishing HexIt — Design

Date: 2026-05-11
Status: Approved (vertical-slice approach A)

## Goal

Take the existing HexIt prototype (a terminal hex viewer/editor in C++ on
ncurses + libtermkey) from "demo with stubs" to "an editor a stranger could
actually use." Implement every empty `cmd*` handler, give the status bar real
content, add a real selection model, fix latent bugs, refresh docs, and add a
new feature — "Analyze selection with AI" via the OpenAI API.

## Non-goals

- Cross-platform Windows support — macOS + libtermkey + ncurses only.
- Async or threaded HTTP. The AI request blocks; UI freezes for a few seconds
  during the call and that is acceptable for a TUI utility.
- A test framework. There is no harness today and adding one is out of scope.
- Multi-byte find/fill, regex search, or scripting.

## High-level decisions (locked from brainstorming)

| Topic                  | Decision |
|------------------------|----------|
| Save target            | If `-o path` was passed on the CLI, save there; otherwise overwrite the original. |
| Find/Fill/Copy/Paste   | Operate on single byte / single word, matching the existing function names. |
| Insert                 | `Ctrl-I` inserts 2 zero bytes at cursor. `Ctrl-Shift-I` prompts for a 4-nibble word and inserts those 2 bytes. `Ctrl-Shift-F` prompts once, fills the current word with the value AND inserts a duplicate immediately after. |
| Status bar             | `offset / size (pct)  MODE  *modified*` — always-on. |
| Selection              | Real selection model: Shift+arrows extend a highlighted range anchored at the cursor's pre-shift position. |
| AI transport           | Shell out to `curl` (no new linked libraries). |
| AI display             | Modal popup window centered over the editor; "press any key to close." |
| API key                | Read `OPENAI_API_KEY` from environment at call time. Never embedded, never logged, never committed. |

## Slicing — vertical, in order

Each slice ends with a working `make` and a green binary that does more than
the slice before it.

### Slice 1 — Save / Close / PageUp / PageDn

- `cmdOutputFile` (`Ctrl-O`): open `m_outputFilename` (if set by `-o`) else
  `m_inputFilename` for binary write, dump `m_buffer.str()` to it, close, clear
  `m_bBufferDirty`. Report success/failure via a status-line message.
- `cmdCloseFile` (`Ctrl-X`): if `m_bBufferDirty`, call `cmdOutputFile`. Then
  set `m_bRunning = false` (already done at the call site — keep that). The
  `editCleanup` path already handles ncurses teardown.
- `cmdPageDn` (`Ctrl-B`): move `m_cursor.word` forward by
  `m_uHeight * ROW_SIZE` bytes, clamped to `maxFilePos() + last-row-byte`.
  Reuse `checkCursorOffscreen()` to update `m_uFilePos`.
- `cmdPageUp` (`Ctrl-Y`): symmetric, clamp to 0.
- Wire `m_outputFilename` from `main.cpp` — currently the `-o` value lives only
  in `output_fn` and is forgotten on the editor side. Add a setter or pass it
  to the constructor.

### Slice 2 — Bug fixes

- Delete `HexIt::operator=(HexIt*)`. It `memcpy`s the whole object, which would
  alias `m_pFile` and the four `WINDOW*`s. The `HexIt(HexIt*)` copy ctor that
  calls it is also unused — delete that too.
- Replace `m_inputFilename` / `m_outputFilename` (both fixed `char[128]`,
  populated via `strcpy`/`sprintf`) with `std::string`. Adjust call sites.
- Tighten `moveCursor`: the existing clamp uses `0xFFFFFF0` (seven Fs) where it
  presumably wants `0xFFFFFFF0` (mask off low nibble). Fix the mask.
- Fix the `cmdInsertWord` body, which currently just does
  `m_uInsertWord++;` — slice 5 will replace it entirely.

### Slice 3 — Selection model + functional status bar

- New members on `HexIt`:
  - `int64_t m_selAnchor` — `-1` when no selection, else byte offset of one
    end (the anchor); the other end is `m_cursor.word` (extended to the end
    of the current word for the visible range).
- Input dispatch: when an arrow key arrives with `TERMKEY_KEYMOD_SHIFT`, if
  `m_selAnchor < 0` set it to the current `m_cursor.word`. If no shift, set
  `m_selAnchor = -1` (collapse selection). Then move the cursor as today.
- Rendering: in the per-byte loop inside `renderScreen()`, compute
  `inSelection(byte_pos)` as
  `m_selAnchor >= 0 && min(m_selAnchor, m_cursor.word) <= byte_pos <= max(m_selAnchor, m_cursor.word) + 1`.
  When true, draw the byte with a new color pair `COLOR_SELECTION`
  (e.g. yellow on blue). The cursor word itself keeps its existing highlight.
- Status bar (`m_wStatusArea`):
  `printf(" %07X / %07X (%3d%%)  %s%s",
      m_cursor.word, m_uFileSize, percent,
      m_cursor.editing ? "EDIT" : "READ",
      m_bBufferDirty ? "  *modified*" : "")`.
  Refresh every frame.

### Slice 4 — Prompt helper + Copy / Paste / Find / Fill

- New helper `bool promptHex(const char* label, uint nibbles, uint& out)`:
  draws `label: ____` in the command area, reads digits via termkey, handles
  backspace and Esc (returns false), returns true on Enter. Used by Find /
  Fill / InsertAt / "save filename" (though we default to `m_outputFilename`
  so the path prompt isn't needed for Slice 1).
- `cmdCopyByte` (`Ctrl-C`, only when not editing): read the byte at
  `m_cursor.word` from `m_buffer`, store in `m_clipboardByte` (new
  `uint8_t` member), flash a status message.
- `cmdPasteByte` (`Ctrl-V`, only when not editing): the current keybinding
  already calls `toggleEdit(true)` first, then `cmdPasteByte`. We instead
  *replace* this: write the clipboard byte into the buffer at `m_cursor.word`
  (LSB only; the word's high byte is preserved). Mark dirty.
- `cmdFindByte` (`Ctrl-W`): `promptHex("Find byte", 2, val)`. Linear search
  forward from `m_cursor.word + 1`; if found, jump cursor and scroll into
  view via `checkCursorOffscreen`. Otherwise status message
  `"byte XX not found"`.
- `cmdFillWord` (`Ctrl-F`): `promptHex("Fill word", 4, val)`. Write 2 bytes
  to `m_buffer` at `m_cursor.word`, MSB first. Mark dirty. Do NOT enter edit
  mode.

### Slice 5 — Insert / Insert-At / Ctrl-Shift-F

Buffer growth: the in-memory buffer is a `stringstream`. To insert at
position `p`, we materialize `m_buffer.str()`, splice in the new bytes,
rebuild the stringstream, and update `m_uFileSize += 2`.

- `cmdInsertWord` (`Ctrl-I`): insert two `0x00` bytes at `m_cursor.word`.
- `cmdInsertWordAt` (`Ctrl-Shift-I`): `promptHex("Insert word", 4, val)`,
  then insert the two bytes (MSB first).
- The existing `Ctrl-Shift-F` (capital F) chain becomes: prompt once
  via `promptHex("Fill+Insert", 4, val)`, fill the current word with `val`,
  then insert another copy of `val` immediately after the cursor word and
  advance the cursor by 2 so the user sees both copies.
- After any insert, call `checkCursorOffscreen()` and mark dirty.

Remove `cmdCursorWord` from the header and implementation — it has no
binding anywhere and only exists as dead code.

### Slice 6 — AI Analyze

- New keybinding: `Ctrl-A`. Only active when `m_selAnchor >= 0` (a selection
  exists); otherwise status message `"select bytes first (Shift+arrows)"`.
- New helper `std::string aiAnalyze(const std::string& bytes_hex)`:
  - Check `getenv("OPENAI_API_KEY")`. If missing, return an error string —
    do not read from any other source, do not embed a default.
  - Build the JSON body in a `std::ostringstream`:
    ```json
    {
      "model": "gpt-4o-mini",
      "messages": [
        {"role": "system", "content": "You are a reverse-engineering assistant. The user will paste a short hex byte sequence. Describe likely format, structure, and notable values. Keep it under 300 words."},
        {"role": "user", "content": "<hex bytes>"}
      ]
    }
    ```
  - JSON-escape `bytes_hex` and the system message defensively (the hex is
    safe, but a helper makes us future-proof).
  - Write the body to a temp file via `mkstemp` so it never appears in the
    process arg list.
  - `popen` a command of the form:
    ```
    curl -sS -X POST https://api.openai.com/v1/chat/completions \
      -H "Content-Type: application/json" \
      -H "Authorization: Bearer $OPENAI_API_KEY" \
      --data-binary @<tmpfile>
    ```
    The API key is referenced as a shell variable, so it stays in the
    inherited env and is never visible in argv. Unlink the temp file after
    `popen` returns.
  - Read the response, extract `choices[0].message.content` with a small,
    pragmatic JSON-string parser (we don't need full JSON, just the first
    `"content":"…"` value with backslash-escape handling).
- New modal popup helper `showPopup(const std::string& title, const std::string& body)`:
  - Centered `WINDOW*`, ~60% width × 60% height of the screen, with a border.
  - Word-wrap `body` to popup width minus 2; render up to popup height minus
    2 lines.
  - Footer line: `"↑/↓ scroll · any other key closes"`. Loop on
    `termkey_waitkey`, scroll on Up/Down/PgUp/PgDn, return on anything else.
  - On close, request a full `renderScreen()` repaint.

### Slice 7 — README + Makefile

- Update README with the full keybinding table, the `-o` save behavior, and
  the new `OPENAI_API_KEY` requirement for `Ctrl-A`. Add a one-paragraph
  "Architecture" pointer to `CLAUDE.md`.
- Add an `.PHONY: clean all` line and a default `all:` target to the
  Makefile so `make` without arguments behaves predictably and `make clean`
  doesn't get shadowed by a file named `clean`.
- Keep the Makefile free of new libraries — curl is invoked as a subprocess.

## Data flow summary

```
                                      ┌─────────────┐
   key (termkey) ──► editMode loop ──►│ command     │── m_buffer mutations
                                      │ dispatcher  │── m_cursor / m_selAnchor moves
                                      └──────┬──────┘── popup / prompt helpers
                                             │
                                             ▼
                                       renderScreen
                                         │   │   │   │
                                         ▼   ▼   ▼   ▼
                                       title edit status command
```

`m_buffer` remains the single source of truth in edit mode. Disk I/O happens
only on `cmdOutputFile`. The AI call is the only network call, gated on a
selection.

## Error handling

- `cmdOutputFile`: any I/O failure → status-bar message, buffer stays dirty.
- `aiAnalyze`: any of (missing env var, curl exit non-zero, missing/empty
  `content` field) → popup shows a short error including the failure mode.
  Never print the API key, even partially.
- `promptHex`: Esc cancels, Enter accepts. A partial value (e.g. 3 nibbles
  when 4 were asked) is right-padded with `0`s and confirmed silently — this
  matches the in-editor edit mode behavior.
- Insert past end of file: allowed; the buffer simply grows. Insert at an
  odd byte position cannot happen (`m_cursor.word` is always even).

## Security notes

- `OPENAI_API_KEY` is read from the environment only. The code does not log
  it, does not write it to any file, does not put it on the command line of
  `curl` directly (it travels in the env, dereferenced by the shell at exec
  time inside the `popen` child).
- Request bodies are sent over HTTPS via curl with default cert validation.
- The temp file used for `--data-binary @` is created via `mkstemp` (700
  perms) and unlinked immediately after `popen`. It contains the bytes the
  user selected — no secrets.

## Out of scope (deliberately deferred)

- Undo/redo. The brainstorm did not call for it. Editing remains
  destructive within the in-memory buffer.
- Range copy/paste/find. Single byte / single word only.
- The `cmdCursorWord` stub — being deleted, not implemented.
- Tests.
- Windows.
