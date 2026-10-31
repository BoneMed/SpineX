"""
应用版本管理
- 公开: GET /api/version          客户端检查更新
- 管理: POST /api/admin/version   创建版本记录
        POST /api/admin/version/{id}/upload  上传 exe 文件
        POST /api/admin/version/fetch         从 WeTransfer 拉取（推荐大文件跨境）
        GET  /api/admin/versions  列出所有版本
        DELETE /api/admin/version/{id}  删除版本
"""
import hashlib
import os
import re
import subprocess
import threading
from pathlib import Path

import requests
from fastapi import APIRouter, Depends, File, HTTPException, UploadFile, status
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session

from database import SessionLocal, get_db
from models import AppVersion
from schemas import AppVersionCreate, AppVersionDetail, AppVersionFetch, AppVersionPublic
from utils.security import require_admin

router = APIRouter(tags=["版本管理"])

DOWNLOADS_DIR = Path("/opt/spinex-api/downloads")
DOWNLOADS_DIR.mkdir(parents=True, exist_ok=True)


# ─── 公开 API ──────────────────────────────────────────

@router.get("/api/version", response_model=AppVersionPublic)
def get_latest_version(db: Session = Depends(get_db)):
    """客户端启动时调用：获取最新版本信息"""
    v = (
        db.query(AppVersion)
        .filter(AppVersion.is_active == True)
        .order_by(AppVersion.created_at.desc())
        .first()
    )
    if v is None:
        raise HTTPException(status_code=404, detail="无可用版本")

    base = os.getenv("DOWNLOAD_BASE_URL", "https://www.spinex.cn")
    return AppVersionPublic(
        latest=v.version,
        url=f"{base}/downloads/{v.filename}",
        sha256=v.sha256,
        size=v.file_size,
        notes=v.notes,
        mandatory=v.mandatory,
    )


# Static file download (public)
@router.get("/downloads/{filename}")
def download_installer(filename: str):
    # Basic path traversal protection
    if "/" in filename or "\\" in filename or ".." in filename:
        raise HTTPException(status_code=400, detail="Invalid filename")
    path = DOWNLOADS_DIR / filename
    if not path.exists():
        raise HTTPException(status_code=404, detail="File not found")
    return FileResponse(path, filename=filename, media_type="application/octet-stream")


# ─── 管理端 API ────────────────────────────────────────

@router.post(
    "/api/admin/version",
    response_model=AppVersionDetail,
    dependencies=[Depends(require_admin)],
)
def create_version(body: AppVersionCreate, db: Session = Depends(get_db)):
    """创建版本记录 (元数据)。之后用 /upload 上传 exe"""
    existing = db.query(AppVersion).filter(AppVersion.version == body.version).first()
    if existing is not None:
        raise HTTPException(
            status_code=409, detail=f"版本 {body.version} 已存在"
        )
    v = AppVersion(
        version=body.version,
        filename=f"SpineX_Setup_v{body.version}.exe",
        file_size=0,
        sha256="",
        notes=body.notes,
        mandatory=body.mandatory,
        is_active=False,  # 未上传文件前不激活
    )
    db.add(v)
    db.commit()
    db.refresh(v)
    return v


@router.post(
    "/api/admin/version/{version_id}/upload",
    response_model=AppVersionDetail,
    dependencies=[Depends(require_admin)],
)
async def upload_installer(
    version_id: int,
    file: UploadFile = File(...),
    db: Session = Depends(get_db),
):
    """上传 exe 文件 (流式保存到磁盘，支持 GB 级大文件)"""
    v = db.query(AppVersion).filter(AppVersion.id == version_id).first()
    if v is None:
        raise HTTPException(status_code=404, detail="版本记录不存在")

    dst = DOWNLOADS_DIR / v.filename
    sha = hashlib.sha256()
    size = 0

    with open(dst, "wb") as out:
        while True:
            chunk = await file.read(1024 * 1024)  # 1 MB
            if not chunk:
                break
            out.write(chunk)
            sha.update(chunk)
            size += len(chunk)

    v.file_size = size
    v.sha256 = sha.hexdigest()
    v.is_active = True  # 上传完成后激活
    db.commit()
    db.refresh(v)
    return v


@router.get(
    "/api/admin/versions",
    response_model=list[AppVersionDetail],
    dependencies=[Depends(require_admin)],
)
def list_versions(db: Session = Depends(get_db)):
    return (
        db.query(AppVersion)
        .order_by(AppVersion.created_at.desc())
        .all()
    )


@router.delete(
    "/api/admin/version/{version_id}",
    dependencies=[Depends(require_admin)],
)
def delete_version(version_id: int, db: Session = Depends(get_db)):
    v = db.query(AppVersion).filter(AppVersion.id == version_id).first()
    if v is None:
        raise HTTPException(status_code=404, detail="版本不存在")
    # 删除文件
    path = DOWNLOADS_DIR / v.filename
    if path.exists():
        try:
            path.unlink()
        except OSError:
            pass
    db.delete(v)
    db.commit()
    return {"message": f"版本 {v.version} 已删除"}


