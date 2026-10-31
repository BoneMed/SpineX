"""
管理员路由：创建/吊销许可证、查看用户列表
需要在请求头中携带 X-Admin-Key 进行身份验证
"""
from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.orm import Session

from database import get_db
from models import Device, License, LicenseRequest, User
from schemas import (
    AdminLicenseCreate,
    AdminLicenseRevoke,
    AdminUserCreate,
    AdminUserModules,
    LicenseDetail,
    UserProfile,
    UserWithLicenses,
)
from utils.security import hash_password
from utils.license_utils import generate_license_key
from utils.security import require_admin

router = APIRouter(prefix="/api/admin", include_in_schema=False)


@router.post(
    "/license/create",
    response_model=LicenseDetail,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(require_admin)],
)
def create_license(body: AdminLicenseCreate, db: Session = Depends(get_db)):
    """管理员为指定用户创建许可证（通过邮箱 + 天数）"""
    from datetime import datetime, timedelta, timezone

    # 通过邮箱找用户
    user = db.query(User).filter(User.email == body.user_email).first()
    if user is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="用户不存在",
        )

    now = datetime.now(timezone.utc)
    lic = License(
        user_id=user.id,
        license_key=generate_license_key(),
        tier=body.tier,
        start_date=now,
        end_date=now + timedelta(days=body.days),
        is_active=True,
    )
    db.add(lic)
    db.commit()
    db.refresh(lic)

    return LicenseDetail(
        id=lic.id,
        user_id=lic.user_id,
        license_key=lic.license_key,
        tier=lic.tier,
        start_date=lic.start_date,
        end_date=lic.end_date,
        is_active=lic.is_active,
        device_count=0,
        created_at=lic.created_at,
    )


@router.post(
    "/license/revoke",
    response_model=LicenseDetail,
    dependencies=[Depends(require_admin)],
)
def revoke_license(body: AdminLicenseRevoke, db: Session = Depends(get_db)):
    """管理员吊销指定许可证"""
    lic = db.query(License).filter(License.license_key == body.license_key).first()
    if lic is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="许可证不存在",
        )

    if not lic.is_active:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="许可证已处于吊销状态",
        )

    # Admin license 不可吊销
    user = db.query(User).filter(User.id == lic.user_id).first()
    if user and user.is_admin:
        raise HTTPException(status_code=403, detail="Admin 许可证不可吊销")

    lic.is_active = False
    db.commit()
    db.refresh(lic)

    device_count = db.query(Device).filter(Device.license_id == lic.id).count()
    return LicenseDetail(
        id=lic.id,
        user_id=lic.user_id,
        license_key=lic.license_key,
        tier=lic.tier,
        start_date=lic.start_date,
        end_date=lic.end_date,
        is_active=lic.is_active,
        device_count=device_count,
        created_at=lic.created_at,
    )


@router.get(
    "/users",
    response_model=list[UserWithLicenses],
    dependencies=[Depends(require_admin)],
)
def list_users(db: Session = Depends(get_db)):
    """管理员查看所有用户及其许可证信息"""
    from datetime import datetime, timedelta, timezone
    now = datetime.now(timezone.utc)
    online_threshold = now - timedelta(minutes=3)  # online if last_seen within 3 min

    users = db.query(User).order_by(User.id).all()
    result = []
    for user in users:
        licenses_data = []
        for lic in user.licenses:
            device_count = db.query(Device).filter(Device.license_id == lic.id).count()
            licenses_data.append(
                LicenseDetail(
                    id=lic.id,
                    user_id=lic.user_id,
                    license_key=lic.license_key,
                    tier=lic.tier,
                    start_date=lic.start_date,
                    end_date=lic.end_date,
                    is_active=lic.is_active,
                    device_count=device_count,
                    created_at=lic.created_at,
                )
            )

        # Online status: any device with last_seen within 3 min
        last_seen = (
            db.query(Device.last_seen)
            .filter(Device.user_id == user.id)
            .order_by(Device.last_seen.desc())
            .first()
        )
        last_seen_dt = last_seen[0] if last_seen else None
        is_online = False
        if last_seen_dt:
            ls = last_seen_dt if last_seen_dt.tzinfo else last_seen_dt.replace(tzinfo=timezone.utc)
            is_online = ls >= online_threshold

        result.append(
            UserWithLicenses(
                id=user.id,
                email=user.email,
                name=user.name,
                is_admin=user.is_admin,
                is_approved=user.is_approved,
                created_at=user.created_at,
                licenses=licenses_data,
                allowed_modules=user.allowed_modules,
                is_online=is_online,
                last_seen=last_seen_dt,
            )
        )
    return result


