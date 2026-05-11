# Finishing HexIt — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement every empty `cmd*` handler in `hexit.cpp`, add a selection model, a functional status bar, an OpenAI-powered "Analyze selection" command, fix the latent bugs identified in the spec, and refresh README/Makefile.

**Architecture:** Vertical slices per the design at `docs/superpowers/specs/2026-05-11-hexit-finish-design.md`. Each slice ends with `make` succeeding and the binary in a working state. The in-memory `stringstream m_buffer` remains the single source of truth in edit mode; disk I/O lives only in `cmdOutputFile`; the AI feature shells out to `curl` and reads `OPENAI_API_KEY` from the environment.

**Tech Stack:** C++ (g++), ncurses, libtermkey, GNU make. The AI feature additionally invokes `curl` as a subprocess. No new linked dependencies.

**Note on TDD:** This project has no unit-test framework and the spec deferred adding one. The plan substitutes "build + targeted smoke run" for unit-test gates. Every task ends with a verification step that runs the binary against a fixture file and exercises the new code path.

---

## File Structure

| File                                      | Role |
|-------------------------------------------|------|
| `hexit.h`                                 | `HexIt` class declaration + `Cursor` struct. Will gain `m_selAnchor`, `m_clipboardByte`, `m_outputFilename` (as `std::string`), `m_inputFilename` (as `std::string`), `promptHex`, `showPopup`, `aiAnalyze`, `insertBytes`, `writeWordToBuffer`, `readByteFromBuffer`. Will lose `operator=`, `HexIt(HexIt*)`, `cmdCursorWord`. |
| `hexit.cpp`                               | All implementation. Every `cmd*` stub is filled in. New helpers added. `setSwitches`, `editMode`, `renderScreen`, `moveCursor` are edited. |
| `hexit_def.h`                             | Add `COLOR_SELECTION` color pair id and a `MODE_LABEL(editing)` macro is unnecessary — string lives in cpp. Add `AI_ENV_VAR_NAME` macro for `"OPENAI_API_KEY"`. |
| `main.cpp`                                | Pass `-o` value into `HexIt` via a new setter `setOutputFilename` so saves can honor it. |
| `Makefile`                                | Add `.PHONY: all clean` and a default `all: hexit` so `make` and `make clean` are unambiguous. |
| `README.md`                               | Full keybinding table, save-target rules, `OPENAI_API_KEY` note for `Ctrl-A`, pointer to `CLAUDE.md`. |
| `fixtures/sample.bin` (new)               | 1 KB binary fixture used by smoke tests in this plan. Generated in Task 0. |

---

## Task 0: Bootstrap fixture and baseline build

**Files:**
- Create: `fixtures/sample.bin`
- Verify: `Makefile`

- [ ] **Step 1: Create the fixture directory and a 1 KB pseudo-random binary file**

Run:
```bash
mkdir -p fixtures
head -c 1024 /dev/urandom > fixtures/sample.bin
ls -la fixtures/sample.bin
```
Expected: file exists, size is 1024 bytes.

- [ ] **Step 2: Verify the baseline build works**

Run:
```bash
make clean && make
```
Expected: produces `./hexit` with no errors. (Warnings are fine.)

- [ ] **Step 3: Confirm print mode works on the fixture**

Run:
```bash
./hexit fixtures/sample.bin | head -5
```
Expected: hex dump rows like `0000000:  XX XX  XX XX ...  ; ........ ;` followed by line count info.

- [ ] **Step 4: Commit the fixture**

```bash
git add fixtures/sample.bin
git commit -m "add 1 KB random binary fixture for smoke testing"
```

---

## Task 1: Wire `m_outputFilename` and prepare member rename

**Files:**
- Modify: `hexit.h:60-70`
- Modify: `hexit.cpp:32-78`
- Modify: `main.cpp:120-125`

- [ ] **Step 1: Convert filename buffers to `std::string` in `hexit.h`**

In `hexit.h`, find the private members block (lines ~62-65):

```cpp
fstream* m_pFile; // file handle
char m_inputFilename[128];
char m_outputFilename[128];
char m_appVersion[16];
```

Replace with:

```cpp
fstream* m_pFile; // file handle
std::string m_inputFilename;
std::string m_outputFilename;
char m_appVersion[16];
```

Also add a public setter just after `setSwitches`:

```cpp
void setOutputFilename(const std::string& path);
```

- [ ] **Step 2: Update the two constructors and add the setter in `hexit.cpp`**

In `HexIt::HexIt()`, delete the line `m_outputFilename[0] = 0;`. (Default-constructed `std::string` is empty.)

In `HexIt::HexIt(char* filename)`, replace `strcpy(m_inputFilename, filename);` with `m_inputFilename = filename;`, and delete `m_outputFilename[0] = 0;`.

Add after the constructors:

```cpp
void HexIt::setOutputFilename(const std::string& path)
{
    m_outputFilename = path;
}
```

- [ ] **Step 3: Update `renderScreen` to use the new string type**

Find the title-rendering block (around line 495):

```cpp
uint centered = HALF_WIDTH(m_uWidth) - HALF_WIDTH((uint)(strlen(m_inputFilename) + 6));
wmove(m_wTitleArea, 0, centered);
waddstr(m_wTitleArea, "File: ");
waddstr(m_wTitleArea, m_inputFilename);
```

Replace with:

```cpp
uint centered = HALF_WIDTH(m_uWidth) - HALF_WIDTH((uint)(m_inputFilename.size() + 6));
wmove(m_wTitleArea, 0, centered);
waddstr(m_wTitleArea, "File: ");
waddstr(m_wTitleArea, m_inputFilename.c_str());
```

