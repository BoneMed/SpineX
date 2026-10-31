import os
"""
支付宝电脑网站支付接口
"""
import uuid
import time
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import HTMLResponse, RedirectResponse
from sqlalchemy.orm import Session
from pydantic import BaseModel

from database import get_db
from models import User, License, LicenseRequest
from utils.license_utils import generate_license_key

router = APIRouter(prefix="/api/payment", tags=["支付"])

# ---- 支付宝配置 ----
ALIPAY_APP_ID = "2021006140666268"
ALIPAY_PRIVATE_KEY = os.environ["ALIPAY_PRIVATE_KEY"].replace("\\n", "\n")

ALIPAY_PUBLIC_KEY = """-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAobbn5uX5yOSq6wGVIakeW5BR57SFbJGq5dmg2oXkCq0BDVnCGHS2hbHiuLkgyZQMgJYrAcq7YSzGBDsXvWTP9JCjEVOHT8x2EGEBXf+Fp1wH2cjww96afcoYwAnAi3Bsep5LbLfxJDsFRz9ig+N/DZQn8QbrkbZ9nmSFLtl/1/QcwxvWmHY/BW6LSudRA/DcVBVsnLgLlHQ7V4DWYoA2Hdk0ulzkeDgGYAJHW7LFN8RFgmNbC82HPOeWoEN5FVdlVK8Vfw+VfuYTXCzMKsYEx1zCpsWknyGZwfF6FVSKpYFOALdD30uqVtpVuoPImqiX3glq3cbPctzREDEUw+AHKQIDAQAB
-----END PUBLIC KEY-----"""

NOTIFY_URL = "https://www.spinex.cn/api/payment/notify"
RETURN_URL = "https://www.spinex.cn/api/payment/return"

# 价格配置 (单位: 元)
PRICES = {
    "community": {"monthly": 99, "yearly": 999},
    "professional": {"monthly": 299, "yearly": 2999},
}


def get_alipay_client():
    from alipay import AliPay
    return AliPay(
        appid=ALIPAY_APP_ID,
        app_notify_url=NOTIFY_URL,
        app_private_key_string=ALIPAY_PRIVATE_KEY,
        alipay_public_key_string=ALIPAY_PUBLIC_KEY,
        sign_type="RSA2",
    )


class CreateOrderRequest(BaseModel):
    email: str
    tier: str = "community"
    period: str = "yearly"  # monthly / yearly


@router.post("/create-order")
def create_order(body: CreateOrderRequest, db: Session = Depends(get_db)):
    """创建支付宝订单，返回支付页面 URL"""
    user = db.query(User).filter(User.email == body.email).first()
    if not user:
        raise HTTPException(status_code=404, detail="用户不存在")

    if body.tier not in PRICES:
        raise HTTPException(status_code=400, detail="无效的方案")

    price = PRICES[body.tier].get(body.period, PRICES[body.tier]["yearly"])
    tier_name = "社区版" if body.tier == "community" else "专业版"
    period_name = "月" if body.period == "monthly" else "年"

    # 生成唯一订单号
    out_trade_no = f"SPX{int(time.time())}{uuid.uuid4().hex[:8].upper()}"

    subject = f"SpineX {tier_name} - {period_name}度订阅"

    alipay = get_alipay_client()

    # 创建电脑网站支付订单
    order_string = alipay.api_alipay_trade_page_pay(
        out_trade_no=out_trade_no,
        total_amount=str(price),
        subject=subject,
        return_url=RETURN_URL,
        notify_url=NOTIFY_URL,
    )

    # 保存订单信息到数据库（用 LicenseRequest 表暂存）
    req = LicenseRequest(
        user_id=user.id,
        tier=body.tier,
        status=f"pending|{out_trade_no}|{body.period}",  # 暂存订单号和周期
    )
    db.add(req)
    db.commit()

    pay_url = f"https://openapi.alipay.com/gateway.do?{order_string}"

    return {"pay_url": pay_url, "order_no": out_trade_no}


@router.post("/notify")
async def alipay_notify(request: Request, db: Session = Depends(get_db)):
    """支付宝异步通知回调 — 支付成功后自动创建 License"""
    form_data = await request.form()
    data = dict(form_data)

    alipay = get_alipay_client()

    # 验签
    sign = data.pop("sign", None)
    sign_type = data.pop("sign_type", None)
    success = alipay.verify(data, sign)

    if not success:
        return "fail"

    trade_status = data.get("trade_status")
    out_trade_no = data.get("out_trade_no")

    if trade_status in ("TRADE_SUCCESS", "TRADE_FINISHED"):
        # 查找对应的订单
        reqs = db.query(LicenseRequest).filter(
            LicenseRequest.status.like(f"pending|{out_trade_no}|%")
        ).all()

        for req in reqs:
            parts = req.status.split("|")
            period = parts[2] if len(parts) > 2 else "yearly"
            tier = req.tier or "community"
            days = 30 if period == "monthly" else 365

            # 创建 License
            now = datetime.now(timezone.utc)
            lic = License(
                user_id=req.user_id,
                license_key=generate_license_key(),
                tier="shared" if tier == "community" else "private",
                start_date=now,
                end_date=now + timedelta(days=days),
                is_active=True,
            )
            db.add(lic)

            # 审批用户（如果还没审批）
            user = db.query(User).filter(User.id == req.user_id).first()
            if user and not user.is_approved:
                user.is_approved = True

            # 更新请求状态
            req.status = f"paid|{out_trade_no}"

            # 清除该用户所有其他未完成的申请
            old_reqs = db.query(LicenseRequest).filter(
                LicenseRequest.user_id == req.user_id,
                LicenseRequest.id != req.id,
                ~LicenseRequest.status.like("paid%"),
            ).all()
            for old in old_reqs:
                old.status = "dismissed"

            db.commit()

    return "success"


