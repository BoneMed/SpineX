"""
SpineX CaaS 服务配置文件
"""
import os


# 数据库连接配置
DATABASE_URL = os.environ["DATABASE_URL"]

# JWT 配置
JWT_SECRET_KEY = os.environ["JWT_SECRET_KEY"]
JWT_ALGORITHM = "HS256"
# Token 有效期（分钟）
ACCESS_TOKEN_EXPIRE_MINUTES = 60 * 24  # 24小时

# 管理员密钥 - 用于管理端点的简单认证
ADMIN_KEY = os.environ["ADMIN_KEY"]

# 每个许可证允许绑定的最大设备数（admin 账号不受此限制，见 routers/license.py）
MAX_DEVICES_PER_LICENSE = 1

# CORS 允许的来源
CORS_ORIGINS = [
    "http://localhost",
    "http://localhost:3000",
    "http://localhost:8080",
    "*",
]