- [ ] **Step 4: Pass `-o` into the editor from `main.cpp`**

In `main.cpp`, just after `h.setSwitches(switches);`, add:

```cpp
if (switches & SWITCH_OUTPUT)
    h.setOutputFilename(output_fn);
```

- [ ] **Step 5: Build**

Run: `make clean && make`
Expected: no errors.

- [ ] **Step 6: Commit**

```bash
git add hexit.h hexit.cpp main.cpp
git commit -m "use std::string for filenames; wire -o into editor"
```

---

## Task 2: Implement `cmdOutputFile` and `cmdCloseFile`

**Files:**
- Modify: `hexit.cpp` (the two stubs near the bottom)

- [ ] **Step 1: Add a private helper `saveToDisk()` declaration to `hexit.h`**

In the `private:` section near the other `cmd*` declarations, add:

```cpp
bool saveToDisk();          // returns true on success
void statusMessage(const std::string& msg);  // transient one-frame status bar message
```

Also add a member to hold the transient message — just above `Cursor m_cursor;`:

```cpp
std::string m_statusMessage;
```

- [ ] **Step 2: Implement `saveToDisk` and `statusMessage` in `hexit.cpp`**

Add these after `editCleanup()` at the very end of the file:

```cpp
void HexIt::statusMessage(const std::string& msg)
{
    m_statusMessage = msg;
}

bool HexIt::saveToDisk()
{
    std::string target = m_outputFilename.empty() ? m_inputFilename : m_outputFilename;
    if (target.empty()) {
        statusMessage("no output filename");
        return false;
    }

    std::string data = m_buffer.str();
    std::ofstream out(target.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        statusMessage("save failed: cannot open " + target);
        return false;
    }
    out.write(data.data(), (std::streamsize)data.size());
    if (!out.good()) {
        statusMessage("save failed: write error");
        return false;
    }
    out.close();
    m_bBufferDirty = false;
    statusMessage("saved " + std::to_string(data.size()) + " bytes to " + target);
    return true;
}
```

- [ ] **Step 3: Replace the empty `cmdOutputFile` and `cmdCloseFile` bodies**

Find:

```cpp
void HexIt::cmdOutputFile()
{

}
```

Replace with:

```cpp
void HexIt::cmdOutputFile()
{
    saveToDisk();
}
```

Find:

```cpp
void HexIt::cmdCloseFile()
{

}
```

Replace with:

```cpp
void HexIt::cmdCloseFile()
{
    if (m_bBufferDirty) {
        saveToDisk();
    }
    // m_bRunning is already set to false by the keybinding site.
}
```

- [ ] **Step 4: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 5: Smoke test save**

Run:
```bash
cp fixtures/sample.bin /tmp/hexit-test.bin
./hexit /tmp/hexit-test.bin -e
```

In the editor: press Enter to start editing the first word, type `DEAD`, press Enter to commit, then `Ctrl-O` to save, then `Ctrl-X` to exit.

Then run:
```bash
xxd /tmp/hexit-test.bin | head -1
```
Expected: first two bytes are `de ad`.

- [ ] **Step 6: Commit**

```bash
git add hexit.h hexit.cpp
git commit -m "implement Ctrl-O save and Ctrl-X close-with-save"
```

---

## Task 3: Implement `cmdPageUp` and `cmdPageDn`

**Files:**
- Modify: `hexit.cpp` (the two stubs)

- [ ] **Step 1: Replace the empty `cmdPageDn` and `cmdPageUp` bodies**

Find:

```cpp
void HexIt::cmdPageDn()
{

}
```

Replace with:

```cpp
void HexIt::cmdPageDn()
{
    uint page = m_uHeight * ROW_SIZE;
    uint maxWord = (m_uFileSize > 0) ? ((m_uFileSize - 1) & ~0x1u) : 0;
    uint target = m_cursor.word + page;
    if (target > maxWord) target = maxWord;
    m_cursor.word = target;
    if (m_cursor.editing) toggleEdit(false); // abort in-flight edit
    checkCursorOffscreen();
}
```

Find:

```cpp
void HexIt::cmdPageUp()
{

}
```

Replace with:

```cpp
void HexIt::cmdPageUp()
{
    uint page = m_uHeight * ROW_SIZE;
    m_cursor.word = (m_cursor.word > page) ? (m_cursor.word - page) : 0;
    if (m_cursor.editing) toggleEdit(false);
    checkCursorOffscreen();
}
```

- [ ] **Step 2: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 3: Smoke test paging**

Run: `./hexit fixtures/sample.bin -e`. Press `Ctrl-B` to page down — the row header on the top line should jump by roughly one screen of rows (e.g., from `0000000` to ~`0000280`). Press `Ctrl-Y` to page up — top line returns toward `0000000`. Press `Ctrl-X` to exit.

- [ ] **Step 4: Commit**

```bash
git add hexit.cpp
git commit -m "implement Ctrl-B / Ctrl-Y page navigation"
```

---

## Task 4: Remove dead code (`operator=`, copy ctor, `cmdCursorWord`) and fix the moveCursor mask

**Files:**
- Modify: `hexit.h`
- Modify: `hexit.cpp`

- [ ] **Step 1: Remove the declarations from `hexit.h`**

Delete these lines from `hexit.h`:

```cpp
HexIt(HexIt* h);
```

```cpp
bool operator=(HexIt* h);
```

```cpp
void cmdCursorWord();
```

- [ ] **Step 2: Remove the definitions from `hexit.cpp`**

Delete the entire body of `HexIt::HexIt(HexIt* h)`, `HexIt::operator=`, and `HexIt::cmdCursorWord()`.

