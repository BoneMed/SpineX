"""
SQLAlchemy 数据库模型定义
"""
import uuid
from datetime import datetime, timezone

from sqlalchemy import (
    Boolean,
    Column,
    DateTime,
    Enum,
    ForeignKey,
    Integer,
    String,
    Text,
)
from sqlalchemy.orm import relationship

from database import Base


class User(Base):
    """用户模型"""

    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    email = Column(String(255), unique=True, index=True, nullable=False)
    hashed_password = Column(Text, nullable=False)
    name = Column(String(128), nullable=False)
    is_admin = Column(Boolean, default=False, nullable=False)
    is_approved = Column(Boolean, default=False, nullable=False)  # 管理员审核通过后才能登录
    # CSV of allowed module keys: data,segmentation,bmd,strain,cobb
    # None or empty → full access (backward compat for existing users)
    allowed_modules = Column(Text, nullable=True)
    created_at = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )

    # 关联
    licenses = relationship("License", back_populates="user", cascade="all, delete-orphan")
    devices = relationship("Device", back_populates="user", cascade="all, delete-orphan")


class License(Base):
    """许可证模型 - 支持 shared 和 private 两种级别"""

    __tablename__ = "licenses"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    license_key = Column(
        String(36),
        unique=True,
        index=True,
        nullable=False,
        default=lambda: str(uuid.uuid4()),
    )
    tier = Column(
        Enum("shared", "private", name="license_tier"),
        nullable=False,
        default="shared",
    )
    start_date = Column(DateTime(timezone=True), nullable=False)
    end_date = Column(DateTime(timezone=True), nullable=False)
    is_active = Column(Boolean, default=True, nullable=False)
    created_at = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )

    # 关联
    user = relationship("User", back_populates="licenses")
    devices = relationship("Device", back_populates="license", cascade="all, delete-orphan")


class Device(Base):
    """设备模型 - 记录哪些机器在使用哪个许可证"""

    __tablename__ = "devices"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    license_id = Column(Integer, ForeignKey("licenses.id", ondelete="CASCADE"), nullable=False)
    machine_fingerprint = Column(String(255), nullable=False, index=True)
    last_seen = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )
    created_at = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )

    # 关联
    user = relationship("User", back_populates="devices")
    license = relationship("License", back_populates="devices")


class LicenseRequest(Base):
    """续费申请"""

    __tablename__ = "license_requests"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    user_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    tier = Column(String(20), default="community", nullable=False)  # community / professional
    status = Column(String(20), default="pending", nullable=False)  # pending / approved / rejected
    created_at = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )

    user = relationship("User")


class AppVersion(Base):
    """应用版本管理"""

    __tablename__ = "app_versions"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    version = Column(String(32), unique=True, nullable=False)  # e.g. "1.1.0"
    filename = Column(String(255), nullable=False)  # e.g. "SpineX_Setup_v1.1.0.exe"
    file_size = Column(Integer, nullable=False)  # bytes
    sha256 = Column(String(64), nullable=False)
    notes = Column(Text, nullable=True)  # release notes (markdown / plain)
    mandatory = Column(Boolean, default=False, nullable=False)  # force update
    is_active = Column(Boolean, default=True, nullable=False)   # publicly available
    created_at = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
    )
