# -*- coding: utf-8 -*-
"""Replace Section 5.8 (Cobb Angle 模块) of the 软著说明书 docx with the
updated full-spine Cobb + sagittal parameters operation walk-through."""
import shutil
from copy import deepcopy
from docx import Document
from docx.shared import Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH

SRC = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.docx"
BAK = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.s58_backup.docx"
shutil.copy(SRC, BAK)
print("backup:", BAK)

doc = Document(SRC)
body = doc.element.body

# Locate paragraphs that bracket Section 5.8.
start_idx = None
end_idx = None
for i, p in enumerate(doc.paragraphs):
    txt = p.text.strip()
    if start_idx is None and txt.startswith("5.8"):
        start_idx = i
    elif start_idx is not None and txt.startswith("5.9"):
        end_idx = i
        break
if start_idx is None or end_idx is None:
    raise RuntimeError(f"Could not locate 5.8 / 5.9 boundaries: {start_idx=} {end_idx=}")
print(f"5.8 starts at paragraph {start_idx}, 5.9 starts at {end_idx}")

# Capture the run formatting of the existing 5.8 heading so we can re-create
# the new heading with the same look.
heading_p = doc.paragraphs[start_idx]
heading_style = heading_p.style.name

# Drop every paragraph from the 5.8 heading through the paragraph just before 5.9.
to_remove = []
for i in range(start_idx, end_idx):
    to_remove.append(doc.paragraphs[i]._element)
ref = doc.paragraphs[end_idx]._element  # we will insert new content BEFORE this

for el in to_remove:
    el.getparent().remove(el)

# ---- helpers to build new paragraphs and insert them before `ref` ----------
def insert_before(new_p):
    ref.addprevious(new_p._element)

def new_para(text, style=None, indent_first=True, bold=False, color=None):
    p = doc.add_paragraph()
    if style:
        p.style = doc.styles[style]
    run = p.add_run(text)
    run.font.name = "Microsoft YaHei"
    run.font.size = Pt(10.5)
    if bold: run.bold = True
    if color is not None: run.font.color.rgb = RGBColor(*color)
    body.remove(p._element)   # detach from end; re-inserted below
    insert_before(p)
    return p

def H2(text):
    p = doc.add_paragraph(text, style="Heading 2")
    body.remove(p._element); insert_before(p); return p

def H3(text):
    p = doc.add_paragraph(text, style="Heading 3")
    body.remove(p._element); insert_before(p); return p

def P(text):
    p = doc.add_paragraph()
    run = p.add_run(text)
    run.font.name = "Microsoft YaHei"
    run.font.size = Pt(10.5)
    body.remove(p._element); insert_before(p); return p

def STEP(num, text):
    p = doc.add_paragraph()
    r1 = p.add_run(f"步骤 {num}:")
    r1.bold = True; r1.font.name = "Microsoft YaHei"; r1.font.size = Pt(10.5)
    r2 = p.add_run(" " + text)
    r2.font.name = "Microsoft YaHei"; r2.font.size = Pt(10.5)
    body.remove(p._element); insert_before(p); return p

def IMG(label):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run(f"【{label}】")
    r.font.name = "Microsoft YaHei"; r.font.size = Pt(10)
    r.font.color.rgb = RGBColor(0x99, 0x99, 0x99)
    r.italic = True
    body.remove(p._element); insert_before(p); return p

# ============================================================
# Section 5.8 — new content
# ============================================================
H2("5.8 Cobb Angle 全脊柱测量模块")
P("Cobb Angle 模块面向脊柱侧弯及矢状面失衡的临床定量分析,自动检测分割掩膜中所有可用椎体并一次性计算冠状面 Cobb 分段、矢状面四项关键临床指标(TK / CL / cSVA / LL),并以三维交互视图同步展示。")

H3("5.8.1 启动条件")
P("进入本模块前,工作区中必须已有有效的椎骨分割掩膜。掩膜可来源于以下任一途径:")
STEP(1, "在 CT Segmentation 模块中运行 AI 自动分割并完成必要的人工修正后,自动加载至当前项目;")
STEP(2, "通过 File → Open Project 加载已保存的 .si 项目,其中包含历史分割结果;")
STEP(3, "在 Data 模块中选中现成的 Mask NIfTI 文件并加载到 Cobb Angle 分析模块。")
P("若掩膜未保存到磁盘,本模块在执行测量时会自动将内存中的掩膜导出至临时 NIfTI 文件供后台算法读取,用户无需手工保存。")

H3("5.8.2 启动测量")
STEP(1, "在 Modules 菜单切换至 Cobb Angle 模块;")
STEP(2, "点击左侧控制面板顶部的 “Calculate Cobb Angle” 按钮启动全脊柱分析。")
P("点击后进度对话框显示 “Running Cobb pipeline (~30-60s)...”。后台 Python 子进程自动执行如下流程:")
STEP(1, "读取掩膜并枚举全部非零椎体标签;")
STEP(2, "Step 1 — 为每块椎骨基于体素分布建立局部坐标系(LCS);")
STEP(3, "Step 2 — 提取椎体本体并自动检测上、下终板三角面;")
STEP(4, "Step 3 — 拟合每块终板平面,投影至冠状面并按斜率切换检测多段 Cobb 曲线,同时为每段计算顶椎(Apex)、凸向、临床类型(thoracic / lumbar / thoracolumbar);")
STEP(5, "Step 4 — 在矢状面计算 TK、CL、cSVA、LL_L1_L5 四项固定参数,并按医学正常范围标注临床状态。")
P("典型耗时约 30~60 秒(取决于椎体数量与硬件性能)。计算完成后,左侧结果面板与右侧三维视图自动刷新。")
IMG("图 5.8.1:Cobb Angle 模块控制面板与计算按钮")