@router.patch(
    "/api/admin/version/{version_id}/toggle",
    response_model=AppVersionDetail,
    dependencies=[Depends(require_admin)],
)
def toggle_version(version_id: int, db: Session = Depends(get_db)):
    """激活/停用某个版本"""
    v = db.query(AppVersion).filter(AppVersion.id == version_id).first()
    if v is None:
        raise HTTPException(status_code=404, detail="版本不存在")
    v.is_active = not v.is_active
    db.commit()
    db.refresh(v)
    return v


# ─── WeTransfer 中转拉取 ─────────────────────────────────

def _resolve_wetransfer_direct_link(short_url: str) -> str:
    """we.tl 短链 → 真正的 download.wetransfer.com 直连"""
    s = requests.Session()
    s.headers["User-Agent"] = "Mozilla/5.0"
    r = s.get(short_url, allow_redirects=True, timeout=30)
    if r.status_code != 200:
        raise HTTPException(status_code=400, detail=f"WeTransfer 短链解析失败 {r.status_code}")
    m = re.search(r"/downloads/([a-f0-9]+)/([a-f0-9]+)", r.url)
    if not m:
        raise HTTPException(status_code=400, detail="无法解析 WeTransfer transfer id")
    tid, shash = m.group(1), m.group(2)
    csrf_m = re.search(r'name="csrf-token" content="([^"]+)"', r.text)
    csrf = csrf_m.group(1) if csrf_m else ""
    api = f"https://wetransfer.com/api/v4/transfers/{tid}/download"
    resp = s.post(
        api,
        json={"intent": "entire_transfer", "security_hash": shash},
        headers={
            "X-CSRF-Token": csrf,
            "X-Requested-With": "XMLHttpRequest",
            "Content-Type": "application/json",
            "Referer": r.url,
        },
        timeout=30,
    )
    if resp.status_code != 200:
        raise HTTPException(status_code=400, detail=f"WeTransfer API {resp.status_code}")
    direct = resp.json().get("direct_link") or resp.json().get("downloadUrl")
    if not direct:
        raise HTTPException(status_code=400, detail="WeTransfer 响应中无 direct_link")
    return direct


def _background_fetch(version_id: int, direct_url: str, filename: str):
    """后台线程：aria2c 多线程下载，完成后算 SHA256 + 激活"""
    db = SessionLocal()
    try:
        v = db.query(AppVersion).filter(AppVersion.id == version_id).first()
        if v is None:
            return
        dst = DOWNLOADS_DIR / filename
        # 删除残留
        if dst.exists():
            dst.unlink()
        # aria2c 16 路并发
        rc = subprocess.run(
            [
                "aria2c",
                "-x", "16", "-s", "16", "-k", "4M",
                "--console-log-level=warn",
                "--summary-interval=0",
                "-d", str(DOWNLOADS_DIR),
                "-o", filename,
                direct_url,
            ],
            capture_output=True,
            text=True,
            timeout=3600,
        )
        if rc.returncode != 0 or not dst.exists():
            v.notes = (v.notes or "") + f"\n[下载失败] aria2c rc={rc.returncode}: {rc.stderr[:200]}"
            db.commit()
            return
        # 算 SHA256
        sha = hashlib.sha256()
        size = 0
        with open(dst, "rb") as f:
            for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
                sha.update(chunk)
                size += len(chunk)
        v.file_size = size
        v.sha256 = sha.hexdigest()
        v.is_active = True
        db.commit()
    finally:
        db.close()


@router.post(
    "/api/admin/version/fetch",
    response_model=AppVersionDetail,
    dependencies=[Depends(require_admin)],
)
def fetch_from_wetransfer(body: AppVersionFetch, db: Session = Depends(get_db)):
    """从 WeTransfer 链接一步创建版本 + 服务器异步下载 + 激活。

    前端调完立即返回（is_active=false, file_size=0）。
    轮询 GET /api/admin/versions 即可看到下载完成后 is_active 变 true。
    """
    # 唯一性检查
    existing = db.query(AppVersion).filter(AppVersion.version == body.version).first()
    if existing is not None:
        raise HTTPException(status_code=409, detail=f"版本 {body.version} 已存在")

    # 先解析 direct link（在请求线程内做，失败立刻返回用户）
    direct = _resolve_wetransfer_direct_link(body.wetransfer_url)

    filename = f"SpineX_Setup_v{body.version}.exe"
    v = AppVersion(
        version=body.version,
        filename=filename,
        file_size=0,
        sha256="",
        notes=body.notes,
        mandatory=body.mandatory,
        is_active=False,
    )
    db.add(v)
    db.commit()
    db.refresh(v)

    # 后台线程跑 aria2c
    t = threading.Thread(
        target=_background_fetch,
        args=(v.id, direct, filename),
        daemon=True,
    )
    t.start()
    return v
