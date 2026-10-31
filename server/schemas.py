"""
Pydantic 请求/响应模式定义
"""
from datetime import datetime
from typing import Literal, Optional

from pydantic import BaseModel, EmailStr, Field


# ─── 认证相关 ───────────────────────────────────────────────

class UserRegister(BaseModel):
    """用户注册请求"""
    email: EmailStr
    password: str = Field(..., min_length=10, max_length=128)
    name: str = Field(..., min_length=1, max_length=128)


class UserLogin(BaseModel):
    """用户登录请求"""
    email: EmailStr
    password: str


class TokenResponse(BaseModel):
    """登录成功返回的 Token"""
    access_token: str
    token_type: str = "bearer"


class UserProfile(BaseModel):
    """用户信息响应"""
    id: int
    email: str
    name: str
    is_admin: bool
    is_approved: bool
    created_at: datetime
    allowed_modules: Optional[str] = None  # CSV: "data,segmentation,bmd,strain,cobb"

    model_config = {"from_attributes": True}


# ─── 许可证相关 ─────────────────────────────────────────────

class LicenseVerifyRequest(BaseModel):
    """客户端启动时验证许可证的请求"""
    license_key: str
    machine_fingerprint: str = Field(..., min_length=1, max_length=255)


class LicenseVerifyResponse(BaseModel):
    """许可证验证结果"""
    valid: bool
    tier: Optional[Literal["shared", "private"]] = None
    expires_at: Optional[datetime] = None
    message: str
    allowed_modules: Optional[str] = None  # CSV; None/empty → full access


class LicenseInfo(BaseModel):
    """许可证详情"""
    id: int
    license_key: str
    tier: str
    start_date: datetime
    end_date: datetime
    is_active: bool
    device_count: int
    created_at: datetime

    model_config = {"from_attributes": True}


# ─── 管理端相关 ─────────────────────────────────────────────

class AdminLicenseCreate(BaseModel):
    """管理员创建许可证的请求"""
    user_email: EmailStr              # 用邮箱而不是 user_id
    tier: Literal["shared", "private"] = "shared"
    days: int = 30                     # 有效天数，自动计算起止日期


class AdminLicenseRevoke(BaseModel):
    """管理员吊销许可证的请求"""
    license_key: str


class AdminUserCreate(BaseModel):
    """管理员手动创建用户账号"""
    email: EmailStr
    password: str = Field(..., min_length=6, max_length=128)
    name: str = Field(..., min_length=1, max_length=128)
    auto_approve: bool = True


class AdminUserModules(BaseModel):
    """管理员设置某用户可用的模块"""
    email: EmailStr
    modules: list[str] = Field(default_factory=list)  # e.g. ["data","segmentation","bmd","strain","cobb"]


# ─── 应用版本管理 ────────────────────────────────────────────

class AppVersionPublic(BaseModel):
    """公开的版本信息 (客户端 GET /api/version)"""
    latest: str               # 版本号 "1.1.0"
    url: str                  # 下载链接
    sha256: str
    size: int                 # 文件字节数
    notes: Optional[str] = None
    mandatory: bool = False


class AppVersionDetail(BaseModel):
    """管理端的完整版本信息"""
    id: int
    version: str
    filename: str
    file_size: int
    sha256: str
    notes: Optional[str] = None
    mandatory: bool
    is_active: bool
    created_at: datetime

    model_config = {"from_attributes": True}


class AppVersionCreate(BaseModel):
    """管理员创建新版本（只填元数据，exe 通过 /upload 另上传）"""
    version: str = Field(..., pattern=r"^\d+\.\d+\.\d+(\.\d+)?$")  # semver
    notes: Optional[str] = None
    mandatory: bool = False


class AppVersionFetch(BaseModel):
    """管理员一步发布：填 WeTransfer 链接，服务器端拉取 + 激活"""
    version: str = Field(..., pattern=r"^\d+\.\d+\.\d+(\.\d+)?$")
    notes: Optional[str] = None
    mandatory: bool = False
    wetransfer_url: str = Field(..., min_length=10)


class LicenseDetail(BaseModel):
    """管理端用的完整许可证信息"""
    id: int
    user_id: int
    license_key: str
    tier: str
    start_date: datetime
    end_date: datetime
    is_active: bool
    device_count: int
    created_at: datetime

    model_config = {"from_attributes": True}


class UserWithLicenses(BaseModel):
    """管理端查看的用户信息（含许可证列表）"""
    id: int
    email: str
    name: str
    is_admin: bool
    is_approved: bool
    created_at: datetime
    licenses: list[LicenseDetail]
    allowed_modules: Optional[str] = None
    is_online: bool = False
    last_seen: Optional[datetime] = None

    model_config = {"from_attributes": True}
