"""
许可证路由：验证许可证、查看许可证信息
"""
from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.orm import Session

from config import MAX_DEVICES_PER_LICENSE
from database import get_db
from models import Device, License, User
from schemas import LicenseInfo, LicenseVerifyRequest, LicenseVerifyResponse
from utils.security import get_current_user

router = APIRouter(prefix="/api/license", tags=["许可证"])


@router.post("/verify", response_model=LicenseVerifyResponse)
def verify_license(body: LicenseVerifyRequest, db: Session = Depends(get_db)):
    """
    客户端启动时调用：验证许可证有效性并记录设备信息。
    检查项：许可证是否存在、是否激活、是否过期、设备数量限制。
    """
    lic = db.query(License).filter(License.license_key == body.license_key).first()

    # 许可证不存在
    if lic is None:
        return LicenseVerifyResponse(valid=False, message="许可证密钥无效")

    # 许可证被吊销
    if not lic.is_active:
        return LicenseVerifyResponse(valid=False, message="许可证已被吊销")

    # 许可证已过期 — 统一转为 UTC 比较
    now = datetime.now(timezone.utc)
    end = lic.end_date if lic.end_date.tzinfo else lic.end_date.replace(tzinfo=timezone.utc)
    if now > end.astimezone(timezone.utc):
        return LicenseVerifyResponse(valid=False, message="许可证已过期")

    # 尚未到生效日期
    start = lic.start_date if lic.start_date.tzinfo else lic.start_date.replace(tzinfo=timezone.utc)
    if now < start.astimezone(timezone.utc):
        return LicenseVerifyResponse(valid=False, message="许可证尚未生效")

    # 加载用户，用于 admin 判断和返回 allowed_modules
    user = db.query(User).filter(User.id == lic.user_id).first()
    is_admin = bool(user and user.is_admin)

    # 设备数量检查 —— 如果当前设备已注册则更新 last_seen，否则检查是否超限
    device = (
        db.query(Device)
        .filter(
            Device.license_id == lic.id,
            Device.machine_fingerprint == body.machine_fingerprint,
        )
        .first()
    )

    if device:
        # 已知设备，更新最后访问时间
        device.last_seen = now
        db.commit()
    else:
        # 新设备：admin 不限制设备数，普通用户按 MAX_DEVICES_PER_LICENSE 限制
        if not is_admin:
            device_count = db.query(Device).filter(Device.license_id == lic.id).count()
            if device_count >= MAX_DEVICES_PER_LICENSE:
                return LicenseVerifyResponse(
                    valid=False,
                    message=f"设备数量已达上限（最多 {MAX_DEVICES_PER_LICENSE} 台）",
                )
        # 注册新设备
        new_device = Device(
            user_id=lic.user_id,
            license_id=lic.id,
            machine_fingerprint=body.machine_fingerprint,
            last_seen=now,
        )
        db.add(new_device)
        db.commit()

    allowed = user.allowed_modules if user else None

    return LicenseVerifyResponse(
        valid=True,
        tier=lic.tier,
        expires_at=lic.end_date,
        message="许可证验证通过",
        allowed_modules=allowed,
    )


@router.get("/info", response_model=list[LicenseInfo])
def get_license_info(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """获取当前用户的所有许可证信息"""
    licenses = db.query(License).filter(License.user_id == current_user.id).all()
    result = []
    for lic in licenses:
        device_count = db.query(Device).filter(Device.license_id == lic.id).count()
        result.append(
            LicenseInfo(
                id=lic.id,
                license_key=lic.license_key,
                tier=lic.tier,
                start_date=lic.start_date,
                end_date=lic.end_date,
                is_active=lic.is_active,
                device_count=device_count,
                created_at=lic.created_at,
            )
        )
    return result


@router.post("/request")
def request_license(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """已登录用户申请续费 License"""
    from models import LicenseRequest

    existing = db.query(LicenseRequest).filter(
        LicenseRequest.user_id == current_user.id,
        LicenseRequest.status == "pending"
    ).first()
    if existing:
        return {"message": "License request already submitted, pending approval"}

    req = LicenseRequest(user_id=current_user.id, status="pending")
    db.add(req)
    db.commit()
    return {"message": "License request submitted", "email": current_user.email}


from pydantic import BaseModel as PydanticBaseModel

class PlanRequest(PydanticBaseModel):
    email: str
    tier: str = "community"

@router.post("/request-plan")
def request_plan(body: PlanRequest, db: Session = Depends(get_db)):
    """注册后选择 Plan 并提交申请（不需要 token）"""
    from models import LicenseRequest

    user = db.query(User).filter(User.email == body.email).first()
    if user is None:
        raise HTTPException(status_code=404, detail="User not found")

    # 检查是否已有 pending 的申请
    existing = db.query(LicenseRequest).filter(
        LicenseRequest.user_id == user.id,
        LicenseRequest.status == "pending"
    ).first()
    if existing:
        # 更新 tier
        existing.tier = body.tier
        db.commit()
        return {"message": "Plan updated", "tier": body.tier}

    req = LicenseRequest(user_id=user.id, status="pending", tier=body.tier)
    db.add(req)
    db.commit()
    return {"message": "Plan request submitted", "email": body.email, "tier": body.tier}
