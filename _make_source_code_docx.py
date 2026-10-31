# -*- coding: utf-8 -*-
"""Generate a 60-page source-code docx for 软著 submission.

Concatenates the project's own C++ headers / sources / Python modules into one
file with line numbers and per-file section headers, formatted as Consolas 9pt
A4 portrait.  Strips third-party (nnUNet) and auto-generated code.
"""
from __future__ import annotations

import os
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_LINE_SPACING
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

OUT = r"/mnt/PhD/pythonproject/PHD_PROGRAM/SpineX-cpp/SpineX_软件源代码.docx"
ROOT = r"/mnt/PhD/pythonproject/PHD_PROGRAM/SpineX-cpp"

# Files in priority order — first 30 pages from project core, last 30 from modules + python
FILES = [
    # ---- App core ----
    "src/main.cpp",
    "src/app/SpineXApp.h",
    "src/app/SpineXApp.cpp",
    "src/app/AppConfig.h",
    "src/app/AppConfig.cpp",
    "src/app/UpdateChecker.h",
    "src/app/UpdateChecker.cpp",
    "src/app/LicenseManager.h",
    "src/app/LicenseManager.cpp",
    # ---- Bridges to Python ----
    "src/bridge/PythonRuntime.h",
    "src/bridge/PythonRuntime.cpp",
    "src/bridge/VolumeExchange.h",
    "src/bridge/VolumeExchange.cpp",
    "src/bridge/InferenceWorker.h",
    "src/bridge/InferenceWorker.cpp",
    # ---- IO ----
    "src/io/NiftiIO.h",
    "src/io/NiftiIO.cpp",
    "src/io/ProjectIO.h",
    "src/io/ProjectIO.cpp",
    # ---- Rendering ----
    "src/rendering/SliceRenderer.h",
    "src/rendering/SliceRenderer.cpp",
    "src/rendering/MaskOverlay.h",
    "src/rendering/MaskOverlay.cpp",
    "src/rendering/MeshGenerator.h",
    "src/rendering/MeshGenerator.cpp",
    # ---- UI ----
    "src/ui/SharedViewport.h",
    "src/ui/SharedViewport.cpp",
    "src/ui/MenuBar.h",
    "src/ui/MenuBar.cpp",
    "src/ui/WelcomeScreen.h",
    "src/ui/WelcomeScreen.cpp",
    "src/ui/CTSliceViewer.h",
    "src/ui/CTSliceViewer.cpp",
    "src/ui/BrushCanvas.h",
    "src/ui/BrushCanvas.cpp",
    "src/ui/LicenseDialog.h",
    "src/ui/LicenseDialog.cpp",
    "src/ui/BCSelectionDialog.h",
    "src/ui/BCSelectionDialog.cpp",
    # ---- Python algorithms ----
    "python/run_segmentation.py",
    "python/run_stress.py",
    "python/cobb_measurement.py",
    "python/model_crypto.py",
    "python/cobb/run_cobb.py",
    "python/cobb/step1_local_coordinate_system.py",
    "python/cobb/step2_endplate_detection.py",
    "python/cobb/step3_cobb_segmentation.py",
    "python/cobb/step4_sagittal_parameters.py",
    # ---- Modules (kept last so the document ends on a C++ closing brace) ----
    "src/modules/ModuleBase.h",
    "src/modules/ModuleBase.cpp",
    "src/modules/DataModule.h",
    "src/modules/DataModule.cpp",
    "src/modules/CTSegmentationModule.h",
    "src/modules/CTSegmentationModule.cpp",
    "src/modules/ModulusEstimationModule.h",
    "src/modules/ModulusEstimationModule.cpp",
    "src/modules/StressPredictionModule.h",
    "src/modules/StressPredictionModule.cpp",
    "src/modules/CobbAngleAddon.h",
    "src/modules/CobbAngleAddon.cpp",
]

# --- 软著 source-code requirements ---
# Provide first 30 + last 30 pages = 60 pages, 50 lines per page, >=3000 lines,
# blank lines removed, page 60 is the code-ending page.
LINES_PER_PAGE = 50
FRONT_PAGES = 30
BACK_PAGES = 30
FRONT_LINES = FRONT_PAGES * LINES_PER_PAGE     # 1500
BACK_LINES = BACK_PAGES * LINES_PER_PAGE       # 1500
APP_VERSION = "V1.0.0"


def read_file(rel: str) -> list[str]:
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return [f"// Source not found: {rel}"]
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def is_useful(line: str) -> bool:
    """Keep meaningful code lines; strip pure-blank lines but keep comments."""
    return bool(line.strip())


