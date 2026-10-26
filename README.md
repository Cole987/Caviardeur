# Caviardeur

**Permanent document redaction for Windows — fully offline.**

Caviardeur ("redactor" in French) turns PDF, Word, Excel, PowerPoint and image files into a redacted PDF.
Redacted content is **removed from the file**, not just covered by a black box: the text, images and
vector graphics under each area are deleted, the document is cleaned of hidden data, and the result is
verified before you save it. No file ever leaves the computer.

Copyright © 2026 Cole. All rights reserved.

> ⚠️ **Personal use only.** The Windows XP build embeds Windows 2000/98 icons (© Microsoft Corporation)
> and the MuPDF engine (GNU AGPL v3). Do not distribute that executable. See [Licensing](#licensing).

---

## Contents

- [Two editions](#two-editions)
- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Using Caviardeur](#using-caviardeur)
- [What gets removed](#what-gets-removed)
- [Limitations](#limitations)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Licensing](#licensing)

---

## Two editions

| | **Windows XP edition** (`xp/`) | **Windows 10/11 edition** (`caviardeur/`) |
|---|---|---|
| Executable | `xp\dist\Caviardeur.exe` (≈ 7 MB) | `caviardeur\dist\Caviardeur.exe` (≈ 68 MB) |
| Language | C++ / Win32 API | Python / PySide6 (Qt) |
| Runs on | Windows XP SP3 and later (32-bit) | Windows 10 and 11 |
| Office conversion | Office 2007 SP2 and later | Office (any recent version) or LibreOffice |
| Interface | Native classic controls, French / English | Windows 2000 look, French only |
| Redaction engine | MuPDF 1.28.5 (C library) | PyMuPDF |

The XP edition is the most complete one and the focus of this document.

## Features

- **Opens** PDF, Word (`.doc`, `.docx`, `.docm`, `.dot`, `.dotx`, `.rtf`, `.odt`, `.txt`),
  Excel (`.xls`, `.xlsx`, `.xlsm`, `.xlsb`, `.ods`, `.csv`), PowerPoint (`.ppt`, `.pptx`, `.pptm`, `.pps`, `.ppsx`, `.odp`)
  and images (JPG, PNG, TIFF, BMP, GIF). Office files are converted to PDF by the installed Office.
- **Automatic search**: any list of terms (case- and accent-insensitive, matches across line breaks),
  plus built-in detectors for e-mail addresses, French phone numbers, IBANs and French social security numbers.
  Optional "search each word separately" mode (useful for names truncated in spreadsheet cells).
- **Manual areas**: draw rectangles with the mouse, click an area to remove it, right-click a page to redact it entirely.
- **True redaction** of text, images (pixels blanked) and vector graphics under each area.
- **Cleaning**: metadata, XMP, annotations and comments, links, form fields, JavaScript, attachments, bookmarks.
- **Flatten option**: every page becomes a 200 dpi image — nothing at all remains under the page.
- **Verification**: the saved PDF is re-read; any searched term still present is reported before saving.
- **Safe by design**: the original file is never modified; the work folder is deleted when the program closes.
- **Bilingual interface** (XP edition): French and English, switchable at any time.

## Requirements

### Windows XP edition

- **Windows XP** (SP3 recommended) or any later version of Windows, 32- or 64-bit.
- **A CPU with SSE2**: Pentium 4, Pentium M, Celeron D, Athlon 64, Sempron 64 or newer.
  A Pentium III or Athlon XP is *not* supported (the program would close at start-up).
  To check: right-click *My Computer* › *Properties* and read the processor line.
- **For Word, Excel and PowerPoint files: Office 2007 with Service Pack 2**, or Office 2007 with
  Microsoft's free *"Save as PDF or XPS"* add-in, or any later Office.
  To check in Word 2007: *Office button* › *Save As* — the entry *PDF or XPS* must be present.
  PDF and image files do not need Office.
- Optional, for the full Windows 2000 look on XP: *Control Panel* › *Display* › *Appearance* › *Windows Classic style*.

### Windows 10/11 edition

- Windows 10 or 11, and Microsoft Office or LibreOffice for Office formats.

## Installation

No installer. Copy `Caviardeur.exe` anywhere (a USB stick works) and double-click it.

Caviardeur stores a single setting, the interface language, in the registry under
`HKEY_CURRENT_USER\Software\Caviardeur`. Temporary files live in `%TEMP%\Caviardeur\<process id>\`
and are deleted on exit.

## Using Caviardeur

1. **Open a document** — *File › Open* (`Ctrl+O`), the *Browse…* button, drag and drop onto the window,
   or drag a file onto the `.exe` icon. Office documents are converted to PDF first (a few seconds; the window
   does not respond during the conversion, which is normal).
2. **Search** — type one term per line under *Terms to find*, tick the data types to detect, then
   *Find and mark* (`F3`). Every match becomes a red-framed area. A summary lists what was found and which terms were not.
3. **Adjust the areas** — draw extra rectangles on the pages; click an area to remove it; right-click a page for
   *Redact the whole page* or *Remove the areas on page N*.
4. **Save** — *Redact and save the PDF…* (`Ctrl+S`). Caviardeur redacts, cleans and verifies the file, shows a report,
   then asks where to save it (default name: `<original>_redacted.pdf`). Saving over the original is refused.
5. **Review the PDF** before sharing it. Always.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open a document |
| `Ctrl+S` | Redact and save |
| `Ctrl+Z` | Undo the last area (in the terms box, undoes typing instead) |
| `F3` | Find and mark |
| `Ctrl` + `+` / `Ctrl` + `-` | Zoom in / out |
| `Ctrl+0` | Fit width |
| `Ctrl` + mouse wheel | Zoom |
| Arrows, `Page Up`, `Page Down`, `Home`, `End` | Scroll the pages |

### Language

*View › Langue / Language* switches between **Français** and **English** immediately, even with a document open
(your areas are kept). The choice is remembered. On first launch, the Windows display language is used:
French on a French Windows, English otherwise.

### Search details

- Matching ignores case and accents: `helene dupont` finds *Hélène Dupont*; ligatures (ﬁ, œ…) are handled.
- Spaces in a term also match line breaks, so a name split across two lines is found.
- Phone numbers: French formats (`06 12 34 56 78`, `06.12.34.56.78`, `+33 6 12 34 56 78`…), including when split across lines.
- IBAN: two letters, two digits, then groups of 4 characters, with or without spaces.
- Social security numbers: 13 or 15 digits in the French NIR layout, with or without spaces.

## What gets removed

| Item | How |
|---|---|
| Text under an area | Deleted from the page content stream (MuPDF redaction) |
| Images under an area | Overlapping pixels replaced by black |
| Vector graphics touching an area | Removed |
| Document metadata (author, title, dates, producer…) | `Info` dictionary deleted |
| XMP metadata | Removed from the catalog and pages |
| Annotations, comments, links, form fields | Removed from every page |
| JavaScript, open actions, named destinations, attachments | Removed from the catalog |
| Bookmarks (outline), page labels, structure tree | Removed |
| Word revisions and comments | Not exported during conversion |
| PowerPoint speaker notes | Not exported during conversion |
| Excel hidden sheets / non-printed areas | Not exported (only the print area is converted) |
| Unused objects | Dropped: the file is fully rewritten (never saved incrementally) |

With *Flatten pages to images*, each page is additionally rendered at 200 dpi and replaced by a JPEG image,
so the output contains no text layer at all.

## Limitations

- **Scanned documents** have no text layer: automatic search finds nothing (Caviardeur warns you). Draw the areas by hand.
- **Hyphenated words** split at the end of a line ("Dupont-" / "Martin") may escape the search.
- **Text inside images** (logos, screenshots) is not searched; redact it manually.
- **Excel**: a value truncated by a narrow column only appears partially in the PDF; use *search each word separately*.
- **Multi-page TIFF**: only the first page is imported.
- **Verification** checks the searched terms and the redacted snippets; it cannot guess sensitive data you did not mark.
  Reviewing the output remains your responsibility.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| The program closes immediately on XP | CPU without SSE2 (see [Requirements](#requirements)) |
| "Office 2007 can only export to PDF with Service Pack 2…" | Install Office 2007 SP2 or the *Save as PDF or XPS* add-in |
| "Microsoft Word is not installed on this computer." | Install Office, or convert the file to PDF elsewhere |
| "This PDF is password-protected." | Remove the protection (open and re-save without password) |
| Conversion takes long and the window freezes | Normal: Office is working in the background |
| Interface in the wrong language | *View › Langue / Language* |

### Diagnostic mode

Run from a command prompt to get a step-by-step report without any dialog box:

```
Caviardeur.exe /autotest "C:\test\document.doc" "C:\test\output.pdf" "C:\test\report.txt" "Dupont|Martin" ["C:\test\window.bmp"] [/aplatir] [/en|/fr] [/bascule]
```

- `report.txt` (UTF-8) lists each step: opening, conversion, areas found, verification result, errors.
- Terms are separated by `|`. E-mail, phone and IBAN detection are enabled.
- `window.bmp` (optional): screenshot of the main window after the search.
- `/aplatir`: flatten pages to images. `/en`, `/fr`: force the language (not remembered).
  `/bascule`: switch language with the document open (interface test).

`Caviardeur.exe /apropos "C:\test\about.bmp" [/en|/fr]` saves a screenshot of the *About* window.

## Building from source

### Windows XP edition

The build runs on a modern Windows machine and targets Windows XP 32-bit.

1. **Compiler** — WinLibs GCC, 32-bit, MSVCRT runtime:
   ```
   winget install BrechtSanders.WinLibs.POSIX.MSVCRT --architecture x86
   ```
   The default target is `pentiumpro` without SSE, `__MSVCRT_VERSION__ = 0x600`.
2. **MuPDF 1.28.5** — download the official source archive from mupdf.com, extract it into
   `outils/src/mupdf-1.28.5-source`, then build the static libraries with the WinLibs `bin` folder on `PATH`
   (from a shell that provides `sh`, e.g. Git Bash):
   ```
   mingw32-make OS=MINGW build=release CC=gcc CXX=g++ AR=ar \
     HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_PTHREAD=no \
     brotli=no mujs=no html=no xps=no svg=no extract=no tofu=yes tofu_cjk=yes \
     XCFLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DARCH_HAS_SSE=0 -O2" libs
   ```
   `ARCH_HAS_SSE=0` disables MuPDF's SSE4 code paths; JavaScript, HTML/EPUB, XPS, SVG and CJK fonts are left out.
3. **Application**:
   ```
   cd xp
   sh build.sh
   ```
   Produces `xp/dist/Caviardeur.exe`, statically linked (no DLL besides Windows system DLLs).

**XP compatibility checks** performed on the build: every imported function exists on Windows XP
(the only XP-specific one is `PrintWindow`, used by the diagnostic screenshot); the C runtime is the original
`msvcrt.dll`; the PE header requires Windows 4.0. The prebuilt GCC C++ runtime contains SSE2 instructions,
hence the SSE2 CPU requirement.

### Windows 10/11 edition

```
cd caviardeur
pip install -r requirements.txt
python build.py
```

Produces `caviardeur/dist/Caviardeur.exe` (PyInstaller, single file).

## Project layout

```
Caviardeur/
├── README.md                 this file
├── xp/                       Windows XP edition (C++ / Win32)
│   ├── src/
│   │   ├── main.cpp          user interface, language handling, About window, diagnostic mode
│   │   ├── engine.cpp/.h     MuPDF wrapper: rendering, search, redaction, cleaning, verification
│   │   ├── office.cpp/.h     Office 2007+ automation (COM) to convert documents to PDF
│   │   ├── text.cpp/.h       UTF-8/UTF-16, accent folding, translation helpers (TR / TRC)
│   │   ├── app.rc            icons and version information
│   │   └── resource.h        resource and command identifiers
│   ├── icones/               Windows 2000/98 icons embedded in the executable
│   ├── build.sh              build script
│   ├── LISEZMOI.md           French user guide
│   └── dist/Caviardeur.exe   built executable
├── caviardeur/               Windows 10/11 edition (Python / PySide6)
└── outils/                   toolchain downloads and MuPDF sources (build only)
```

## How it works

1. **Copy** — the chosen file is copied into the work folder; the original is never touched.
2. **Convert** — PDFs are used as is; images are wrapped into a one-page PDF; Office files are opened read-only by
   Word, Excel or PowerPoint through COM automation and exported to PDF (`ExportAsFixedFormat` / `SaveAs` PDF),
   without revisions, comments, document properties or speaker notes.
3. **Index** — each page's structured text is extracted with character positions; the text is folded
   (lower case, no accents, ligatures expanded) and searched with regular expressions. A match yields one area per text line.
4. **Redact** — for each area, a PDF *Redact* annotation is created and applied with MuPDF (`pdf_redact_page`):
   text removed, image pixels blanked, touching line art removed, black box drawn.
5. **Clean** — metadata, annotations, links, forms, scripts, attachments and outlines are deleted; the PDF is
   saved in full with garbage collection, compression and content-stream sanitising.
6. **Flatten** (optional) — each redacted page is rendered to an image and rebuilt into a new PDF.
7. **Verify** — the output is reopened; the searched terms and the snippets that were under the areas are looked for
   again; metadata is checked. The report is shown before you choose where to save.

## Licensing

Caviardeur — Copyright © 2026 Cole. All rights reserved.

The Windows XP executable includes the following third-party components:

| Component | Copyright | License |
|---|---|---|
| MuPDF 1.28.5 | © 2006-2026 Artifex Software, Inc. | GNU AGPL v3 |
| jbig2dec | © 2001-2023 Artifex Software, Inc. | GNU AGPL v3 |
| FreeType 2.14.3 | Portions © 2026 The FreeType Project (https://freetype.org). All rights reserved. | FreeType License (FTL) |
| libjpeg 10 | This software is based in part on the work of the Independent JPEG Group. | IJG License |
| OpenJPEG 2.5.4 | © 2002-2014 Université catholique de Louvain (UCL), Professor Benoit Macq, and contributors | BSD 2-Clause |
| Little CMS 2 | © 1998-2026 Marti Maria Saguer | MIT |
| zlib 1.3.2 | © 1995-2026 Jean-loup Gailly and Mark Adler | zlib License |
| GCC runtime (libstdc++, libgcc) | © Free Software Foundation, Inc. | GPL v3 with GCC Runtime Library Exception |
| MinGW-w64 runtime | MinGW-w64 project | Zope Public License 2.1 / public domain |
| Windows 2000 / 98 icons | © Microsoft Corporation. All rights reserved. | Not licensed for redistribution |

**Consequences**

- Because of the Microsoft icons, the XP executable is for **personal use only** and must not be distributed.
  To build a shareable version, remove the icons from `xp/src/app.rc` (the program then needs replacement icons or none).
- MuPDF and jbig2dec are licensed under the **GNU AGPL v3**. If you ever distribute the program (or let others use it
  over a network), you must provide the complete corresponding source code under the AGPL, or obtain a commercial
  license from Artifex.
- The Windows 10/11 edition uses PyMuPDF (AGPL v3), PySide6 (LGPL v3) and pywin32 (PSF license).

The *About* window (*? › About Caviardeur…*) shows the same notices.
