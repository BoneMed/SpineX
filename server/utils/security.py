"""
安全工具：JWT Token 生成/验证 + 密码哈希
"""
from datetime import datetime, timedelta, timezone

from fastapi import Depends, Header, HTTPException, status
from jose import JWTError, jwt
from passlib.context import CryptContext
from sqlalchemy.orm import Session

from config import (
    ACCESS_TOKEN_EXPIRE_MINUTES,
    ADMIN_KEY,
    JWT_ALGORITHM,
    JWT_SECRET_KEY,
)
from database import get_db
from models import User

# bcrypt 密码哈希上下文
pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


# ─── 密码工具 ───────────────────────────────────────────────

def hash_password(plain: str) -> str:
    """对明文密码进行 bcrypt 哈希"""
    return pwd_context.hash(plain)


def verify_password(plain: str, hashed: str) -> bool:
    """验证密码是否匹配"""
    return pwd_context.verify(plain, hashed)


# ─── JWT 工具 ───────────────────────────────────────────────

def create_access_token(user_id: int) -> str:
    """为用户生成 JWT access token"""
    expire = datetime.now(timezone.utc) + timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES)
    payload = {
        "sub": str(user_id),
        "exp": expire,
    }
    return jwt.encode(payload, JWT_SECRET_KEY, algorithm=JWT_ALGORITHM)


def decode_access_token(token: str) -> int:
    """解码 JWT 并返回 user_id，失败则抛出异常"""
    try:
        payload = jwt.decode(token, JWT_SECRET_KEY, algorithms=[JWT_ALGORITHM])
        user_id = int(payload["sub"])
        return user_id
    except (JWTError, KeyError, ValueError):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="无效或已过期的 Token",
            headers={"WWW-Authenticate": "Bearer"},
        )


# ─── FastAPI 依赖注入 ──────────────────────────────────────

def get_current_user(
    authorization: str = Header(..., description="Bearer <token>"),
    db: Session = Depends(get_db),
) -> User:
    """从 Authorization header 中解析当前用户"""
    if not authorization.startswith("Bearer "):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Authorization header 格式应为: Bearer <token>",
        )
    token = authorization[len("Bearer "):]
    user_id = decode_access_token(token)

    user = db.query(User).filter(User.id == user_id).first()
    if user is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="用户不存在",
        )
    return user


def require_admin(admin_key: str = Header(..., alias="X-Admin-Key")) -> str:
    """验证管理员密钥（简单的 header 认证）"""
    if admin_key != ADMIN_KEY:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="管理员密钥无效",
        )
    return admin_key