H3("5.8.3 冠状面 Cobb 曲线结果")
P("结果面板首先列出所有 ≥ 10° 的临床显著侧弯曲线,每段曲线包含:")
STEP(1, "曲线编号(Curve N)与上下端椎(UEV / LEV);")
STEP(2, "Cobb 角度(°),按统一调色板上色;")
STEP(3, "顶椎(APEX)标识;")
STEP(4, "临床类型(thoracic / lumbar / thoracolumbar)与凸向(left / right)。")
P("当未检测到 ≥ 10° 的临床显著曲线时,显示提示 “No clinical curve (≥ 10°) detected”。")
IMG("图 5.8.2:冠状面 Cobb 曲线列表(示例三段曲线 11.6° / 34.5° / 17.9°)")

H3("5.8.4 矢状面临床参数")
P("结果面板下半部分自动展示四项矢状面临床指标。参数同时显示数值、临床状态(normal / abnormal_low / abnormal_high)及参考正常范围,异常值以红色加粗显示,并附带针对性中文诊断结论:")
STEP(1, "TK(胸椎后凸角):T4 椎体上终板 → T12 椎体下终板的矢状面 Cobb 角,正常范围 41-48°,增大提示胸椎后凸加重,减小提示胸椎后凸不足;")
STEP(2, "CL(颈椎前凸角):C2 椎体下终板 → C7 椎体下终板的矢状面 Cobb 角,正常范围 5-12°,增大提示颈椎前凸过度,减小或反向提示颈椎后凸;")
STEP(3, "cSVA(C2-C7 矢状面轴向距离):C2 椎体几何中心铅垂线与 C7 椎体后上缘的水平距离,正常范围 <25 mm,增大提示颈椎矢状面失衡;")
STEP(4, "LL_L1_L5(腰椎前凸角近似):L1 椎体上终板 → L5 椎体下终板的矢状面 Cobb 角,正常范围 43-61°,与 PI 匹配判断脊柱整体平衡(标准定义需用骶骨上终板替代 L5)。")
P("当所需椎体在掩膜中缺失时,对应参数显示为 “unavailable (missing C2,C7)” 等提示,提醒用户回到 CT Segmentation 模块补全分割。")
IMG("图 5.8.3:矢状面参数列表与诊断行")

H3("5.8.5 三维交互视图(冠状/矢状切换)")
P("右侧共享视口提供 Coronal 与 Sagittal 两种视图,通过控制面板上方的 “View: [Coronal] [Sagittal]” 切换按钮实时切换,无需重新计算。两种视图共享底层椎体与端板网格,仅切换可见性与配色,响应延迟通常 <50 ms。")

P("Coronal 视图(默认):", )
STEP(1, "椎骨按所属 Cobb 段染色(8 色循环调色板);")
STEP(2, "顶椎(Apex)金色高亮显示,共享椎(两段曲线交界椎)蓝色加深显示;")
STEP(3, "未参与曲线的椎骨灰色淡化(opacity 30%);")
STEP(4, "相机自动定位到正面视角(沿 +Y 方向),便于观察冠状面侧弯。")
IMG("图 5.8.4:冠状面 3D 视图(段着色 + Apex 金 + 共椎蓝)")

P("Sagittal 视图:")
STEP(1, "C2 与 C7 着色为蓝色(CL 参数),T4 与 T12 红色(TK 参数),L1 与 L5 绿色(LL 参数);")
STEP(2, "其余非端点椎骨灰色透明化,凸出端点椎结构;")
STEP(3, "cSVA 可视化:从 C2 几何中心向下绘制黄色虚线铅垂线,在 C7 高度延伸出黄色实线指向 C7 后上角,长度即 cSVA 值;")
STEP(4, "相机自动转向侧面视角(沿 +X 方向),便于观察矢状面后凸/前凸/失衡。")
IMG("图 5.8.5:矢状面 3D 视图(端点椎着色 + cSVA 黄色 plumb 线)")

H3("5.8.6 测量数据导出与日志")
P("LOG 区域同步打印完整诊断流水,包含每块椎体处理摘要、各 Cobb 段角度与顶椎、四项矢状参数数值与临床状态。该日志可复制粘贴用于病例报告或科研记录。临床报告 PDF 导出功能预留接口,可在后续版本中由用户自定义模板生成。")

P("点击 “Clear Measurement” 按钮可清空全部计算结果(冠状曲线 + 矢状参数 + 3D 高亮),恢复至初始 “No curves yet” 状态,以便对同一掩膜重新调整参数后再次计算。")

# ----- save -----
doc.save(SRC)
print("saved:", SRC)

# verify section 5.8 headings
doc2 = Document(SRC)
print("\n=== 5.8 Headings after rewrite ===")
in_section = False
for p in doc2.paragraphs:
    t = p.text.strip()
    if t.startswith("5.8"): in_section = True
    if in_section and p.style.name.startswith("Heading"):
        print(f"  ({p.style.name}) {t}")
    if t.startswith("5.9"):
        print(f"  ({p.style.name}) {t}")
        break
