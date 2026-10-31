# -*- coding: utf-8 -*-
"""把操作说明章节从 四 → 五,4.X → 5.X 重新编号。"""
import re
import shutil
from docx import Document

SRC = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.docx"
BAK = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.renum_backup.docx"
shutil.copy(SRC, BAK)
print("backup:", BAK)

doc = Document(SRC)

def renumber(text: str) -> str:
    if not text:
        return text
    # "四、操作说明" → "五、操作说明"
    text = text.replace("四、操作说明", "五、操作说明")
    # 4.X.Y → 5.X.Y  (仅我们操作说明章节用的 4.1~4.10)
    # 图 4.X.Y → 图 5.X.Y
    def sub(m):
        return "5." + m.group(1)
    text = re.sub(r"(?<![\d])4\.(10|[1-9])(?![\d])", sub, text)
    return text

n_changed = 0
for p in doc.paragraphs:
    for run in p.runs:
        new = renumber(run.text)
        if new != run.text:
            run.text = new
            n_changed += 1

# 表格里也扫一遍(以防有引用)
for t in doc.tables:
    for row in t.rows:
        for cell in row.cells:
            for p in cell.paragraphs:
                for run in p.runs:
                    new = renumber(run.text)
                    if new != run.text:
                        run.text = new
                        n_changed += 1

print(f"changed runs: {n_changed}")
doc.save(SRC)
print("saved")

# verify by printing headings
doc2 = Document(SRC)
print("\n=== Headings after renumber ===")
for p in doc2.paragraphs:
    if p.style and "Heading" in p.style.name and p.text.strip():
        print(f"  ({p.style.name}) {p.text}")