@router.post(
    "/user/create",
    response_model=UserProfile,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(require_admin)],
)
def create_user(body: AdminUserCreate, db: Session = Depends(get_db)):
    """管理员手动创建账号（默认已审核通过）"""
    existing = db.query(User).filter(User.email == body.email).first()
    if existing is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"邮箱 {body.email} 已被注册",
        )

    user = User(
        email=body.email,
        hashed_password=hash_password(body.password),
        name=body.name,
        is_admin=False,
        is_approved=body.auto_approve,
    )
    db.add(user)
    db.commit()
    db.refresh(user)
    return user


VALID_MODULES = {"data", "segmentation", "bmd", "strain", "cobb"}


@router.put(
    "/user/modules",
    dependencies=[Depends(require_admin)],
)
def set_user_modules(body: AdminUserModules, db: Session = Depends(get_db)):
    """设置用户可用的 module 列表"""
    user = db.query(User).filter(User.email == body.email).first()
    if user is None:
        raise HTTPException(status_code=404, detail="用户不存在")

    # Validate module keys
    invalid = [m for m in body.modules if m not in VALID_MODULES]
    if invalid:
        raise HTTPException(
            status_code=400,
            detail=f"无效的 module: {invalid}. 有效值: {sorted(VALID_MODULES)}",
        )

    user.allowed_modules = ",".join(body.modules) if body.modules else None
    db.commit()
    return {
        "email": user.email,
        "allowed_modules": user.allowed_modules or "",
        "message": f"已更新 {len(body.modules)} 个模块权限",
    }


@router.post(
    "/user/approve",
    dependencies=[Depends(require_admin)],
)
def approve_user(email: str, db: Session = Depends(get_db)):
    """管理员审批用户（通过邮箱）"""
    user = db.query(User).filter(User.email == email).first()
    if user is None:
        raise HTTPException(status_code=404, detail="用户不存在")
    if user.is_approved:
        return {"message": f"用户 {email} 已经是审核通过状态"}
    user.is_approved = True
    db.commit()
    return {"message": f"用户 {email} 审核通过"}


@router.get(
    "/users/pending",
    dependencies=[Depends(require_admin)],
)
def list_pending_users(db: Session = Depends(get_db)):
    """查看待审核用户列表"""
    users = db.query(User).filter(User.is_approved == False).all()
    return [{"id": u.id, "email": u.email, "name": u.name, "created_at": str(u.created_at)} for u in users]


@router.delete(
    "/user/delete",
    dependencies=[Depends(require_admin)],
)
def delete_user(email: str, db: Session = Depends(get_db)):
    """管理员删除用户及其所有 license 和设备记录（admin 账号不可删除）"""
    user = db.query(User).filter(User.email == email).first()
    if user is None:
        raise HTTPException(status_code=404, detail="用户不存在")
    if user.is_admin:
        raise HTTPException(status_code=403, detail="Admin 账号不可删除")
    db.delete(user)
    db.commit()
    return {"message": f"用户 {email} 已删除"}


@router.get(
    "/license-requests",
    dependencies=[Depends(require_admin)],
)
def list_license_requests(db: Session = Depends(get_db)):
    """查看所有待处理的续费申请"""
    reqs = db.query(LicenseRequest).filter(
        ~LicenseRequest.status.like("approved%"),
        ~LicenseRequest.status.like("dismissed%"),
        ~LicenseRequest.status.like("paid%"),
    ).all()
    result = []
    for r in reqs:
        user = db.query(User).filter(User.id == r.user_id).first()
        status_str = r.status or "pending"
        if status_str.startswith("paid"):
            payment = "paid"
        elif status_str.startswith("pending|"):
            payment = "waiting"
        else:
            payment = "unpaid"
        result.append({
            "id": r.id,
            "email": user.email if user else "unknown",
            "name": user.name if user else "unknown",
            "tier": getattr(r, 'tier', 'community') or 'community',
            "payment": payment,
            "created_at": str(r.created_at),
        })
    return result


@router.post(
    "/license-request/dismiss",
    dependencies=[Depends(require_admin)],
)
def dismiss_license_request(request_id: int, db: Session = Depends(get_db)):
    """标记续费申请为已处理"""
    req = db.query(LicenseRequest).filter(LicenseRequest.id == request_id).first()
    if req is None:
        raise HTTPException(status_code=404, detail="申请不存在")
    req.status = "approved"
    db.commit()
    return {"message": "申请已处理"}