- [ ] **Step 3: Fix the mask in `moveCursor`**

In `moveCursor`, find:

```cpp
if( (y < 0 && newY >= 0) ||
    (y > 0 && newY < (m_uFileSize & 0xFFFFFF0)) )
{
    m_cursor.word = newY;
}
```

Replace `0xFFFFFF0` with `0xFFFFFFF0u`:

```cpp
if( (y < 0 && newY >= 0) ||
    (y > 0 && newY < (int)(m_uFileSize & 0xFFFFFFF0u)) )
{
    m_cursor.word = newY;
}
```

- [ ] **Step 4: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 5: Smoke test arrow movement is unchanged**

Run: `./hexit fixtures/sample.bin -e`. Use arrow keys to move the cursor. The highlight should follow without jumps or going off-screen. `Ctrl-X` to exit.

- [ ] **Step 6: Commit**

```bash
git add hexit.h hexit.cpp
git commit -m "remove dead code (operator=, copy ctor, cmdCursorWord) and fix scroll mask"
```

---

## Task 5: Add selection state and shift-arrow extension

**Files:**
- Modify: `hexit.h`
- Modify: `hexit.cpp` — `editMode` dispatcher and arrow handling
- Modify: `hexit_def.h` — add `COLOR_SELECTION`

- [ ] **Step 1: Reserve a color pair id for selection**

In `hexit_def.h`, after `#define COLOR_COMMAND 6`, add:

```cpp
#define COLOR_SELECTION         7
```

- [ ] **Step 2: Initialize the color pair in `editInit`**

In `hexit.cpp`'s `editInit()`, inside the `if(m_bShowColor)` block, after the existing `init_pair(COLOR_COMMAND, ...)` line, add:

```cpp
init_pair(COLOR_SELECTION,      COLOR_BLACK,        COLOR_YELLOW);
```

- [ ] **Step 3: Add the anchor member to the class**

In `hexit.h`, near `Cursor m_cursor;`, add:

```cpp
int64_t m_selAnchor;        // -1 means no selection; otherwise byte offset of the anchor
```

Add `#include <cstdint>` near the top of `hexit.h` if not already pulled in transitively. Then initialize `m_selAnchor(-1)` in both `HexIt::HexIt()` and `HexIt::HexIt(char* filename)` constructor initializer lists.

- [ ] **Step 4: Add `inSelection(byte_pos)` helper to the class**

In `hexit.h` private methods:

```cpp
bool inSelection(uint byte_pos) const;
```

In `hexit.cpp`:

```cpp
bool HexIt::inSelection(uint byte_pos) const
{
    if (m_selAnchor < 0) return false;
    uint a = (uint)m_selAnchor;
    uint b = m_cursor.word + 1; // include the cursor's second byte
    uint lo = (a < b) ? a : b;
    uint hi = (a > b) ? a : b;
    return byte_pos >= lo && byte_pos <= hi;
}
```

- [ ] **Step 5: Handle Shift+arrow in `editMode`**

In `editMode`, the arrow-key block currently looks like:

```cpp
case TERMKEY_SYM_DOWN:
    if(!m_cursor.editing)
        moveCursor(0,ROW_SIZE);
    break;
case TERMKEY_SYM_UP:
    if(!m_cursor.editing)
        moveCursor(0,-ROW_SIZE);
    break;
case TERMKEY_SYM_LEFT:
    if(m_cursor.editing)
        moveNibble(-1);
    else
        moveCursor(-WORD_SIZE,0);
    break;
case TERMKEY_SYM_RIGHT:
    if(m_cursor.editing)
        moveNibble(1);
    else
        moveCursor(WORD_SIZE,0);
    break;
```

Replace it with (note the shift handling at the top of each case):

```cpp
case TERMKEY_SYM_DOWN:
case TERMKEY_SYM_UP:
case TERMKEY_SYM_LEFT:
case TERMKEY_SYM_RIGHT: {
    bool shifted = (key.modifiers & TERMKEY_KEYMOD_SHIFT) != 0;
    if (shifted && m_selAnchor < 0) {
        m_selAnchor = (int64_t)m_cursor.word;
    } else if (!shifted) {
        m_selAnchor = -1;
    }
    if (m_cursor.editing) {
        if (key.code.sym == TERMKEY_SYM_LEFT)  moveNibble(-1);
        if (key.code.sym == TERMKEY_SYM_RIGHT) moveNibble(1);
    } else {
        if (key.code.sym == TERMKEY_SYM_DOWN)  moveCursor(0,  ROW_SIZE);
        if (key.code.sym == TERMKEY_SYM_UP)    moveCursor(0, -ROW_SIZE);
        if (key.code.sym == TERMKEY_SYM_LEFT)  moveCursor(-WORD_SIZE, 0);
        if (key.code.sym == TERMKEY_SYM_RIGHT) moveCursor( WORD_SIZE, 0);
    }
    break;
}
```

- [ ] **Step 6: Render selected bytes with the new color in `renderScreen`**

In `renderScreen`'s inner per-byte loop, find the call to `textColor(byte+j, toWrite);` and replace it with:

```cpp
if (inSelection(byte + j) && !(((byte + j) & 0xFFFFFFFE) == m_cursor.word)) {
    std::stringstream tmp;
    tmp << hex << getCaseFunction() << setw(2) << setfill('0') << (toWrite & 0xFF);
    wattron(m_wEditArea, COLOR_PAIR(COLOR_SELECTION));
    waddstr(m_wEditArea, tmp.str().c_str());
    wattroff(m_wEditArea, COLOR_PAIR(COLOR_SELECTION));
} else {
    textColor(byte + j, toWrite);
}
```

- [ ] **Step 7: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 8: Smoke test selection**

