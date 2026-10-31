"""SpineX API integration test"""
import requests
import json

BASE = "http://localhost:8000"
ADMIN_KEY = "REDACTED_SET_IN_ENVIRONMENT"

# 1. Register
print("=== 1. Register ===")
r = requests.post(f"{BASE}/api/auth/register", json={
    "email": "ryan@bonemed.com",
    "password": "test123456",
    "name": "Ryan Cai"
})
print(json.dumps(r.json(), indent=2))

# 2. Login
print("\n=== 2. Login ===")
r = requests.post(f"{BASE}/api/auth/login", json={
    "email": "ryan@bonemed.com",
    "password": "test123456"
})
token = r.json()["access_token"]
print(f"Token: {token[:30]}...")
headers = {"Authorization": f"Bearer {token}"}

# 3. Me
print("\n=== 3. Me ===")
r = requests.get(f"{BASE}/api/auth/me", headers=headers)
print(json.dumps(r.json(), indent=2))

# 4. Admin create shared license
print("\n=== 4. Create shared license (30 days) ===")
r = requests.post(f"{BASE}/api/admin/license/create",
    headers={"X-Admin-Key": ADMIN_KEY},
    json={"user_email": "ryan@bonemed.com", "tier": "shared", "days": 30})
print(json.dumps(r.json(), indent=2))
license_key = r.json()["license_key"]

# 5. Verify license
print(f"\n=== 5. Verify license: {license_key} ===")
r = requests.post(f"{BASE}/api/license/verify", json={
    "license_key": license_key,
    "machine_fingerprint": "test-machine-001"
})
print(json.dumps(r.json(), indent=2))

# 6. Admin create private license
print("\n=== 6. Create private license (365 days) ===")
r = requests.post(f"{BASE}/api/admin/license/create",
    headers={"X-Admin-Key": ADMIN_KEY},
    json={"user_email": "ryan@bonemed.com", "tier": "private", "days": 365})
print(json.dumps(r.json(), indent=2))

# 7. List licenses
print("\n=== 7. My licenses ===")
r = requests.get(f"{BASE}/api/license/info", headers=headers)
print(json.dumps(r.json(), indent=2))

print("\n=== ALL TESTS PASSED ===")
