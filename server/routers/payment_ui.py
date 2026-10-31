"""
支付页面 - 用户选完 Plan 后跳转到浏览器完成支付
"""
from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse

router = APIRouter(tags=["支付"])


@router.get("/pay", response_class=HTMLResponse)
def payment_page(email: str = "", tier: str = "community"):
    """支付页面"""
    tier_name = "专业版 (Professional)" if tier == "professional" else "社区版 (Community)"
    price = "¥299/月" if tier == "professional" else "¥99/月"
    price_num = "299" if tier == "professional" else "99"

    return HTMLResponse(PAYMENT_HTML.replace("{{EMAIL}}", email)
                        .replace("{{TIER}}", tier)
                        .replace("{{TIER_NAME}}", tier_name)
                        .replace("{{PRICE}}", price)
                        .replace("{{PRICE_NUM}}", price_num))


PAYMENT_HTML = """
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SpineX - 订阅付款</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif; background: #f4f6f8; color: #1a2332; }
.header { background: #2c5f6f; color: white; padding: 20px 0; text-align: center; }
.header h1 { font-size: 28px; letter-spacing: 2px; }
.header p { color: rgba(255,255,255,0.7); margin-top: 4px; }
.container { max-width: 500px; margin: 40px auto; padding: 0 20px; }
.card { background: white; border-radius: 16px; padding: 32px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); }
.card h2 { color: #2c5f6f; font-size: 20px; margin-bottom: 20px; text-align: center; }
.order-info { background: #f0f4f8; border-radius: 10px; padding: 20px; margin-bottom: 24px; }
.order-row { display: flex; justify-content: space-between; padding: 8px 0; font-size: 15px; }
.order-row.total { border-top: 2px solid #dde1e7; margin-top: 8px; padding-top: 16px; font-size: 20px; font-weight: bold; color: #2c5f6f; }
.pay-btn { display: block; width: 100%; padding: 16px; background: #1677FF; color: white; border: none; border-radius: 10px; font-size: 18px; font-weight: bold; cursor: pointer; text-align: center; text-decoration: none; }
.pay-btn:hover { background: #0958d9; }
.pay-btn img { height: 24px; vertical-align: middle; margin-right: 8px; }
.secure { text-align: center; margin-top: 16px; color: #9ca3af; font-size: 12px; }
.footer { text-align: center; margin-top: 40px; color: #9ca3af; font-size: 12px; padding-bottom: 40px; }
</style>
</head>
<body>
<div class="header">
    <h1>SpineX</h1>
    <p>脊柱影像智能分析平台</p>
</div>
<div class="container">
    <div class="card">
        <h2>订阅确认</h2>
        <div class="order-info">
            <div class="order-row"><span>方案</span><span>{{TIER_NAME}}</span></div>
            <div class="order-row"><span>账户</span><span>{{EMAIL}}</span></div>
            <div class="order-row"><span>订阅周期</span><span>1 年</span></div>
            <div class="order-row total"><span>合计</span><span>{{PRICE}}</span></div>
        </div>
        <a class="pay-btn" id="payBtn" href="javascript:void(0)" onclick="handlePay()">
            支付宝支付 {{PRICE}}
        </a>
        <p class="secure">🔒 由支付宝安全加密处理</p>
    </div>
</div>
<div class="footer">
    © 2025-2026 上海柏恩美医疗科技有限公司 | 仅供科研使用<br>
    <a href="https://beian.miit.gov.cn/" target="_blank" style="color:#9ca3af;">沪ICP备2026012808号-1</a>
</div>
<script>
function handlePay() {
    var btn = document.getElementById('payBtn');
    btn.textContent = '正在创建订单...';
    btn.style.pointerEvents = 'none';
    fetch('/api/payment/create-order', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({email: '{{EMAIL}}', tier: '{{TIER}}', period: 'yearly'})
    })
    .then(r => r.json())
    .then(data => {
        if (data.pay_url) {
            window.location.href = data.pay_url;
        } else {
            alert('创建订单失败: ' + JSON.stringify(data));
            btn.textContent = '支付宝支付 {{PRICE}}';
            btn.style.pointerEvents = 'auto';
        }
    })
    .catch(err => {
        alert('网络错误，请重试');
        btn.textContent = '支付宝支付 {{PRICE}}';
        btn.style.pointerEvents = 'auto';
    });
}
</script>
</body>
</html>
"""
