# -*- coding: utf-8 -*-
"""Append Section 四、操作说明 to the 软著说明书 docx."""
import shutil
from docx import Document
from docx.shared import Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH

SRC = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.docx"
BAK = r"H:/pythonproject/PHD_PROGRAM/SpineX-cpp/Spine X 软著说明书.op_backup.docx"
shutil.copy(SRC, BAK)
print("backup:", BAK)

doc = Document(SRC)

def H1(text):
    p = doc.add_paragraph(text, style="Heading 1")
    return p

def H2(text):
    p = doc.add_paragraph(text, style="Heading 2")
    return p

def H3(text):
    p = doc.add_paragraph(text, style="Heading 3")
    return p

def P(text, indent_first=True):
    p = doc.add_paragraph()
    run = p.add_run(text)
    run.font.name = "Microsoft YaHei"
    run.font.size = Pt(10.5)
    return p

def IMG(label):
    """图片占位段落,居中 + 灰色"""
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run(f"【{label}】")
    run.font.name = "Microsoft YaHei"
    run.font.size = Pt(10)
    run.font.color.rgb = RGBColor(0x99, 0x99, 0x99)
    run.italic = True
    return p

def STEP(num, text):
    p = doc.add_paragraph()
    run1 = p.add_run(f"步骤 {num}:")
    run1.bold = True
    run1.font.name = "Microsoft YaHei"
    run1.font.size = Pt(10.5)
    run2 = p.add_run(" " + text)
    run2.font.name = "Microsoft YaHei"
    run2.font.size = Pt(10.5)
    return p

# ============================================================
# 四、操作说明
# ============================================================
H1("四、操作说明")
P("本章详细描述 SpineX 软件各功能模块的操作方法与典型使用流程。操作演示数据以一例完整胸腰椎 CT 序列为例。")

# -------- 4.1 软件启动 --------
H2("4.1 软件启动")
P("用户可通过以下三种方式启动软件:")
STEP(1, "双击桌面 SpineX 图标;")
STEP(2, "在 Windows 开始菜单中搜索 “SpineX” 并点击图标;")
STEP(3, "进入软件安装目录,双击 SpineX.exe 可执行文件。")
P("软件启动后首先显示启动画面(Splash Screen),加载 Qt/VTK 图形组件、Python 解释器及深度学习推理引擎。加载完成后,自动进入用户登录界面。")
IMG("图 4.1.1:SpineX 启动画面")

# -------- 4.2 登录与激活 --------
H2("4.2 登录与激活")
P("SpineX 采用在线许可证验证机制,首次使用需注册账号并激活许可证。")

H3("4.2.1 用户注册")
P("若用户为首次使用,点击登录界面底部的 “Don't have an account? Register” 按钮,切换至注册页面。填写如下信息:")
STEP(1, "姓名 (Name):输入真实姓名,用于许可证绑定;")
STEP(2, "邮箱 (Email):输入有效的电子邮箱,作为登录账号;")
STEP(3, "密码 (Password):长度不少于 10 位,且必须包含字母与数字的组合;")
STEP(4, "点击 “Register” 按钮提交注册信息,系统将向注册邮箱发送验证邮件。")
P("用户需前往邮箱点击验证链接完成账号激活。")
IMG("图 4.2.1:用户注册界面")

H3("4.2.2 用户登录")
P("已注册用户按以下步骤登录:")
STEP(1, "在登录界面 Email 输入框中输入注册邮箱;")
STEP(2, "在 Password 输入框中输入密码;")
STEP(3, "点击 “Sign In” 按钮提交,系统自动向服务端发起认证请求;")
STEP(4, "认证通过后,软件自动检查许可证状态。若许可证有效,直接进入主工作界面;若许可证尚未购买或已过期,软件将跳转至许可证申请界面。")
IMG("图 4.2.2:用户登录界面")

H3("4.2.3 许可证申请与激活")
P("许可证申请界面显示当前账号信息及许可证状态。用户可选择 Community(社区版,数据共享优惠)或 Professional(专业版,数据私有)两种订阅层级,点击 “Request License” 提交申请。")
P("经管理员审核通过并完成付费流程后,系统自动下发许可证文件。软件在下次启动时自动激活,无需用户手动操作。")
IMG("图 4.2.3:许可证申请界面")

