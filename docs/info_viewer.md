# Dual-Pane File Manager & Built-In File Viewer

The **Polyend Tracker Community Edition (CE)** includes a **Commander-style 2-pane File Manager** (`File → File Manager`) and an integrated **Image & Scrollable Text Viewer** across all three devices (**Original Tracker**, **Tracker+**, and **Tracker Mini**).

Manage samples, projects, presets, themes, notes, and reference images directly on your microSD card without connecting to a computer.

---

## 1. Opening the File Manager

1. Press **`File`** (or open `File` from the module menu on Tracker Mini).
2. Press **Screen Button 8 (`File / Manager`)**.

Both panes remember their directories and cursor positions across visits and when returning from the built-in File Viewer.

---

## 2. File Manager Controls

### 8 Screen Buttons (Aligned with Sample Loader)

| Button | Normal Mode | Holding `Shift` |
| :---: | :--- | :--- |
| **1** | **`Enter`** *(folder / `/..`)* or **`Select`** / **`Unselect`** *(file)* | **`Select`** *(toggles any item, including folders)* |
| **2** | **`New Folder`** | **`Select All`** / **`Clear Sel`** |
| **3** | **`Preview`** *(hold on `.wav`)* or **`View`** *(press on image/text)* | **`Preview Vol`** *(adjust preview volume)* |
| **4** | **`Copy`** *(auto-directional arrow toward opposite pane)* | **`Sync Dir`** *(open opposite pane to active directory)* |
| **5** | **`Move`** *(auto-directional arrow toward opposite pane)* | **`Info`** *(file size or folder file count)* |
| **6** | **`Delete`** | **`Clear Sel`** |
| **7** | **`Rename`** | **`Swap Panes`** |
| **8** | **`Back`** *(return to `File` menu)* | **`Back`** |

### Hardware Keys (Original Tracker, Tracker+, and Tracker Mini)

- **Up / Down / Jog-wheel**: Scroll active pane.
  - **Hold `Shift` + Up / Down / Jog-wheel**: Range multi-select.
  - **Hold `Preview` (Btn 3) + Up / Down / Jog-wheel**: Audition `.wav` files as you scroll.
- **Left / Right**: Switch active pane (`Left` ↔ `Right`). Pressing **Left** while already on the Left pane — or **Right** while already on the Right pane — rewinds that pane to the top (`/..`) and clears its selection.
- **Enter / Insert** *(or Mini Middle)*: Enter folder/`/..`, preview `.wav`, or open viewable image/text files in the built-in Viewer.
- **Copy**: Copies highlighted/selected item(s) to the internal clipboard.
- **Shift + Copy (Paste)**: Pastes clipboard into the active pane (same-directory pastes auto-rename `name_1.ext`, …).
- **Delete**: Deletes highlighted/selected item(s).

---

## 3. Operations, Collisions & Safety Guardrails

- **Non-Blocking Progress & Cancel**: Copying/moving large files or nested folders runs incrementally with a 64-bit byte-weighted progress bar, live `(current/total)` counter, and a **Cancel** button (Btn 7).
- **Name Collisions**: When copying or moving onto an existing item, a prompt lets you choose **Overwrite**, **Skip**, **Rename** (auto-suffixes `_1`, `_2`, …), or **Cancel**, with an **`All: ON/OFF`** toggle for batch operations.
- **Confirmation Policy (`Config → General → File Manager`)**:
  - **`Confirm all`** — Prompts before Copy, Move, Paste, and Delete.
  - **`Confirm delete`** *(default)* — Prompts before Delete only; Copy/Move/Paste run immediately.
  - **`Never ask`** — Skips confirmation prompts (name collisions and protected-folder guards still apply).
- **Root Visibility & Protection**:
  - At SD root (`/`), `/System` and `/.system` are visible so you can manage Themes, Fonts, Patches, Snapshots, and Envelopes directly, while `System Volume Information` and other OS root dotfiles (`.Trash-*`, `.Spotlight-*`, `.DS_Store`) are hidden. Inside subdirectories, dotfiles remain visible so macOS `._*` / `.DS_Store` clutter can be deleted on-device.
  - Root system folders (`/System`, `/.system`, `/Firmware`, `/Workspace`) and the **currently loaded project** are hard-protected from delete, move, and rename.

---

## 4. Built-In Image & Scrollable Text Viewer

Highlight any supported file in File Manager and press **Button 3 (`View`)** or **Enter**:

- **Images**: `.jpg`, `.jpeg` (Baseline JPEG, up to `800 × 390` px, up to `650 KB`).
- **Scrollable Text**: `.txt`, `.md`, `.json`, `.ini`, `.cfg`, `.log` (up to **`128 KB`** / **`8,192` lines**, both LF and CRLF supported).
- **Sibling Navigation**: Automatically scans all viewable images and text files in the same folder (sorted alphabetically — no numbered filename convention required).

### Viewer Controls

| Action | Controls |
| :--- | :--- |
| **Scroll Text (Line)** | **Up / Down Arrow** or **Jog-wheel** |
| **Scroll Text (Page)** | **Screen Button 3 (`PgUp`)** / **Screen Button 4 (`PgDn`)** (10 lines) |
| **Next / Previous Sibling File** | **Right / Left Arrow** or **Screen Button 7 (`>>`)** / **Screen Button 1 (`<<`)** (or Jog-wheel on images) |
| **Return to File Manager** | **Screen Button 8 (`Back`)** *(restores active pane and cursor position)* |

### Converting Images for the FT812 Display

The Tracker's FT812 hardware decoder requires **Baseline JPEG** files (Progressive JPEGs are not supported), YCbCr `4:2:0` or `4:4:4`, at most **`800 × 390` px**:

```bash
# ImageMagick:
magick input.png -resize 800x390 -background black -gravity center -extent 800x390 -interlace none -sampling-factor 4:2:0 -quality 90 output.jpg

# ffmpeg:
ffmpeg -i input.png -vf "scale=800:390:force_original_aspect_ratio=decrease,pad=800:390:(ow-iw)/2:(oh-ih)/2:black" -q:v 2 -pix_fmt yuvj420p output.jpg
```

---

## 5. Editable Firmware Info (`Config → Info`)

Selecting **`Info`** in **`Config`** renders editable text directly inline from **`/System/info.txt`** (or `/.system/info.txt`, up to `2 KB`), falling back to built-in firmware text if the file is absent.