Run: `./hexit fixtures/sample.bin -e`. Hold Shift and press Right several times — bytes between anchor and cursor should highlight in yellow. Release shift, press any arrow without shift — selection clears. `Ctrl-X` to exit.

- [ ] **Step 9: Commit**

```bash
git add hexit.h hexit.cpp hexit_def.h
git commit -m "add Shift+arrow selection model with highlighted range"
```

---

## Task 6: Replace placeholder status line

**Files:**
- Modify: `hexit.cpp` — `renderScreen` status block

- [ ] **Step 1: Replace the status-area rendering**

Find in `renderScreen`:

```cpp
// Render Status Area
wmove(m_wStatusArea, 0, HALF_WIDTH(m_uWidth)-HALF_WIDTH(6));
waddstr(m_wStatusArea, "STATUS");
```

Replace with:

```cpp
// Render Status Area
{
    char left[128];
    uint pct = (m_uFileSize > 0) ? (uint)((100.0 * m_cursor.word) / m_uFileSize) : 0;
    snprintf(left, sizeof(left), " %07X / %07X (%3u%%)  %s%s",
             m_cursor.word, m_uFileSize, pct,
             m_cursor.editing ? "EDIT" : "READ",
             m_bBufferDirty ? "  *modified*" : "");
    wmove(m_wStatusArea, 0, 0);
    waddstr(m_wStatusArea, left);

    if (!m_statusMessage.empty()) {
        int msglen = (int)m_statusMessage.size();
        int col = (int)m_uWidth - msglen - 1;
        if (col > (int)strlen(left) + 2) {
            wmove(m_wStatusArea, 0, col);
            waddstr(m_wStatusArea, m_statusMessage.c_str());
        }
        m_statusMessage.clear(); // one-frame
    }
}
```

- [ ] **Step 2: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 3: Smoke test status bar**

Run: `./hexit fixtures/sample.bin -e`. The status line should now read `0000000 / 0000400 (  0%)  READ`. Move the cursor; the offset and percentage update. Edit a word and commit; `*modified*` should appear. `Ctrl-O` should flash a "saved … bytes" message on the right side of the status bar.

- [ ] **Step 4: Commit**

```bash
git add hexit.cpp
git commit -m "replace placeholder status line with offset/size/mode/dirty + transient messages"
```

---

## Task 7: Prompt-in-command-area helper

**Files:**
- Modify: `hexit.h` — add `promptHex` declaration
- Modify: `hexit.cpp` — implement `promptHex`

- [ ] **Step 1: Declare `promptHex` in `hexit.h`**

In the `private:` block:

```cpp
bool promptHex(const char* label, uint nibbles, uint& out);
```

- [ ] **Step 2: Implement `promptHex`**

Append to `hexit.cpp`:

```cpp
bool HexIt::promptHex(const char* label, uint nibbles, uint& out)
{
    if (nibbles == 0 || nibbles > 8) return false;

    std::string entered;
    out = 0;

    auto redraw = [&]() {
        wbkgd(m_wCommandArea, COLOR_PAIR(COLOR_COMMAND));
        werase(m_wCommandArea);
        wmove(m_wCommandArea, 0, 1);
        waddstr(m_wCommandArea, label);
        waddstr(m_wCommandArea, ": ");
        waddstr(m_wCommandArea, entered.c_str());
        for (size_t i = entered.size(); i < nibbles; ++i) waddch(m_wCommandArea, '_');
        wmove(m_wCommandArea, 1, 1);
        waddstr(m_wCommandArea, "[0-9A-F]  Enter=accept  Esc=cancel  Backspace=delete");
        wrefresh(m_wCommandArea);
    };

    redraw();

    while (true) {
        TermKeyKey key;
        if (termkey_waitkey(m_tk, &key) != TERMKEY_RES_KEY) continue;

        if (key.type == TERMKEY_TYPE_KEYSYM) {
            if (key.code.sym == TERMKEY_SYM_ENTER) {
                while (entered.size() < nibbles) entered.push_back('0');
                out = 0;
                for (char c : entered) {
                    uint v = 0;
                    if (c >= '0' && c <= '9') v = c - '0';
                    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
                    else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
                    out = (out << 4) | v;
                }
                return true;
            }
            if (key.code.sym == TERMKEY_SYM_ESCAPE) return false;
            if (key.code.sym == TERMKEY_SYM_BACKSPACE) {
                if (!entered.empty()) entered.pop_back();
                redraw();
                continue;
            }
        } else if (key.type == TERMKEY_TYPE_UNICODE) {
            char c = (char)key.code.codepoint;
            bool isHex = (c >= '0' && c <= '9') ||
                         (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
            if (isHex && entered.size() < nibbles) {
                entered.push_back(c);
                redraw();
            }
        }
    }
}
```

- [ ] **Step 3: Build**

