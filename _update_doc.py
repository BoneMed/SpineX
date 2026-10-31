# -*- coding: utf-8 -*-
"""Update 2.1 硬件环境 table in SpineX 软著说明书.docx."""
import shutil
from docx import Document
from docx.shared import Pt

SRC = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.docx"
BAK = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.backup.docx"
shutil.copy(SRC, BAK)
print("backup:", BAK)

doc = Document(SRC)
tbl = doc.tables[0]
print(f"Table[0] rows={len(tbl.rows)} cols={len(tbl.columns)}")

rows_data = [
    ("项目", "最低配置", "推荐配置"),
    ("操作系统", "Windows 10 64 位(版本 1809 / build 17763)或更高", "Windows 11 64 位"),
    ("中央处理器 (CPU)", "Intel / AMD x86-64 架构,四核,主频 ≥ 2.5 GHz,支持 SSE4.2 指令集", "Intel Core i7-12700 / AMD Ryzen 7 5800X 及以上,八核 16 线程"),
    ("内存 (RAM)", "16 GB DDR4", "32 GB DDR4 或 DDR5"),
    ("图形处理器 (GPU)", "NVIDIA 独立显卡,计算能力 (Compute Capability) ≥ 7.5(Turing 架构及以上,如 RTX 2080、RTX 3060)", "NVIDIA RTX 3090 / RTX 4090 / RTX A6000"),
    ("显存 (VRAM)", "16 GB", "24 GB 或更高"),
    ("硬盘空间", "10 GB 可用空间", "50 GB 可用空间(含临时文件、模型缓存、推理中间结果)"),
    ("存储介质", "机械硬盘 (HDD),支持 SATA 3.0 及以上", "固态硬盘 (SSD),NVMe 优先"),
    ("显示器", "1920 × 1080 分辨率", "2560 × 1440 分辨率或以上,24 英寸及以上"),
]

# 当前表 9 行(header + 7 数据 + 1 空),新表 9 行(header + 8 数据)。刚好占满,无需增减。
# 若行数不足则追加;多余则留空。
while len(tbl.rows) < len(rows_data):
    tbl.add_row()

for ri, row_content in enumerate(rows_data):
    row = tbl.rows[ri]
    for ci, text in enumerate(row_content):
        cell = row.cells[ci]
        # 清空 cell 现有段落
        for p in list(cell.paragraphs):
            p._element.getparent().remove(p._element)
        p = cell.add_paragraph()
        run = p.add_run(text)
        # 中英文字体
        run.font.name = "Microsoft YaHei"
        run.font.size = Pt(10.5)

# 清空多余行(如果存在)
for ri in range(len(rows_data), len(tbl.rows)):
    row = tbl.rows[ri]
    for cell in row.cells:
        for p in list(cell.paragraphs):
            p._element.getparent().remove(p._element)
        cell.add_paragraph("")

doc.save(SRC)
print("saved:", SRC)
print("=== new table ===")
doc2 = Document(SRC)
for ri, row in enumerate(doc2.tables[0].rows):
    cells = [c.text.replace("\n", " ")[:50] for c in row.cells]
    print(f"  R{ri}: {cells}")
