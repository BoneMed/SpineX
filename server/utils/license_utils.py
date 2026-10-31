"""
许可证工具函数
"""
import uuid


def generate_license_key() -> str:
    """生成 UUID4 格式的许可证密钥"""
    return str(uuid.uuid4())
