import os
"""创建 admin 账号 + 永久 license"""
from datetime import datetime, timedelta, timezone
from database import SessionLocal
from models import User, License
from utils.security import hash_password
from utils.license_utils import generate_license_key

db = SessionLocal()

user = User(
    email=os.environ["SPINEX_ADMIN_EMAIL"],
    hashed_password=hash_password(os.environ["SPINEX_ADMIN_PASSWORD"]),
    name=os.environ.get("SPINEX_ADMIN_NAME", "Administrator"),
    is_admin=True,
    is_approved=True,
)
db.add(user)
db.commit()
print(f"Admin user created: id={user.id}, email={user.email}")

lic = License(
    user_id=user.id,
    license_key=generate_license_key(),
    tier="private",
    start_date=datetime.now(timezone.utc),
    end_date=datetime.now(timezone.utc) + timedelta(days=9999),
    is_active=True,
)
db.add(lic)
db.commit()
print(f"Permanent license: key={lic.license_key}, expires={lic.end_date}")
db.close()
