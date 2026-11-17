# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""Checks the mods' mockups (docs/mods/img/*.svg):

1. each one parses as XML and is 800 x 480;
2. each text of class "lbl" is a string in the firmware's source: inside a C string literal in core/,
   synth/source/, synth/include/, synthfx-sdk/pack/ or masterfx-sdk/pack/ (the default Synth FX and Master FX plugins; the whole label, or the label as one entry of a list);
3. each page in docs/mods/ links only images that exist, and each image is linked from a page.

Texts of class "val" (values the firmware formats) and "demo" (a project's own names) are listed
with --all but not looked up.

    python tools/docs/check_mockups.py [--all]
"""
from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMG = ROOT / "docs" / "mods" / "img"
PAGES = ROOT / "docs" / "mods"
SOURCES = [ROOT / "core", ROOT / "synth" / "source", ROOT / "synth" / "include", ROOT / "synthfx-sdk" / "pack",
           ROOT / "masterfx-sdk" / "pack"]
NS = "{http://www.w3.org/2000/svg}"
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def literals() -> set[str]:
    found: set[str] = set()
    for base in SOURCES:
        for path in base.rglob("*"):
            if path.suffix not in (".c", ".cpp", ".h", ".hpp", ".inc") or "tests" in path.parts:
                continue
            for s in LITERAL.findall(path.read_text(encoding="utf-8", errors="replace")):
                found.add(s.replace('\\"', '"'))
                # a literal with a 0 in it holds a string after each 0 too (a plugin's short name)
                found.update(part.replace('\\"', '"') for part in s.split("\\0")[1:] if part)
    return found


CONVERSION = re.compile(r"%[-+ 0#]*\d*(?:\.\d+)?(?:hh|h|ll|l)?([sdiuxXc%])")


def format_regex(s: str) -> re.Pattern | None:
    """A printf format as a regex that a whole label must match ("Edit Patch: %s", "M%d")"""
    out, pos, fields = [], 0, 0
    for m in CONVERSION.finditer(s):
        out.append(re.escape(s[pos:m.start()]))
        kind = m.group(1)
        out.append("%" if kind == "%" else ".+" if kind == "s" else r"-?[0-9A-Fa-f]+" if kind in "xX"
                   else "." if kind == "c" else r"-?\d+")
        fields += kind != "%"
        pos = m.end()
    out.append(re.escape(s[pos:]))
    fixed = CONVERSION.sub("", s)
    return re.compile("".join(out)) if fields and re.search("[A-Za-z]", fixed) else None


def in_source(label: str, strings: set[str], formats: list[re.Pattern]) -> bool:
    return label in strings or any(f.fullmatch(label) for f in formats)


def main() -> int:
    show_all = "--all" in sys.argv
    strings = literals()
    formats = [f for f in (format_regex(s) for s in strings if "%" in s) if f]
    failures = 0
    images = sorted(IMG.glob("*.svg"))
    for svg in images:
        try:
            root = ET.parse(svg).getroot()
        except ET.ParseError as e:
            print(f"FAIL {svg.name}: does not parse ({e})")
            failures += 1
            continue
        if (root.get("width"), root.get("height")) != ("800", "480"):
            print(f"FAIL {svg.name}: not 800 x 480")
            failures += 1
        for t in root.iter(NS + "text"):
            kind, label = t.get("class"), (t.text or "")
            if kind == "lbl":
                if not in_source(label.strip(), strings, formats):
                    print(f"FAIL {svg.name}: label not in the source: {label!r}")
                    failures += 1
            elif kind not in ("val", "demo"):
                print(f"FAIL {svg.name}: text without a class: {label!r}")
                failures += 1
            elif show_all:
                print(f"     {svg.name}: {kind} {label!r}")

    linked: set[str] = set()
    for page in sorted(PAGES.glob("*.md")):
        for ref in re.findall(r"\]\((img/[^)]+\.svg)\)", page.read_text(encoding="utf-8")):
            linked.add(Path(ref).name)
            if not (PAGES / ref).exists():
                print(f"FAIL {page.name}: links a missing image {ref}")
                failures += 1
    for svg in images:
        if svg.name not in linked:
            print(f"FAIL {svg.name}: no page links it")
            failures += 1

    print(f"{len(images)} images, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
