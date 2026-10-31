import os
#!/usr/bin/env python3
"""
SpineX License Key Generator (天精度)
======================================
Key 外观完全随机，到期日期隐藏编码其中。

格式: SPX-XXXX-XXXX-XXXX
  去掉 SPX 后 12 字符:
    [0:4]  随机填充
    [4:8]  到期日期（加密编码）
    [8:12] HMAC 校验码

用法:
  python generate_license.py                    # 1个，默认1年后到期
  python generate_license.py 5                  # 5个
  python generate_license.py 1 2027-06-30       # 指定到期日
  python generate_license.py 1 30d              # 30天后到期
  python generate_license.py 1 6m               # 6个月后到期
  python generate_license.py 1 1y               # 1年后到期
"""

import hashlib
import random
import string
import sys
from datetime import datetime, timedelta

# ===== 必须与 C++ 代码完全一致 =====
SECRET_SALT = os.environ["SPINEX_LICENSE_SALT"].encode("utf-8")
DATE_XOR_KEY = 0x9C3E
BASE36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def compute_hmac(data: bytes) -> bytes:
    msg = SECRET_SALT + data + SECRET_SALT
    return hashlib.sha256(msg).digest()


def encode_date(year: int, month: int, day: int) -> str:
    year_off = year - 2000
    packed = year_off * 416 + month * 32 + day
    packed ^= DATE_XOR_KEY
    result = ""
    for _ in range(4):
        result = BASE36[packed % 36] + result
        packed //= 36
    return result


def decode_date(encoded: str) -> tuple:
    packed = 0
    for ch in encoded:
        packed = packed * 36 + BASE36.index(ch)
    packed ^= DATE_XOR_KEY
    day = packed % 32;     packed //= 32
    month = packed % 13;   packed //= 13
    year = packed + 2000
    return year, month, day


def parse_expiry(spec: str) -> datetime:
    spec = spec.strip().lower()
    now = datetime.now()
    if spec.endswith('d'):
        return now + timedelta(days=int(spec[:-1]))
    elif spec.endswith('m'):
        months = int(spec[:-1])
        year = now.year + (now.month + months - 1) // 12
        month = (now.month + months - 1) % 12 + 1
        return datetime(year, month, min(now.day, 28))
    elif spec.endswith('y'):
        return datetime(now.year + int(spec[:-1]), now.month, min(now.day, 28))
    else:
        return datetime.strptime(spec, "%Y-%m-%d")


def generate_license_key(expiry_date: datetime) -> str:
    chars = string.ascii_uppercase + string.digits
    random_section = ''.join(random.choices(chars, k=4))
    date_section = encode_date(expiry_date.year, expiry_date.month, expiry_date.day)
    data_section = random_section + date_section
    hmac = compute_hmac(data_section.encode('utf-8'))
    check_section = hmac.hex()[:4].upper()
    body = data_section + check_section
    return f"SPX-{body[0:4]}-{body[4:8]}-{body[8:12]}"


def verify_key(key: str) -> tuple:
    clean = key.replace('-', '').replace(' ', '').upper()
    if not clean.startswith('SPX'):
        return False, None
    payload = clean[3:]
    if len(payload) < 12:
        return False, None
    data_section = payload[:8]
    check_section = payload[8:12]
    hmac = compute_hmac(data_section.encode('utf-8'))
    expected = hmac.hex()[:4].upper()
    if check_section != expected:
        return False, None
    year, month, day = decode_date(data_section[4:8])
    try:
        expiry = datetime(year, month, day)
        return True, expiry
    except ValueError:
        return False, None


if __name__ == '__main__':
    count = 1
    expiry_spec = "1y"
    if len(sys.argv) > 1:
        count = int(sys.argv[1])
    if len(sys.argv) > 2:
        expiry_spec = sys.argv[2]

    expiry = parse_expiry(expiry_spec)

    print("=" * 58)
    print("  SpineX License Key Generator")
    print(f"  Expiry: {expiry.strftime('%Y-%m-%d')}")
    print("=" * 58)
    for _ in range(count):
        key = generate_license_key(expiry)
        valid, decoded = verify_key(key)
        exp_str = decoded.strftime('%Y-%m-%d') if decoded else 'N/A'
        status = 'OK' if valid else 'FAIL'
        print(f"  {key}  [{status}]  expires: {exp_str}")
    print("=" * 58)