# -------- 4.3 主界面介绍 --------
H2("4.3 主界面介绍")
P("成功登录并激活后,软件进入欢迎界面 (Welcome Screen)。欢迎界面顶部显示产品标识与版权声明,中部展示新建项目、打开项目、加载 CT 数据三个快捷入口,底部显示版本号与技术支持联系方式。")
IMG("图 4.3.1:欢迎界面")

P("主工作界面整体分为四个区域:")
STEP(1, "顶部菜单栏:包含 File(文件)、Save(保存)、Modules(模块切换)、Help(帮助)四个主菜单;")
STEP(2, "左侧模块控制面板:根据当前激活的模块显示对应的操作控件,宽度固定在 360~480 像素;")
STEP(3, "右侧共享视口:以四象限布局同时呈现轴状、矢状、冠状切面与三维渲染视图;")
STEP(4, "底部状态栏:显示当前操作提示与版权声明。")
IMG("图 4.3.2:主工作界面布局")

# -------- 4.4 Data 数据模块 --------
H2("4.4 Data 数据模块")
P("Data 模块负责管理原始影像数据的导入、组织与派发。支持 NIfTI(.nii / .nii.gz)、DICOM 序列文件夹、MetaImage(.mha / .mhd)等多种医学影像格式。")

H3("4.4.1 导入单个 NIfTI 文件")
STEP(1, "在菜单栏选择 File → Data Loading,或直接在 Modules 菜单中选择 Data 进入数据模块;")
STEP(2, "点击左侧控制面板中的 “Add Files” 按钮,弹出文件选择对话框;")
STEP(3, "选择一个或多个 .nii 或 .nii.gz 文件,点击 “打开”。软件自动根据文件名提取患者 ID,并按患者层级组织到数据树 (Data Tree) 中;")
STEP(4, "若文件名含 “mask” 或 “seg” 关键字,软件自动识别为分割掩膜 (Mask),显示类型为 “Mask”;否则识别为体积数据 (Volume)。")
IMG("图 4.4.1:NIfTI 文件导入")

H3("4.4.2 导入 DICOM 序列")
STEP(1, "点击 “Add DICOM Folder” 按钮,选择包含一序列 DICOM 文件的文件夹;")
STEP(2, "软件后台调用 VTK DICOM 读取器,将原始 DICOM 序列重采样并转换为标准 NIfTI 格式,保存至临时目录;")
STEP(3, "转换过程中显示进度提示窗口。转换完成后,数据自动加入数据树。")
IMG("图 4.4.2:DICOM 文件夹导入对话框")

H3("4.4.3 加载至分析模块")
P("在数据树中选中 CT 文件(可配合 Ctrl 或 Shift 多选,同时选中对应 Mask),通过下方按钮一键派发到对应分析模块:")
STEP(1, "Load to CT Segmentation:加载到椎骨分割模块;")
STEP(2, "Load to BMD Analysis:加载到骨密度分析模块。")
P("当同时选中 CT 与 Mask 时,软件自动识别类型并分别填充 ct_file_path 与 mask_file_path,避免数据互相覆盖。")
IMG("图 4.4.3:数据加载按钮组")

# -------- 4.5 CT Segmentation 模块 --------
H2("4.5 CT Segmentation 椎骨分割模块")
P("本模块提供 AI 自动分割、人工笔刷编辑、椎体标签管理三大功能,用于从 CT 影像中提取逐椎骨的分割掩膜。")

H3("4.5.1 AI 自动分割")
STEP(1, "进入分割模块后,软件自动在共享视口中加载 CT 影像,并在左侧控制面板显示分割操作按钮;")
STEP(2, "点击 “Run AI Segmentation” 按钮,软件启动两阶段 nnU-Net 推理流程:Stage 1(低分辨率定位)与 Stage 2(高分辨率精分割);")
STEP(3, "推理期间显示进度条,典型 CT(512×512×400)全流程耗时 1~3 分钟(RTX 4090 FP16 加速);")
STEP(4, "完成后自动加载分割结果至视口,各椎骨以不同颜色叠加显示在 CT 切片上。左侧面板 “椎体列表” 自动根据标签值(Label)生成 T1~L5 的可见性开关。")
IMG("图 4.5.1:AI 分割完成界面")