Run: `make clean && make`
Expected: clean build (helper is unused so far — that's expected).

- [ ] **Step 4: Commit**

```bash
git add hexit.h hexit.cpp
git commit -m "add promptHex command-area input helper"
```

---

## Task 8: Implement `cmdCopyByte` and `cmdPasteByte`

**Files:**
- Modify: `hexit.h` — add `m_clipboardByte`, `m_hasClipboard`
- Modify: `hexit.cpp`

- [ ] **Step 1: Add clipboard members**

In `hexit.h` private members, near `m_uInsertWord`:

```cpp
uint8_t m_clipboardByte;
bool    m_hasClipboard;
```

Initialize both in `HexIt()` and `HexIt(char*)`: `m_clipboardByte(0)`, `m_hasClipboard(false)`.

- [ ] **Step 2: Replace `cmdCopyByte`**

```cpp
void HexIt::cmdCopyByte()
{
    char b = 0;
    m_buffer.seekg(m_cursor.word, std::ios::beg);
    m_buffer.read(&b, 1);
    m_clipboardByte = (uint8_t)b;
    m_hasClipboard = true;

    char msg[40];
    snprintf(msg, sizeof(msg), "copied %02X at %07X", m_clipboardByte, m_cursor.word);
    statusMessage(msg);
    m_buffer.seekg(m_uFilePos);
}
```

- [ ] **Step 3: Replace `cmdPasteByte` and adjust the Ctrl-V keybinding site**

Replace the body of `cmdPasteByte`:

```cpp
void HexIt::cmdPasteByte()
{
    if (!m_hasClipboard) {
        statusMessage("clipboard empty");
        return;
    }
    auto* pbuf = m_buffer.rdbuf();
    if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
        pbuf->sputc((char)m_clipboardByte);
        m_bBufferDirty = true;
        statusMessage("pasted");
    }
}
```

In `editMode`, the existing Ctrl-V handler does:

```cpp
case 'v':
case 'V':
    if(!m_cursor.editing)
    {
        toggleEdit(true);
        cmdPasteByte();
    }
    break;
```

Replace with the simpler:

```cpp
case 'v':
case 'V':
    if(!m_cursor.editing) cmdPasteByte();
    break;
```

- [ ] **Step 4: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 5: Smoke test copy/paste**

Open `./hexit fixtures/sample.bin -e`. Note the first byte. Press `Ctrl-C` — status: "copied XX at 0000000". Move right one word. Press `Ctrl-V`. The byte at the new cursor position should now equal the copied byte. Press `Ctrl-X`.

- [ ] **Step 6: Commit**

```bash
git add hexit.h hexit.cpp
git commit -m "implement Ctrl-C copy / Ctrl-V paste of a single byte"
```

---

## Task 9: Implement `cmdFindByte`

**Files:**
- Modify: `hexit.cpp`

- [ ] **Step 1: Replace `cmdFindByte`**

```cpp
void HexIt::cmdFindByte()
{
    uint val = 0;
    if (!promptHex("Find byte", 2, val)) return;
    uint8_t needle = (uint8_t)(val & 0xFF);

    std::string data = m_buffer.str();
    size_t start = (size_t)m_cursor.word + 1;
    size_t hit = std::string::npos;
    for (size_t i = start; i < data.size(); ++i) {
        if ((uint8_t)data[i] == needle) { hit = i; break; }
    }
    if (hit == std::string::npos) {
        // wrap from beginning to just before start
        for (size_t i = 0; i < start && i < data.size(); ++i) {
            if ((uint8_t)data[i] == needle) { hit = i; break; }
        }
    }
    if (hit == std::string::npos) {
        char msg[32];
        snprintf(msg, sizeof(msg), "byte %02X not found", needle);
        statusMessage(msg);
        return;
    }

    m_cursor.word = (uint)(hit & ~0x1u);
    checkCursorOffscreen();

    char msg[32];
    snprintf(msg, sizeof(msg), "found %02X @ %07zX", needle, hit);
    statusMessage(msg);
}
```

- [ ] **Step 2: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 3: Smoke test find**

Open `./hexit fixtures/sample.bin -e`. Press `Ctrl-W`, type `FF` (a byte likely to exist in random data), press Enter. Cursor should jump to the next byte with value `0xFF` and the status bar should show "found FF @ XXXXXXX". Press Esc during the prompt to cancel.

- [ ] **Step 4: Commit**

```bash
git add hexit.cpp
git commit -m "implement Ctrl-W find-byte with wrap-around"
```

---

## Task 10: Implement `cmdFillWord`

**Files:**
- Modify: `hexit.cpp`

- [ ] **Step 1: Replace `cmdFillWord`**

```cpp
void HexIt::cmdFillWord()
{
    uint val = 0;
    if (!promptHex("Fill word", 4, val)) return;

    auto* pbuf = m_buffer.rdbuf();
    if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
        pbuf->sputc((char)((val >> 8) & 0xFF));
        pbuf->sputc((char)(val & 0xFF));
        m_bBufferDirty = true;
        char msg[40];
        snprintf(msg, sizeof(msg), "filled %04X at %07X", val & 0xFFFF, m_cursor.word);
        statusMessage(msg);
    }
}
```

- [ ] **Step 2: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 3: Smoke test fill**

Open `./hexit fixtures/sample.bin -e`. Press `Ctrl-F`, type `BEEF`, press Enter. The current word should now read `BE EF`. Status: "filled BEEF at 0000000". Press `Ctrl-X` (saves to original — careful, use a copy). Better: run it against `/tmp/hexit-test.bin` as in Task 2.

- [ ] **Step 4: Commit**

```bash
git add hexit.cpp
git commit -m "implement Ctrl-F fill-word with prompted value"
```

---

## Task 11: Implement `cmdInsertWord`, `cmdInsertWordAt`, and rework Ctrl-Shift-F

**Files:**
- Modify: `hexit.h` — declare `insertBytesAt`
- Modify: `hexit.cpp`

- [ ] **Step 1: Add a private helper declaration**

In `hexit.h`:

```cpp
void insertBytesAt(uint pos, const uint8_t* bytes, uint n);
```

- [ ] **Step 2: Implement `insertBytesAt`**

In `hexit.cpp`:

```cpp
void HexIt::insertBytesAt(uint pos, const uint8_t* bytes, uint n)
{
    if (n == 0) return;
    std::string data = m_buffer.str();
    if (pos > data.size()) pos = (uint)data.size();
    data.insert(pos, reinterpret_cast<const char*>(bytes), n);

    // rebuild the stringstream from the new data
    m_buffer.str(std::string());
    m_buffer.clear();
    m_buffer.write(data.data(), (std::streamsize)data.size());
    m_buffer.seekg(0, std::ios::beg);

    m_uFileSize = (uint)data.size();
    m_bBufferDirty = true;
}
```

- [ ] **Step 3: Replace `cmdInsertWord` (now Ctrl-I)**

```cpp
void HexIt::cmdInsertWord()
{
    uint8_t zeros[2] = { 0x00, 0x00 };
    insertBytesAt(m_cursor.word, zeros, 2);
    checkCursorOffscreen();
    statusMessage("inserted 0000");
}
```

- [ ] **Step 4: Replace `cmdInsertWordAt` (Ctrl-Shift-I)**

```cpp
void HexIt::cmdInsertWordAt()
{
    uint val = 0;
    if (!promptHex("Insert word", 4, val)) return;
    uint8_t bytes[2] = { (uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF) };
    insertBytesAt(m_cursor.word, bytes, 2);
    checkCursorOffscreen();
    char msg[40];
    snprintf(msg, sizeof(msg), "inserted %04X", val & 0xFFFF);
    statusMessage(msg);
}
```

- [ ] **Step 5: Rework the capital-F keybinding to a single combined operation**

In `editMode`, find:

```cpp
case 'F':
    cmdFillWord();
    cmdInsertWordAt();
    break;
```

Replace with:

```cpp
case 'F': {
    uint val = 0;
    if (promptHex("Fill+Insert", 4, val)) {
        // fill current word with val
        auto* pbuf = m_buffer.rdbuf();
        if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
            pbuf->sputc((char)((val >> 8) & 0xFF));
            pbuf->sputc((char)(val & 0xFF));
        }
        // then insert another copy after this word
        uint8_t bytes[2] = { (uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF) };
        insertBytesAt(m_cursor.word + WORD_SIZE, bytes, 2);
        m_cursor.word += WORD_SIZE;   // move onto the freshly inserted copy
        checkCursorOffscreen();
        m_bBufferDirty = true;
        char msg[40];
        snprintf(msg, sizeof(msg), "fill+insert %04X", val & 0xFFFF);
        statusMessage(msg);
    }
    break;
}
```

- [ ] **Step 6: Remove the stray `m_uInsertWord++` no-op**

The body of `cmdInsertWord` is replaced; the member `m_uInsertWord` is now unused. Delete its declaration from `hexit.h` and remove any remaining `m_uInsertWord` references (constructors).

- [ ] **Step 7: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 8: Smoke test insert**

Copy fixture: `cp fixtures/sample.bin /tmp/hi.bin`. Run `./hexit /tmp/hi.bin -e`. Note the size in the status bar (0000400). Press `Ctrl-I` — status: "inserted 0000"; size should now read 0000402 and the current word should be `00 00`. Press `Ctrl-Shift-I`, type `CAFE`, Enter — the current word becomes `CA FE` (the two zeros got pushed down). Press `Ctrl-Shift-F`, type `1234`, Enter — the current word becomes `12 34` and the cursor jumps to the next word which also reads `12 34`. `Ctrl-O` to save, `Ctrl-X` to exit. Then:

```bash
xxd /tmp/hi.bin | head -1
```

Expected: bytes `12 34 12 34 ca fe …`.

- [ ] **Step 9: Commit**

```bash
git add hexit.h hexit.cpp
git commit -m "implement Ctrl-I insert / Ctrl-Shift-I insert-with-value / Ctrl-Shift-F fill+insert"
```

---

## Task 12: AI analyze command (`Ctrl-A`)

**Files:**
- Modify: `hexit.h` — declare `cmdAnalyzeAI`, `aiAnalyze`, `showPopup`, `jsonEscape`
- Modify: `hexit.cpp`
- Modify: `hexit_def.h` — add `AI_ENV_VAR_NAME`
- Modify: `editMode` — add the Ctrl-A keybinding

- [ ] **Step 1: Add the env-var name macro**

In `hexit_def.h`:

```cpp
#define AI_ENV_VAR_NAME         "OPENAI_API_KEY"
```

- [ ] **Step 2: Declare new methods in `hexit.h`**

In `private:`:

```cpp
void cmdAnalyzeAI();
std::string aiAnalyze(const std::string& bytes_hex);
void showPopup(const std::string& title, const std::string& body);
std::string jsonEscape(const std::string& s);
std::string extractContent(const std::string& json);
std::string selectionAsHex();
```

Add `#include <cstdlib>` and `#include <cstdio>` if not already pulled in.

- [ ] **Step 3: Implement `jsonEscape` and `extractContent`**

Append to `hexit.cpp`:

```cpp
std::string HexIt::jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Pull the first JSON string value of the "content" key.
// Pragmatic — not a full parser — but handles \\, \", \n, \r, \t, \uXXXX.
std::string HexIt::extractContent(const std::string& json)
{
    const std::string needle = "\"content\":";
    size_t p = json.find(needle);
    if (p == std::string::npos) return std::string();
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) ++p;
    if (p >= json.size() || json[p] != '"') return std::string();
    ++p;

    std::string out;
    while (p < json.size()) {
        char c = json[p++];
        if (c == '"') return out;
        if (c == '\\' && p < json.size()) {
            char esc = json[p++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u':
                    if (p + 4 <= json.size()) {
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char hc = json[p + i];
                            cp <<= 4;
                            if (hc >= '0' && hc <= '9') cp |= hc - '0';
                            else if (hc >= 'a' && hc <= 'f') cp |= 10 + (hc - 'a');
                            else if (hc >= 'A' && hc <= 'F') cp |= 10 + (hc - 'A');
                        }
                        p += 4;
                        if (cp < 0x80) {
                            out += (char)cp;
                        } else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                default: out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return out;
}
```

- [ ] **Step 4: Implement `selectionAsHex`**

```cpp
std::string HexIt::selectionAsHex()
{
    if (m_selAnchor < 0) return std::string();
    uint a = (uint)m_selAnchor;
    uint b = m_cursor.word + 1;
    uint lo = (a < b) ? a : b;
    uint hi = (a > b) ? a : b;
    if (hi >= m_uFileSize) hi = m_uFileSize - 1;

    std::string data = m_buffer.str();
    std::string out;
    out.reserve((hi - lo + 1) * 3);
    for (uint i = lo; i <= hi; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", (unsigned char)data[i]);
        out += buf;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
```

- [ ] **Step 5: Implement `aiAnalyze`**

```cpp
std::string HexIt::aiAnalyze(const std::string& bytes_hex)
{
    const char* key = std::getenv(AI_ENV_VAR_NAME);
    if (!key || !*key) {
        return "Error: " AI_ENV_VAR_NAME " is not set in the environment.";
    }

    std::string sys = "You are a reverse-engineering assistant. The user will paste a short hex byte sequence. Describe likely format, structure, and notable values. Keep it under 300 words.";
    std::string body = std::string("{\"model\":\"gpt-4o-mini\",\"messages\":[")
        + "{\"role\":\"system\",\"content\":\"" + jsonEscape(sys) + "\"},"
        + "{\"role\":\"user\",\"content\":\""   + jsonEscape(bytes_hex) + "\"}]}";

    // write body to a temp file
    char tmpl[] = "/tmp/hexit-aiXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return "Error: mkstemp failed";
    ssize_t written = write(fd, body.data(), body.size());
    close(fd);
    if (written != (ssize_t)body.size()) {
        unlink(tmpl);
        return "Error: failed to write temp body";
    }

    // The API key stays in the environment; the shell expands $OPENAI_API_KEY at exec time.
    std::string cmd = std::string("curl -sS -X POST https://api.openai.com/v1/chat/completions ")
        + "-H 'Content-Type: application/json' "
        + "-H \"Authorization: Bearer $" AI_ENV_VAR_NAME "\" "
        + "--data-binary @" + tmpl + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { unlink(tmpl); return "Error: popen failed"; }
    std::string resp;
    char chunk[4096];
    while (size_t n = fread(chunk, 1, sizeof(chunk), pipe)) resp.append(chunk, n);
    int rc = pclose(pipe);
    unlink(tmpl);

    if (rc != 0) {
        return "Error: curl exited " + std::to_string(rc) + "\n" + resp;
    }
    std::string content = extractContent(resp);
    if (content.empty()) {
        return "Error: no content in response\n" + resp.substr(0, 800);
    }
    return content;
}
```

- [ ] **Step 6: Implement `showPopup`**

```cpp
void HexIt::showPopup(const std::string& title, const std::string& body)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int h = (rows * 6) / 10; if (h < 8) h = (rows < 8 ? rows - 2 : 8);
    int w = (cols * 7) / 10; if (w < 40) w = (cols < 40 ? cols - 2 : 40);
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;

    WINDOW* pop = newwin(h, w, y, x);
    keypad(pop, TRUE);
    box(pop, 0, 0);

    // word-wrap body to (w-2) columns
    std::vector<std::string> lines;
    {
        std::string cur;
        for (size_t i = 0; i < body.size(); ++i) {
            char c = body[i];
            if (c == '\n' || (int)cur.size() >= w - 2) {
                lines.push_back(cur);
                cur.clear();
                if (c == '\n') continue;
            }
            cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    int viewH = h - 2 - 1; // top/bottom border, one footer line
    int offset = 0;

    auto redraw = [&]() {
        werase(pop);
        box(pop, 0, 0);
        if (!title.empty()) {
            mvwprintw(pop, 0, 2, " %s ", title.c_str());
        }
        for (int i = 0; i < viewH && (offset + i) < (int)lines.size(); ++i) {
            mvwaddstr(pop, 1 + i, 1, lines[offset + i].c_str());
        }
        std::string footer = "  Up/Down=scroll  any other key closes  ";
        mvwaddstr(pop, h - 1, 2, footer.c_str());
        wrefresh(pop);
    };

    redraw();

    while (true) {
        TermKeyKey key;
        if (termkey_waitkey(m_tk, &key) != TERMKEY_RES_KEY) continue;
        if (key.type == TERMKEY_TYPE_KEYSYM) {
            if (key.code.sym == TERMKEY_SYM_UP) {
                if (offset > 0) { offset--; redraw(); }
                continue;
            }
            if (key.code.sym == TERMKEY_SYM_DOWN) {
                if (offset + viewH < (int)lines.size()) { offset++; redraw(); }
                continue;
            }
            if (key.code.sym == TERMKEY_SYM_PAGEUP) {
                offset = (offset > viewH) ? offset - viewH : 0; redraw(); continue;
            }
            if (key.code.sym == TERMKEY_SYM_PAGEDOWN) {
                if (offset + viewH < (int)lines.size())
                    offset = std::min(offset + viewH, (int)lines.size() - viewH);
                redraw(); continue;
            }
        }
        break;
    }

    delwin(pop);
    touchwin(stdscr);
    refresh();
    // request a fresh paint of all our windows
    renderScreen();
}
```

- [ ] **Step 7: Implement `cmdAnalyzeAI`**

```cpp
void HexIt::cmdAnalyzeAI()
{
    if (m_selAnchor < 0) {
        statusMessage("select bytes first (Shift+arrows)");
        return;
    }
    std::string hex = selectionAsHex();
    statusMessage("calling OpenAI...");
    renderScreen(); // show the status before the blocking call
    std::string response = aiAnalyze(hex);
    showPopup("Analyze Selection", response);
}
```

- [ ] **Step 8: Wire Ctrl-A in `editMode`**

In the Ctrl-modifier switch, add a new case alongside the others:

```cpp
case 'a':
case 'A':
    if (!m_cursor.editing) cmdAnalyzeAI();
    break;
```

- [ ] **Step 9: Add missing includes**

Top of `hexit.cpp` (after existing includes):

```cpp
#include <vector>
#include <unistd.h>
```

- [ ] **Step 10: Build**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 11: Smoke test offline (no network call)**

Without `OPENAI_API_KEY` set:
```bash
unset OPENAI_API_KEY
./hexit fixtures/sample.bin -e
```
Hold Shift and press Right a few times to select. Press `Ctrl-A`. Popup should display "Error: OPENAI_API_KEY is not set in the environment." Press any key to close. `Ctrl-X` to exit.

- [ ] **Step 12: Online smoke test (only if a valid key is available)**

If you have a valid key in the environment, repeat with `OPENAI_API_KEY=sk-… ./hexit fixtures/sample.bin -e`. Select bytes, `Ctrl-A`. Popup should show the model's analysis. **Do not** put the key on the `./hexit` command line — set it via `export` or use `env`.

- [ ] **Step 13: Commit**

```bash
git add hexit.h hexit.cpp hexit_def.h
git commit -m "implement Ctrl-A analyze-selection via OpenAI (curl subprocess)"
```

---

## Task 13: README and Makefile refresh

**Files:**
- Modify: `README.md`
- Modify: `Makefile`

- [ ] **Step 1: Rewrite README.md**

Replace the file with:

```markdown
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
| Shift+Arrows    | Extend a selection from the cursor's anchor |
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
```

- [ ] **Step 2: Tighten the Makefile**

Replace the contents of `Makefile` with:

```makefile
OBJS    = main.o hexit.o
CC      = g++
DEBUG   = -g
CFLAGS  = -Wall -c
LFLAGS  = -Wall
LDLIBS  = -lcurses -ltermkey
EXE     = hexit

SRCS = \
    main.cpp \
    hexit.cpp

.PHONY: all clean
all: $(EXE)

$(EXE): $(OBJS)
	$(CC) $(DEBUG) $(LFLAGS) -o $(EXE) $(OBJS) $(LDLIBS)

%.o: %.cpp hexit.h hexit_def.h
	$(CC) $(DEBUG) $(CFLAGS) $< -o $@

clean:
	rm -f *.o $(EXE)
```

(Two notable fixes vs. the old file: linker flags follow object files so
`-l` resolution works under modern linkers; `.PHONY` declares `all` and
`clean`; the implicit `.cpp.o` rule is replaced with a pattern rule that
re-builds when the headers change.)

- [ ] **Step 3: Build with the new Makefile**

Run: `make clean && make`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add README.md Makefile
git commit -m "refresh README with full keybindings and AI feature; tighten Makefile"
```

---

## Task 14: Final verification pass

**Files:** none (verification only)

- [ ] **Step 1: Full clean build**

Run: `make clean && make`
Expected: no errors.

- [ ] **Step 2: Run the binary against the fixture in both modes**

```bash
./hexit fixtures/sample.bin | wc -l   # print mode produces lines
./hexit fixtures/sample.bin -e        # opens the editor — Ctrl-X to exit cleanly
```

Expected: print mode writes ~65 lines (1024/16 + footer). Editor opens, renders the status bar, and exits cleanly.

- [ ] **Step 3: Verify all advertised keys**

In `./hexit fixtures/sample.bin -e`, exercise: arrows, Shift+arrows (selection highlighted), Enter (edit), digit keys (writes nibbles), Esc (cancels), Ctrl-Y/Ctrl-B (page), Ctrl-C/Ctrl-V (copy/paste), Ctrl-W (find), Ctrl-F (fill), Ctrl-I (insert), Ctrl-Shift-I (insert with value), Ctrl-Shift-F (fill+insert), Ctrl-A without selection (status "select bytes first"), Ctrl-A with selection and no key (popup with env-var error), Ctrl-O (save), Ctrl-X (exit).

- [ ] **Step 4: Check `git status` is clean**

Run: `git status`
Expected: nothing to commit, working tree clean.

- [ ] **Step 5: Look at the resulting log**

Run: `git log --oneline | head -20`
Expected: a sequence of slice commits matching the tasks above.

---

## Self-review

- **Spec coverage:** every locked decision from the spec maps to a task — save target in T2 (honors `m_outputFilename`), copy/paste in T8, find/fill in T9–T10 (single byte / single word), insert behavior in T11 (zeros / prompted / fill+insert), status bar in T6, selection model in T5, curl transport in T12, modal popup in T12, env-only API key in T12.
- **Placeholders:** none — every step shows the actual code to write or the actual command to run.
- **Type consistency:** `insertBytesAt` (T11) is the only buffer-shifting primitive; `cmdInsertWord` and `cmdInsertWordAt` both call it. `promptHex` signature (T7) is used identically by T9, T10, T11. `showPopup` and `aiAnalyze` are used only by `cmdAnalyzeAI` (T12). `statusMessage` is used by every command that needs feedback. No name drift.
- **Scope:** plan is one feature set (finishing the editor + AI button) with sequential vertical slices that each leave the binary in a working state. No decomposition needed.