def add_page_break(doc):
    p = doc.add_paragraph()
    r = p.add_run()
    br = OxmlElement("w:br")
    br.set(qn("w:type"), "page")
    r._r.append(br)


LINE_HEIGHT_PT = 14   # exact line height: 50 * 14pt = 700pt < usable height -> 50 lines/page


def _set_exact_spacing(p):
    fmt = p.paragraph_format
    fmt.line_spacing_rule = WD_LINE_SPACING.EXACTLY
    fmt.line_spacing = Pt(LINE_HEIGHT_PT)
    fmt.space_before = Pt(0)
    fmt.space_after = Pt(0)


def add_file_header(doc, rel_path: str):
    p = doc.add_paragraph()
    _set_exact_spacing(p)
    pPr = p._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), "E8EEF4")
    pPr.append(shd)
    r = p.add_run(f"// ===== File: {rel_path}  =====")
    r.bold = True
    r.font.name = "Consolas"
    r.font.size = Pt(9)
    r.font.color.rgb = RGBColor(0x1A, 0x33, 0x55)
    return p


def add_code_line(doc, line_no: int, text: str):
    p = doc.add_paragraph()
    _set_exact_spacing(p)
    # Unnumbered code (软著 requirement: no per-file line numbers).
    if len(text) > 118:
        text = text[:115] + "..."
    r2 = p.add_run(text.replace("\t", "    "))
    r2.font.name = "Consolas"
    r2.font.size = Pt(9)
    r2.font.color.rgb = RGBColor(0x1F, 0x29, 0x37)
    return p


def append_page_break(p):
    r = p.add_run()
    br = OxmlElement("w:br")
    br.set(qn("w:type"), "page")
    r._r.append(br)


def build_stream() -> list:
    """Flatten all self-developed source into one stream of render items.

    Each item is ('hdr', path) or ('code', line_no, text).  Blank lines are
    dropped (densify pages); third-party / framework / generated code is never
    listed in FILES, so only独立开发 code is included.
    """
    stream = []
    for rel in FILES:
        lines = [ln.rstrip() for ln in read_file(rel)]
        if not lines:
            continue
        stream.append(("hdr", rel.replace("\\", "/")))
        for i, ln in enumerate(lines, start=1):
            if is_useful(ln):
                stream.append(("code", i, ln))
    return stream


def render(doc, items):
    """Render items, forcing a hard page break after every LINES_PER_PAGE lines."""
    n = len(items)
    for idx, it in enumerate(items, start=1):
        if it[0] == "hdr":
            p = add_file_header(doc, it[1])
        else:
            p = add_code_line(doc, it[1], it[2])
        # Page break after each full page, except after the very last line.
        if idx % LINES_PER_PAGE == 0 and idx != n:
            append_page_break(p)


def main() -> None:
    doc = Document()
    # A4 portrait, 1.5 cm margins
    sec = doc.sections[0]
    sec.page_width  = Cm(21.0)
    sec.page_height = Cm(29.7)
    sec.top_margin = Cm(1.5)
    sec.bottom_margin = Cm(1.5)
    sec.left_margin = Cm(1.5)
    sec.right_margin = Cm(1.5)

    # ---- Cover page ----
    title = doc.add_paragraph()
    title.alignment = 1  # center
    tr = title.add_run("\n\n\n\n\n\nSpineX\n\n软件源代码")
    tr.font.name = "Microsoft YaHei"
    tr.font.size = Pt(28)
    tr.bold = True

    sub = doc.add_paragraph()
    sub.alignment = 1
    sr = sub.add_run(f"\n\n\n\n\n\n申报版本:{APP_VERSION}\n\n开发单位:上海柏恩美医疗科技有限公司")
    sr.font.name = "Microsoft YaHei"
    sr.font.size = Pt(14)

    add_page_break(doc)

    # ---- Source code: first 30 pages + last 30 pages ----
    stream = build_stream()
    total = len(stream)
    if total <= FRONT_LINES + BACK_LINES:
        selected = stream                      # short program: provide all
    else:
        selected = stream[:FRONT_LINES] + stream[-BACK_LINES:]

    render(doc, selected)

    code_lines = sum(1 for it in selected if it[0] == "code")
    last = selected[-1]
    last_text = last[2] if last[0] == "code" else last[1]
    print(f"Full stream lines      : {total}")
    print(f"Lines in document      : {len(selected)}  (code lines: {code_lines})")
    print(f"Pages                  : {len(selected) / LINES_PER_PAGE:.1f}")
    print(f"Last line (page 60 end): {last_text!r}")
    doc.save(OUT)
    print(f"Saved: {OUT}")


if __name__ == "__main__":
    main()