H3("4.5.2 手动笔刷编辑")
P("对 AI 分割结果进行人工修正:")
STEP(1, "在左侧控制面板选择目标椎体标签(例如 L4);")
STEP(2, "点击 “Paint” 按钮进入绘制模式,或点击 “Erase” 进入擦除模式;")
STEP(3, "使用滑块调整笔刷半径(3~50 像素);")
STEP(4, "在任意切片视图上按住左键拖动鼠标,即可在当前切片上绘制或擦除指定椎体掩膜。支持鼠标中键平移视图、滚轮缩放,缩放/平移后笔刷仍精准对齐解剖位置;")
STEP(5, "点击 “Brush Off” 退出笔刷模式,恢复常规切片浏览。")
IMG("图 4.5.2:笔刷编辑操作")

H3("4.5.3 椎体标签管理")
P("左侧 “椎体列表” 面板支持以下操作:")
STEP(1, "单击复选框:切换对应椎体在三维视图中的可见性;")
STEP(2, "双击椎体名称:进入重命名对话框,可自定义标签文本(如 “L4 骨折”);")
STEP(3, "右键菜单:快速跳转至该椎体的中心切片。")

# -------- 4.6 BMD Analysis 模块 --------
H2("4.6 BMD Analysis 骨密度分析模块")
P("BMD 模块在 CT 分割基础上完成骨密度(Bone Mineral Density)计算、弹性模量 (E) 映射与有限元网格生成。")

H3("4.6.1 HU → BMD 转换")
STEP(1, "在 Modules 菜单切换至 BMD Analysis;")
STEP(2, "软件自动读取当前项目的 CT 与 Mask 数据;")
STEP(3, "点击 “Run HU to BMD” 按钮,软件启动 Python 子进程调用 HU2FEM 算法,将 CT 的 Hounsfield Unit 值按校准曲线转换为 BMD(g/cm³);")
STEP(4, "随后根据经验公式 E = coef × BMD^exp 计算每个体素的弹性模量,结果保存为 E_volume.raw + E_volume.nii.gz。")
IMG("图 4.6.1:HU-BMD 转换参数设置")

H3("4.6.2 网格生成 (Meshing)")
STEP(1, "在 BMD Analysis 面板点击 “Meshing (C3D10)” 按钮,弹出网格尺寸输入对话框;")
STEP(2, "输入网格单元目标尺寸(单位 mm,常用 1.0~2.0);")
STEP(3, "确认后软件对每块椎骨执行以下流程:表面网格提取 → Frontal-Delaunay 表面三角化 → Frontal 体积四面体化 → TET10 高阶化 → 输出 Abaqus C3D10 格式 .inp 文件;")
STEP(4, "生成的 .inp 文件存放在临时目录 spinex_mesh_<PID>/,文件名形如 label_20_C3D10.inp(对应 T12 椎骨);")
STEP(5, "网格路径自动写入当前项目 mesh_output_dir 字段,供后续 Strain Prediction 模块与导出功能使用。")
IMG("图 4.6.2:网格生成进度")

H3("4.6.3 结果可视化")
P("生成完成后,视口自动切换为三维模式,以 ANSYS 风格 rainbow 色谱叠加显示 E 分布。用户可通过 Volume Rendering 切换平面切面与体渲染两种模式。")
IMG("图 4.6.3:骨密度三维可视化")

# -------- 4.7 Strain Prediction 模块 --------
H2("4.7 Strain Prediction 应变预测模块")
P("本模块基于预训练的 Hybrid DualEncoder 深度学习模型,输入 CT 几何与骨密度映射,直接预测加载工况下的三维应变分布,无需传统有限元求解。")

H3("4.7.1 选择边界条件")
STEP(1, "点击 “Select BC” 按钮进入边界条件选择对话框;")
STEP(2, "选择加载模式:Compression(压缩)、Flexion(前屈)、Extension(后伸)、Lateral Bending(侧弯)、Torsion(扭转)之一;")
STEP(3, "使用交互式平面 Widget 在三维视图中拖动黄色约束盒,选定椎体的上下终板区域作为位移边界;")
STEP(4, "点击 “确认” 保存边界条件至项目数据。")
IMG("图 4.7.1:边界条件选择界面")