@router.get("/return", response_class=HTMLResponse)
def payment_return(request: Request):
    """支付完成后用户跳转回来的页面"""
    return """<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>支付完成</title>
<style>
body{font-family:'Microsoft YaHei',sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;background:#f4f6f8;margin:0}
.card{background:#fff;border-radius:16px;padding:48px;text-align:center;box-shadow:0 4px 20px rgba(0,0,0,0.08);max-width:400px}
.icon{font-size:64px;margin-bottom:16px}
h2{color:#2c5f6f;margin-bottom:8px}
p{color:#6b7280;margin-bottom:24px}
a{display:inline-block;padding:12px 32px;background:#2c5f6f;color:#fff;border-radius:8px;text-decoration:none;font-weight:bold}
a:hover{background:#1e4a57}
</style></head><body>
<div class="card">
<div class="icon">✅</div>
<h2>支付成功！</h2>
<p>您的 SpineX 许可证已自动激活。<br>请返回软件重新登录即可使用。</p>
<a href="https://www.spinex.cn">返回首页</a>
</div></body></html>"""


@router.post("/verify-payment")
def verify_payment(email: str, db: Session = Depends(get_db)):
    """
    主动向支付宝查询订单状态 — 只有支付宝确认收到钱才创建 License。
    管理员不能手动决定 tier，tier 由用户选择时锁定。
    """
    user = db.query(User).filter(User.email == email).first()
    if not user:
        raise HTTPException(status_code=404, detail="用户不存在")

    # 找到该用户最新的待付款订单
    req = db.query(LicenseRequest).filter(
        LicenseRequest.user_id == user.id,
        LicenseRequest.status.like("pending|%"),
    ).order_by(LicenseRequest.id.desc()).first()

    if not req:
        raise HTTPException(status_code=404, detail="没有找到待付款的订单")

    # 从 status 提取订单号和 period
    parts = req.status.split("|")
    out_trade_no = parts[1] if len(parts) > 1 else ""
    period = parts[2] if len(parts) > 2 else "yearly"

    if not out_trade_no:
        raise HTTPException(status_code=400, detail="订单号无效")

    # 主动查询支付宝订单状态（跳过返回验签）
    from alipay import AliPay
    alipay_no_verify = AliPay(
        appid=ALIPAY_APP_ID,
        app_notify_url=NOTIFY_URL,
        app_private_key_string=ALIPAY_PRIVATE_KEY,
        alipay_public_key_string=ALIPAY_PUBLIC_KEY,
        sign_type="RSA2",
    )
    try:
        result = alipay_no_verify.api_alipay_trade_query(out_trade_no=out_trade_no)
        trade_status = result.get("trade_status", "")
    except Exception as e:
        return {"message": f"查询失败: {str(e)}", "paid": False}

    if trade_status not in ("TRADE_SUCCESS", "TRADE_FINISHED"):
        return {"message": "支付宝确认：未收到付款", "trade_status": trade_status, "paid": False}

    # 支付宝确认收到钱了 — 自动创建 License
    tier = req.tier or "community"
    days = 30 if period == "monthly" else 365

    now = datetime.now(timezone.utc)
    lic = License(
        user_id=user.id,
        license_key=generate_license_key(),
        tier="shared" if tier == "community" else "private",
        start_date=now,
        end_date=now + timedelta(days=days),
        is_active=True,
    )
    db.add(lic)

    if not user.is_approved:
        user.is_approved = True

    req.status = f"paid|{out_trade_no}"

    # 清除该用户所有其他未完成的申请
    old_reqs = db.query(LicenseRequest).filter(
        LicenseRequest.user_id == user.id,
        LicenseRequest.id != req.id,
        ~LicenseRequest.status.like("paid%"),
    ).all()
    for old in old_reqs:
        old.status = "dismissed"

    db.commit()

    tier_name = "社区版" if tier == "community" else "专业版"
    return {"message": f"支付宝确认收款成功，{tier_name} License 已创建（{days}天）", "paid": True, "tier": tier}
