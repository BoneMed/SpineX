"""
SpineX CaaS 服务入口
启动方式: uvicorn main:app --host 0.0.0.0 --port 8000 --reload
"""
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from config import CORS_ORIGINS
from database import Base, engine
from routers import admin, admin_ui, auth, license, payment, payment_ui, version


import asyncio
from datetime import datetime, timedelta, timezone
from database import SessionLocal
from models import User


async def cleanup_expired_registrations():
    """每小时清理 7 天未审批的注册申请"""
    while True:
        await asyncio.sleep(3600)  # 每小时检查一次
        try:
            db = SessionLocal()
            cutoff = datetime.now(timezone.utc) - timedelta(days=7)
            deleted = db.query(User).filter(
                User.is_approved == False,
                User.created_at < cutoff
            ).delete()
            db.commit()
            db.close()
            if deleted:
                print(f"[Cleanup] Removed {deleted} expired registration(s)")
        except Exception as e:
            print(f"[Cleanup] Error: {e}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用启动时自动创建数据库表 + 启动后台清理任务"""
    Base.metadata.create_all(bind=engine)
    task = asyncio.create_task(cleanup_expired_registrations())
    yield
    task.cancel()


app = FastAPI(
    title="SpineX CaaS",
    description="SpineX 脊柱分析云服务 - 许可证管理与用户认证系统",
    version="1.0.0",
    lifespan=lifespan,
)

# CORS 中间件 —— 允许客户端跨域访问
app.add_middleware(
    CORSMiddleware,
    allow_origins=CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 注册路由
app.include_router(auth.router)
app.include_router(license.router)
app.include_router(admin.router)
app.include_router(admin_ui.router)
app.include_router(payment.router)
app.include_router(payment_ui.router)
app.include_router(version.router)


@app.get("/health", tags=["健康检查"])
def health():
    """服务健康检查"""
    return {"status": "ok", "service": "柏恩美软件服务平台", "version": "1.0.0"}


from fastapi.responses import HTMLResponse

@app.get("/", response_class=HTMLResponse, include_in_schema=False)
def homepage():
    """SpineX 产品介绍首页"""
    return """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SpineX - 脊柱影像智能分析平台</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI','Microsoft YaHei',sans-serif;color:#1a2332;background:#fff}
.hero{background:linear-gradient(135deg,#2c5f6f 0%,#1a3a45 100%);color:#fff;padding:80px 20px;text-align:center}
.hero h1{font-size:56px;letter-spacing:4px;margin-bottom:12px}
.hero p{font-size:20px;color:rgba(255,255,255,0.8);max-width:600px;margin:0 auto}
.hero .subtitle{font-size:16px;margin-top:20px;color:rgba(255,255,255,0.6)}
.features{max-width:1000px;margin:60px auto;padding:0 20px}
.features h2{text-align:center;font-size:28px;color:#2c5f6f;margin-bottom:40px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:24px}
.card{background:#f8fafb;border-radius:12px;padding:28px;text-align:center;border:1px solid #e8ecf0}
.card .icon{font-size:40px;margin-bottom:12px}
.card h3{color:#2c5f6f;font-size:18px;margin-bottom:8px}
.card p{color:#6b7280;font-size:14px;line-height:1.6}
.disclaimer{background:#fff3cd;padding:20px;text-align:center;font-size:14px;color:#856404;border-top:1px solid #ffc107}
.footer{background:#f4f6f8;padding:30px 20px;text-align:center;color:#9ca3af;font-size:12px}
.footer a{color:#9ca3af}
.cta{margin-top:32px}
.cta a{display:inline-block;padding:14px 40px;background:#fff;color:#2c5f6f;border-radius:8px;text-decoration:none;font-size:16px;font-weight:bold}
.cta a:hover{background:#e8ecf0}
.about{max-width:800px;margin:40px auto;padding:0 20px 40px;text-align:center}
.about p{color:#6b7280;font-size:15px;line-height:1.8}
@media(max-width:768px){.grid{grid-template-columns:1fr}.hero h1{font-size:36px}}
</style>
</head>
<body>
<div class="hero">
    <h1>SpineX</h1>
    <p>脊柱影像智能分析平台</p>
    <p class="subtitle">基于深度学习的脊柱 CT 自动分割、骨密度分析与可视化</p>
    <div class="cta"><a href="mailto:spinalyzeai@gmail.com">联系我们</a></div>
</div>

<div class="features">
    <h2>核心功能</h2>
    <div class="grid">
        <div class="card">
            <div class="icon">🧠</div>
            <h3>AI 自动分割</h3>
            <p>基于 nnU-Net 深度学习框架，实现脊柱椎体的全自动精准分割，支持 C1-S1 全节段识别。</p>
        </div>
        <div class="card">
            <div class="icon">🦴</div>
            <h3>骨密度分析</h3>
            <p>从 CT 影像直接提取骨密度（BMD）分布数据，生成弹性模量映射，支持 VTU/CSV 导出。</p>
        </div>
        <div class="card">
            <div class="icon">📊</div>
            <h3>3D 可视化</h3>
            <p>多平面重建（MPR）+ 三维体渲染，支持截面切割、材料属性 Jet Colormap 渲染。</p>
        </div>
        <div class="card">
            <div class="icon">☁️</div>
            <h3>云端数据服务</h3>
            <p>患者纵向追踪、骨密度趋势分析、智能预警，数据安全存储于国内服务器。</p>
        </div>
        <div class="card">
            <div class="icon">🔒</div>
            <h3>数据安全</h3>
            <p>所有数据存储于中国大陆服务器，符合数据安全法规要求，支持脱敏与加密传输。</p>
        </div>
        <div class="card">
            <div class="icon">💻</div>
            <h3>桌面应用</h3>
            <p>原生 Windows 桌面应用，基于 C++/Qt6/VTK 构建，GPU 加速渲染，流畅交互体验。</p>
        </div>
    </div>
</div>

<div class="about">
    <h2 style="color:#2c5f6f;margin-bottom:16px">关于我们</h2>
    <p>SpineX 由上海柏恩美医疗科技有限公司自主研发，致力于为脊柱医学研究人员提供高效、智能的影像分析工具。
    我们的团队融合了医学影像、人工智能和生物力学领域的专业知识，持续推动脊柱研究的数字化进程。</p>
    <p style="margin-top:16px"><b>联系方式：</b>spinalyzeai@gmail.com</p>
</div>

<div class="disclaimer">
    ⚠️ 本软件仅供科研和教育用途，未经任何监管机构批准用于临床诊断或治疗。
</div>

<div class="footer">
    © 2025-2026 上海柏恩美医疗科技有限公司 All Rights Reserved<br>
    <a href="https://beian.miit.gov.cn/" target="_blank">沪ICP备2026012808号-1</a>
</div>
</body>
</html>"""