H3("4.7.2 执行应变预测")
STEP(1, "确认边界条件已设置后,点击 “Run Strain Prediction” 按钮;")
STEP(2, "软件 Python 子进程加载加密模型 Hybrid_DualEncoder.pth.enc,结合当前 CT + Mask + E 分布完成推理;")
STEP(3, "推理耗时约 10~30 秒(RTX 4090),结果以 NumPy .npz 文件与 VTU 网格保存。")

H3("4.7.3 三维应变云图")
P("推理完成后,视口自动切换至应变可视化模式,三维视图显示 ANSYS 风格彩色应变云图,二维切片自动叠加半透明应变图层。右侧颜色条 (Color Bar) 显示应变范围(10⁻⁶~10⁻³ 量级)。")
P("用户可通过左侧滑块调整透明度、阈值,以及切换 Von Mises / Principal / Shear 应变分量。")
IMG("图 4.7.2:三维应变云图")

# -------- 4.8 Cobb Angle 模块 --------
H2("4.8 Cobb Angle 测量模块")
P("Cobb 角附件模块用于自动测量脊柱侧弯 Cobb 角。")

H3("4.8.1 椎体选择")
STEP(1, "在 Modules 菜单切换至 Cobb Angle;")
STEP(2, "视图切换为三维单视图模式,默认视角正对冠状面;")
STEP(3, "在左侧椎体列表中依次勾选上端椎 (Upper End Vertebra) 与下端椎 (Lower End Vertebra)。")
IMG("图 4.8.1:Cobb 角椎体选择")

H3("4.8.2 Cobb 角测量与报告")
STEP(1, "点击 “Measure Cobb” 按钮,软件自动对所选椎体拟合终板平面,计算两平面法向量夹角;")
STEP(2, "结果数值(单位:度)实时显示在视图右上角标注框中;")
STEP(3, "同时生成 PDF 报告,包含测量结果、自动标注的冠状面截图与相应的解剖标记。")
IMG("图 4.8.2:Cobb 角自动测量结果")

# -------- 4.9 数据保存与导出 --------
H2("4.9 数据保存与导出")

H3("4.9.1 保存项目 (.si)")
P("在 File 菜单选择 Save Project,或在退出时根据弹窗提示保存。软件将当前 CT、Mask、BMD 结果、网格路径、应变结果等全部数据以自定义 .si 文件打包保存。.si 格式为 ZIP 容器,内部包含元数据 JSON 与二进制影像,兼顾可移植性与读写性能。")
IMG("图 4.9.1:保存项目对话框")

H3("4.9.2 导出 Mask")
P("Save → Save Mask:将当前分割掩膜单独导出为标准 NIfTI .nii.gz 文件,供第三方软件(如 3D Slicer、ITK-SNAP)打开。")

H3("4.9.3 导出 BMD 结果")
P("Save → Export BMD Results...:弹出格式选择对话框,支持两种输出:")
STEP(1, "VTK Unstructured Grid (.vtu):包含节点坐标、E 值与泊松比字段,可直接载入 ParaView 进行可视化分析;")
STEP(2, "CSV (.csv):表头为 “element ID, E”,按单元索引顺序输出每个单元的弹性模量,适合导入 Excel 或 Python 做后处理。")
IMG("图 4.9.2:BMD 导出格式选择")

H3("4.9.4 导出网格 (INP)")
P("Save → Export Mesh (INP)...:弹出目录选择对话框,软件将 Meshing 阶段生成的全部 label_*_C3D10.inp 网格文件复制至用户指定目录。导出的 .inp 为纯网格文件(节点 + C3D10 四面体单元 + 表面 ELSET),不含材料分配或截面定义,可直接导入 Abaqus CAE 进行自定义边界条件与载荷设定。")

# -------- 4.10 退出软件 --------
H2("4.10 退出软件")
P("用户可通过以下三种方式退出软件:")
STEP(1, "在 File 菜单选择 Exit;")
STEP(2, "点击主窗口右上角关闭按钮;")
STEP(3, "使用 Alt+F4 快捷键。")
P("若当前项目存在未保存数据,软件弹出确认对话框,提供 “保存并退出”、“不保存退出”、“取消” 三个选项。选择 “保存并退出” 时,若项目尚未指定保存路径,软件进一步提示用户输入项目名称并选择保存目录。")
IMG("图 4.10.1:退出确认对话框")

doc.save(SRC)
print("saved:", SRC)
print(f"=== final: {len(doc.paragraphs)} paragraphs, {len(doc.tables)} tables ===")
