"""
管理后台网页界面
访问 /admin 即可使用，需要输入 Admin Key 登录
"""
from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse

router = APIRouter(tags=["管理后台"])


@router.get("/spx-console-9f3a7b", response_class=HTMLResponse, include_in_schema=False)
def admin_panel(request: Request):
    """管理后台（隐藏路径，不在 API 文档中显示）"""
    return HTMLResponse(ADMIN_HTML)


ADMIN_HTML = """
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>柏恩美软件服务平台</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', sans-serif; background: #f4f6f8; color: #1a2332; }
.header { background: #2c5f6f; color: white; padding: 16px 32px; display: flex; align-items: center; justify-content: space-between; }
.header h1 { font-size: 22px; }
.header .logout { background: rgba(255,255,255,0.2); border: none; color: white; padding: 8px 16px; border-radius: 6px; cursor: pointer; font-size: 14px; }
.header .logout:hover { background: rgba(255,255,255,0.3); }
.container { max-width: 1000px; margin: 24px auto; padding: 0 20px; }

/* Login */
.login-box { max-width: 400px; margin: 100px auto; background: white; padding: 40px; border-radius: 12px; box-shadow: 0 2px 12px rgba(0,0,0,0.08); text-align: center; }
.login-box h2 { color: #2c5f6f; margin-bottom: 8px; }
.login-box p { color: #6b7280; margin-bottom: 24px; }
.login-box input { width: 100%; padding: 12px; border: 2px solid #dde1e7; border-radius: 8px; font-size: 14px; margin-bottom: 16px; }
.login-box input:focus { border-color: #2c5f6f; outline: none; }
.login-box button { width: 100%; padding: 12px; background: #2c5f6f; color: white; border: none; border-radius: 8px; font-size: 16px; font-weight: bold; cursor: pointer; }
.login-box button:hover { background: #1e4a57; }

/* Cards */
.card { background: white; border-radius: 10px; padding: 24px; margin-bottom: 20px; box-shadow: 0 1px 6px rgba(0,0,0,0.06); }
.card h3 { color: #2c5f6f; margin-bottom: 16px; font-size: 18px; }
.stats { display: flex; gap: 16px; margin-bottom: 20px; }
.stat { background: white; border-radius: 10px; padding: 20px; flex: 1; text-align: center; box-shadow: 0 1px 6px rgba(0,0,0,0.06); }
.stat .num { font-size: 32px; font-weight: bold; color: #2c5f6f; }
.stat .label { color: #6b7280; font-size: 13px; margin-top: 4px; }

/* Table */
table { width: 100%; border-collapse: collapse; font-size: 14px; }
th { text-align: left; padding: 10px 12px; background: #f0f4f8; color: #6b7280; font-weight: 600; }
td { padding: 10px 12px; border-top: 1px solid #eee; }
tr:hover td { background: #f9fafb; }

/* Buttons */
.btn { padding: 6px 14px; border: none; border-radius: 6px; cursor: pointer; font-size: 13px; font-weight: 600; }
.btn-approve { background: #10b981; color: white; }
.btn-approve:hover { background: #059669; }
.btn-revoke { background: #ef4444; color: white; }
.btn-revoke:hover { background: #dc2626; }
.btn-create { background: #2c5f6f; color: white; }
.btn-create:hover { background: #1e4a57; }
.btn-reject { background: #f59e0b; color: white; }
.btn-reject:hover { background: #d97706; }

/* Badge */
.badge { padding: 3px 10px; border-radius: 12px; font-size: 12px; font-weight: 600; }
.badge-active { background: #d1fae5; color: #065f46; }
.badge-inactive { background: #fee2e2; color: #991b1b; }
.badge-shared { background: #dbeafe; color: #1e40af; }
.badge-private { background: #fef3c7; color: #92400e; }
.badge-pending { background: #fef3c7; color: #92400e; }
.badge-approved { background: #d1fae5; color: #065f46; }

/* Modal */
.modal-bg { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.4); z-index: 100; }
.modal { position: fixed; top: 50%; left: 50%; transform: translate(-50%,-50%); background: white; padding: 32px; border-radius: 12px; z-index: 101; min-width: 380px; }
.modal h3 { margin-bottom: 16px; color: #2c5f6f; }
.modal label { display: block; margin-bottom: 4px; font-weight: 600; color: #374151; font-size: 14px; }
.modal input, .modal select { width: 100%; padding: 10px; border: 2px solid #dde1e7; border-radius: 8px; font-size: 14px; margin-bottom: 12px; }
.modal .btns { display: flex; gap: 10px; justify-content: flex-end; margin-top: 8px; }

.msg { padding: 10px 16px; border-radius: 8px; margin-bottom: 16px; font-size: 14px; display: none; }
.msg-ok { background: #d1fae5; color: #065f46; }
.msg-err { background: #fee2e2; color: #991b1b; }

#app { display: none; }
</style>
</head>
<body>

<!-- Login -->
<div id="loginBox" class="login-box">
  <h2>柏恩美软件服务平台</h2>
  <p>请输入管理密钥登录</p>
  <input type="password" id="keyInput" placeholder="管理密钥" onkeydown="if(event.key==='Enter')doLogin()">
  <button onclick="doLogin()">登录</button>
  <p id="loginErr" style="color:#ef4444;margin-top:12px;font-size:13px"></p>
</div>

<!-- App -->
<div id="app">
  <div class="header">
    <h1>SpineX 管理后台</h1>
    <button class="logout" onclick="doLogout()">退出</button>
  </div>

  <div class="container">
    <div id="msg" class="msg"></div>

    <!-- Stats -->
    <div class="stats">
      <div class="stat"><div class="num" id="statUsers">-</div><div class="label">用户总数</div></div>
      <div class="stat"><div class="num" id="statPending">-</div><div class="label">待审批</div></div>
      <div class="stat"><div class="num" id="statLicenses">-</div><div class="label">有效许可</div></div>
      <div class="stat"><div class="num" id="statRenewals">-</div><div class="label">订阅申请</div></div>
    </div>

    <!-- Renewal Requests -->
    <div class="card" id="renewalCard" style="display:none">
      <h3 style="color:#f59e0b">订阅 / 续费申请</h3>
      <table><thead><tr><th>姓名</th><th>邮箱</th><th>方案</th><th>付款</th><th>申请时间</th><th>操作</th></tr></thead>
      <tbody id="renewalBody"></tbody></table>
    </div>

    <!-- Pending -->
    <div class="card" id="pendingCard" style="display:none">
      <h3>待审批注册</h3>
      <table><thead><tr><th>姓名</th><th>邮箱</th><th>注册时间</th><th>操作</th></tr></thead>
      <tbody id="pendingBody"></tbody></table>
    </div>

    <!-- Users -->
    <div class="card">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;">
        <h3 style="margin:0;">用户与许可证</h3>
        <button class="btn btn-create" onclick="openCreateUserModal()">+ 创建账号</button>
      </div>
      <table><thead><tr><th>在线</th><th>姓名</th><th>邮箱</th><th>状态</th><th>许可证</th><th>方案</th><th>到期</th><th>设备</th><th>操作</th></tr></thead>
      <tbody id="usersBody"></tbody></table>
    </div>

    <!-- Versions -->
    <div class="card">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;">
        <h3 style="margin:0;">应用版本管理</h3>
        <button class="btn btn-create" onclick="openVersionModal()">+ 发布新版本</button>
      </div>
      <table><thead><tr><th>版本</th><th>状态</th><th>大小</th><th>强制</th><th>发布时间</th><th>更新说明</th><th>操作</th></tr></thead>
      <tbody id="versionsBody"></tbody></table>
    </div>
  </div>
</div>

<!-- Create License Modal -->
<div class="modal-bg" id="modalBg" onclick="closeModal()"></div>
<div class="modal" id="modal" style="display:none">
  <h3>创建许可证</h3>
  <label>用户邮箱</label>
  <input type="email" id="mEmail" readonly>
  <label>方案</label>
  <select id="mTier"><option value="shared">Community（数据共享优惠）</option><option value="private">Professional（数据私有）</option></select>
  <label>有效天数</label>
  <input type="number" id="mDays" value="365" min="1">
  <div class="btns">
    <button class="btn" style="background:#d1d5db;color:#374151" onclick="closeModal()">取消</button>
    <button class="btn btn-create" onclick="createLicense()">创建</button>
  </div>
</div>

<!-- Module Permissions Modal -->
<div class="modal" id="moduleModal" style="display:none">
  <h3>设置可用模块</h3>
  <div style="color:#6b7280;font-size:12px;margin-bottom:12px;">邮箱：<span id="modModalEmail"></span></div>
  <div style="display:flex;flex-direction:column;gap:10px;">
    <label style="display:flex;align-items:center;gap:8px;"><input type="checkbox" class="modBox" value="data" style="width:auto;"> Data（CT 加载/管理）</label>
    <label style="display:flex;align-items:center;gap:8px;"><input type="checkbox" class="modBox" value="segmentation" style="width:auto;"> CT Segmentation（分割）</label>
    <label style="display:flex;align-items:center;gap:8px;"><input type="checkbox" class="modBox" value="bmd" style="width:auto;"> BMD Analysis（骨密度/Meshing）</label>
    <label style="display:flex;align-items:center;gap:8px;"><input type="checkbox" class="modBox" value="strain" style="width:auto;"> Strain Prediction（应变预测）</label>
    <label style="display:flex;align-items:center;gap:8px;"><input type="checkbox" class="modBox" value="cobb" style="width:auto;"> Cobb Angle（Cobb 角）</label>
  </div>
  <div style="color:#9ca3af;font-size:11px;margin-top:12px;">不勾选任何项 = 允许所有模块（向后兼容）</div>
  <div class="btns">
    <button class="btn" style="background:#d1d5db;color:#374151" onclick="closeModuleModal()">取消</button>
    <button class="btn btn-create" onclick="saveModules()">保存</button>
  </div>
</div>

<!-- Create Version Modal -->
<div class="modal" id="versionModal" style="display:none">
  <h3>发布新版本</h3>
  <label>版本号 (x.y.z)</label>
  <input type="text" id="vVersion" placeholder="1.1.0">
  <label>更新说明 (支持多行)</label>
  <textarea id="vNotes" rows="5" style="width:100%;padding:8px;border:1px solid #dde1e7;border-radius:6px;font-family:inherit;" placeholder="- 修复 xxx&#10;- 优化 yyy"></textarea>
  <label style="display:flex;align-items:center;gap:8px;margin-top:8px;">
    <input type="checkbox" id="vMandatory" style="width:auto;">
    强制更新（用户必须升级才能继续使用）
  </label>
  <label style="margin-top:12px;">WeTransfer 下载链接</label>
  <input type="text" id="vUrl" placeholder="https://we.tl/t-XXXXXXXX">
  <div style="color:#6b7280;font-size:12px;margin-top:6px;line-height:1.5;">
    先在本地把 .exe 上传到 <a href="https://wetransfer.com" target="_blank" style="color:#2c5f6f;">wetransfer.com</a>
    (选 Create link / Public)，把链接粘贴过来。服务器会用 aria2c 16 路并发后台拉取（~45 秒/615MB）。
  </div>
  <div class="btns">
    <button class="btn" style="background:#d1d5db;color:#374151" onclick="closeVersionModal()">取消</button>
    <button class="btn btn-create" id="vCreateBtn" onclick="createVersion()">创建并拉取</button>
  </div>
</div>

<!-- Create User Modal -->
<div class="modal" id="userModal" style="display:none">
  <h3>手动创建账号</h3>
  <label>邮箱</label>
  <input type="email" id="uEmail" placeholder="user@example.com">
  <label>姓名</label>
  <input type="text" id="uName" placeholder="张三">
  <label>密码（至少 6 位）</label>
  <input type="text" id="uPassword" placeholder="初始密码">
  <label style="display:flex;align-items:center;gap:8px;margin-top:8px;">
    <input type="checkbox" id="uAutoApprove" checked style="width:auto;">
    自动审核通过（无需用户审核）
  </label>
  <div class="btns">
    <button class="btn" style="background:#d1d5db;color:#374151" onclick="closeUserModal()">取消</button>
    <button class="btn btn-create" onclick="createUser()">创建</button>
  </div>
</div>

<div style="text-align:center;color:#9ca3af;font-size:12px;padding:20px 0 40px;">
  © 2025-2026 上海柏恩美医疗科技有限公司 |
  <a href="https://beian.miit.gov.cn/" target="_blank" style="color:#9ca3af;">沪ICP备2026012808号-1</a>
</div>

<script>
const API = window.location.origin;
let KEY = '';

function doLogin() {
  KEY = document.getElementById('keyInput').value;
  fetch(API + '/api/admin/users/pending', {headers:{'X-Admin-Key':KEY}})
    .then(r => { if(!r.ok) throw 'fail'; return r.json(); })
    .then(() => { document.getElementById('loginBox').style.display='none'; document.getElementById('app').style.display='block'; loadAll(); setInterval(loadAll, 10000); })
    .catch(() => { document.getElementById('loginErr').textContent = '密钥错误'; });
}

function doLogout() { KEY=''; location.reload(); }

function showMsg(text, ok) {
  const m = document.getElementById('msg');
  m.className = 'msg ' + (ok ? 'msg-ok' : 'msg-err');
  m.textContent = text; m.style.display = 'block';
  setTimeout(() => m.style.display = 'none', 4000);
}

function loadAll() { loadPending(); loadRenewals(); loadUsers(); loadVersions(); }

function loadVersions() {
  fetch(API + '/api/admin/versions', {headers:{'X-Admin-Key':KEY}})
    .then(r => r.json())
    .then(list => {
      const body = document.getElementById('versionsBody');
      body.innerHTML = '';
      if (!list.length) {
        body.innerHTML = '<tr><td colspan="7" style="text-align:center;color:#9ca3af;padding:20px;">暂无版本</td></tr>';
        return;
      }
      list.forEach(v => {
        const statusBadge = v.is_active
          ? '<span class="badge badge-active">已发布</span>'
          : '<span class="badge badge-inactive">未发布</span>';
        const mandatoryBadge = v.mandatory
          ? '<span class="badge" style="background:#fee2e2;color:#dc2626">是</span>'
          : '<span style="color:#9ca3af">否</span>';
        const sizeStr = v.file_size ? (v.file_size / 1024 / 1024).toFixed(1) + ' MB' : '—';
        const notes = (v.notes || '').replace(/</g,'&lt;').replace(/\\n/g,'<br>');
        const created = new Date(v.created_at).toLocaleString();
        const toggleLabel = v.is_active ? '停用' : '启用';
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td><b>${v.version}</b></td>
          <td>${statusBadge}</td>
          <td>${sizeStr}</td>
          <td>${mandatoryBadge}</td>
          <td>${created}</td>
          <td style="max-width:300px;font-size:11px;">${notes}</td>
          <td style="white-space:nowrap">
            <button class="btn" style="padding:4px 8px;font-size:12px;background:#6366f1;color:white" onclick="toggleVersion(${v.id})">${toggleLabel}</button>
            <button class="btn btn-reject" style="padding:4px 8px;font-size:12px;margin-left:3px" onclick="deleteVersion(${v.id}, '${v.version}')">删除</button>
          </td>`;
        body.appendChild(tr);
      });
    });
}

function openVersionModal() {
  document.getElementById('vVersion').value = '';
  document.getElementById('vNotes').value = '';
  document.getElementById('vMandatory').checked = false;
  document.getElementById('vUrl').value = '';
  document.getElementById('vCreateBtn').disabled = false;
  document.getElementById('versionModal').style.display = 'block';
  document.getElementById('modalBg').style.display = 'block';
}

function closeVersionModal() {
  document.getElementById('versionModal').style.display = 'none';
  document.getElementById('modalBg').style.display = 'none';
}

function createVersion() {
  const version = document.getElementById('vVersion').value.trim();
  const notes = document.getElementById('vNotes').value;
  const mandatory = document.getElementById('vMandatory').checked;
  const url = document.getElementById('vUrl').value.trim();
  if (!version.match(/^\d+\.\d+\.\d+(\.\d+)?$/)) {
    showMsg('版本号格式错误（例如 1.1.0 或 1.0.1.1）', false);
    return;
  }
  if (!url.match(/^https:\/\/we\.tl\/t-/)) {
    showMsg('WeTransfer 链接格式不对（应以 https://we.tl/t- 开头）', false);
    return;
  }
  document.getElementById('vCreateBtn').disabled = true;
  fetch(API + '/api/admin/version/fetch', {
    method: 'POST',
    headers: {'Content-Type':'application/json', 'X-Admin-Key':KEY},
    body: JSON.stringify({version:version, notes:notes, mandatory:mandatory, wetransfer_url:url})
  })
  .then(r => r.json().then(d => ({status:r.status, data:d})))
  .then(({status, data}) => {
    if (status !== 200) throw (data.detail || 'create failed');
    showMsg('版本 ' + version + ' 已创建，后台拉取中（~45 秒后自动激活）', true);
    closeVersionModal();
    loadVersions();
    // Poll for activation
    const iv = setInterval(() => {
      fetch(API + '/api/admin/versions', {headers:{'X-Admin-Key':KEY}})
        .then(r => r.json()).then(list => {
          const v = list.find(x => x.id === data.id);
          if (v && v.is_active) {
            showMsg('版本 ' + version + ' 拉取完成，已激活', true);
            loadVersions();
            clearInterval(iv);
          }
        });
    }, 5000);
    setTimeout(() => clearInterval(iv), 20 * 60 * 1000);  // 20 min safeguard
  })
  .catch(e => {
    showMsg('创建失败: ' + e, false);
    document.getElementById('vCreateBtn').disabled = false;
  });
}

function toggleVersion(id) {
  fetch(API + '/api/admin/version/' + id + '/toggle', {method:'PATCH', headers:{'X-Admin-Key':KEY}})
    .then(r => r.json()).then(() => { showMsg('状态已切换', true); loadVersions(); });
}

function deleteVersion(id, version) {
  if (!confirm(`确认删除版本 ${version} ?（将删除安装文件）`)) return;
  fetch(API + '/api/admin/version/' + id, {method:'DELETE', headers:{'X-Admin-Key':KEY}})
    .then(r => r.json()).then(() => { showMsg('已删除', true); loadVersions(); });
}

function loadRenewals() {
  fetch(API + '/api/admin/license-requests', {headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(data => {
      document.getElementById('statRenewals').textContent = data.length;
      const card = document.getElementById('renewalCard');
      const body = document.getElementById('renewalBody');
      body.innerHTML = '';
      if (data.length === 0) { card.style.display = 'none'; return; }
      card.style.display = 'block';
      data.forEach(r => {
        const tierBadge = r.tier === 'professional'
          ? '<span class="badge badge-private">专业版</span>'
          : '<span class="badge badge-shared">社区版</span>';
        let payBadge = '<span class="badge badge-pending">未付款</span>';
        if (r.payment === 'paid') payBadge = '<span class="badge badge-approved">已付款</span>';
        else if (r.payment === 'waiting') payBadge = '<span class="badge badge-pending">待付款</span>';
        const tr = document.createElement('tr');
        let actionBtns = '';
        if (r.payment === 'paid') {
          actionBtns = '<span class="badge badge-approved">已自动创建</span>';
        } else if (r.payment === 'waiting') {
          actionBtns = `<button class="btn btn-approve" onclick="verifyPayment('${r.email}',${r.id})">核实收款</button>`;
        } else {
          actionBtns = '<span style="color:#9ca3af;font-size:12px">等待用户付款</span>';
        }
        actionBtns += ` <button class="btn btn-reject" onclick="dismissRequest(${r.id})" style="margin-left:6px">忽略</button>`;
        tr.innerHTML = `<td>${r.name}</td><td>${r.email}</td><td>${tierBadge}</td><td>${payBadge}</td><td>${new Date(r.created_at).toLocaleDateString()}</td>
          <td>${actionBtns}</td>`;
        body.appendChild(tr);
      });
    });
}

function verifyPayment(email, reqId) {
  showMsg('正在向支付宝查询付款状态...', true);
  fetch(API + '/api/payment/verify-payment?email=' + encodeURIComponent(email), {method:'POST', headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(d => {
      if (d.paid) {
        showMsg(d.message, true);
      } else {
        showMsg('支付宝确认：' + (d.message || '未收到付款'), false);
      }
      loadAll();
    })
    .catch(() => { showMsg('查询失败', false); });
}

function dismissRequest(id) {
  fetch(API + '/api/admin/license-request/dismiss?request_id=' + id, {method:'POST', headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(() => { loadAll(); });
}

function loadPending() {
  fetch(API + '/api/admin/users/pending', {headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(data => {
      document.getElementById('statPending').textContent = data.length;
      const card = document.getElementById('pendingCard');
      const body = document.getElementById('pendingBody');
      body.innerHTML = '';
      if (data.length === 0) { card.style.display = 'none'; return; }
      card.style.display = 'block';
      data.forEach(u => {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td>${u.name}</td><td>${u.email}</td><td>${new Date(u.created_at).toLocaleDateString()}</td>
          <td><button class="btn btn-approve" onclick="approveUser('${u.email}')">通过</button>
          <button class="btn btn-reject" onclick="rejectUser('${u.email}')" style="margin-left:6px">拒绝</button></td>`;
        body.appendChild(tr);
      });
    });
}

function loadUsers() {
  fetch(API + '/api/admin/users', {headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(data => {
      document.getElementById('statUsers').textContent = data.length;
      let totalLic = 0, totalDev = 0;
      const body = document.getElementById('usersBody');
      body.innerHTML = '';
      data.forEach(u => {
        // 取最新的 license（最后一个）
        const lics = u.licenses || [];
        const lic = lics.length > 0 ? lics[lics.length - 1] : null;
        if (lic && lic.is_active) totalLic++;
        lics.forEach(l => { totalDev += l.device_count || 0; });
        const statusBadge = u.is_approved
          ? '<span class="badge badge-approved">已通过</span>'
          : '<span class="badge badge-pending">待审批</span>';
        let licInfo = '-', tierBadge = '-', expires = '-', devices = '-', actions = '';
        if (lic) {
          licInfo = lic.is_active ? '<span class="badge badge-active">有效</span>' : '<span class="badge badge-inactive">已吊销</span>';
          tierBadge = lic.tier === 'shared' ? '<span class="badge badge-shared">社区版</span>' : '<span class="badge badge-private">专业版</span>';
          expires = new Date(lic.end_date).toLocaleDateString();
          devices = lic.device_count;
          if (!u.is_admin && lic.is_active) {
            actions = `<button class="btn btn-revoke" onclick="revokeLicense('${lic.license_key}')" style="padding:4px 8px;font-size:12px">吊销</button> `;
          }
        } else if (u.is_approved && !u.is_admin) {
          actions = `<button class="btn btn-create" onclick="openModal('${u.email}')" style="padding:4px 8px;font-size:12px">创建许可证</button> `;
        }
        if (!u.is_admin) {
          const modVal = (u.allowed_modules || '').replace(/'/g, '');
          actions += `<button class="btn" style="background:#6366f1;color:white;margin-left:3px;padding:4px 8px;font-size:12px" onclick="openModuleModal('${u.email}','${modVal}')">模块</button>`;
          actions += `<button class="btn btn-reject" onclick="deleteUser('${u.email}')" style="margin-left:3px;padding:4px 8px;font-size:12px">删除</button>`;
        }
        const tr = document.createElement('tr');
        const onlineBadge = u.is_online
          ? '<span style="display:inline-block;width:10px;height:10px;border-radius:50%;background:#10b981;box-shadow:0 0 6px #10b981;" title="在线"></span>'
          : '<span style="display:inline-block;width:10px;height:10px;border-radius:50%;background:#d1d5db;" title="离线"></span>';
        tr.innerHTML = `<td style="text-align:center">${onlineBadge}</td><td>${u.name}</td><td>${u.email}</td><td>${statusBadge}</td><td>${licInfo}</td><td>${tierBadge}</td><td>${expires}</td><td>${devices}</td><td style="white-space:nowrap">${actions}</td>`;
        body.appendChild(tr);
      });
      document.getElementById('statLicenses').textContent = totalLic;
    });
}

function approveUser(email) {
  fetch(API + '/api/admin/user/approve?email=' + encodeURIComponent(email), {method:'POST', headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(d => { showMsg(d.message || '已通过', true); loadAll(); });
}

function rejectUser(email) {
  if (!confirm('确认拒绝并删除该注册申请？')) return;
  fetch(API + '/api/admin/user/delete?email=' + encodeURIComponent(email), {method:'DELETE', headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(d => { showMsg(d.message || '已删除', true); loadAll(); })
    .catch(() => { showMsg('删除失败', false); });
}

function deleteUser(email) {
  if (!confirm('确认删除用户 "' + email + '" 及其所有许可证？此操作不可撤销。')) return;
  fetch(API + '/api/admin/user/delete?email=' + encodeURIComponent(email), {method:'DELETE', headers:{'X-Admin-Key':KEY}})
    .then(r=>r.json()).then(d => { showMsg(d.message || '已删除', true); loadAll(); })
    .catch(() => { showMsg('删除失败', false); });
}

function revokeLicense(key) {
  if (!confirm('确认吊销该许可证？')) return;
  fetch(API + '/api/admin/license/revoke', {method:'POST', headers:{'Content-Type':'application/json','X-Admin-Key':KEY}, body:JSON.stringify({license_key:key})})
    .then(r=>r.json()).then(d => { showMsg('许可证已吊销', true); loadAll(); });
}

function openModal(email) {
  document.getElementById('mEmail').value = email;
  document.getElementById('modal').style.display = 'block';
  document.getElementById('modalBg').style.display = 'block';
}

function closeModal() {
  document.getElementById('modal').style.display = 'none';
  document.getElementById('modalBg').style.display = 'none';
}

function createLicense() {
  const email = document.getElementById('mEmail').value;
  const tier = document.getElementById('mTier').value;
  const days = parseInt(document.getElementById('mDays').value);
  fetch(API + '/api/admin/license/create', {method:'POST', headers:{'Content-Type':'application/json','X-Admin-Key':KEY},
    body:JSON.stringify({user_email:email,tier:tier,days:days})})
    .then(r=>r.json()).then(d => {
      if (d.license_key) { showMsg('许可证已创建: ' + d.license_key, true); }
      else { showMsg(JSON.stringify(d.detail || d), false); }
      closeModal(); loadAll();
    });
}

function openModuleModal(email, modCsv) {
  document.getElementById('modModalEmail').textContent = email;
  const mods = (modCsv || '').split(',').map(s => s.trim()).filter(Boolean);
  document.querySelectorAll('.modBox').forEach(cb => { cb.checked = mods.includes(cb.value); });
  document.getElementById('moduleModal').style.display = 'block';
  document.getElementById('modalBg').style.display = 'block';
}

function closeModuleModal() {
  document.getElementById('moduleModal').style.display = 'none';
  document.getElementById('modalBg').style.display = 'none';
}

function saveModules() {
  const email = document.getElementById('modModalEmail').textContent;
  const mods = Array.from(document.querySelectorAll('.modBox:checked')).map(cb => cb.value);
  fetch(API + '/api/admin/user/modules', {method:'PUT', headers:{'Content-Type':'application/json','X-Admin-Key':KEY},
    body:JSON.stringify({email:email,modules:mods})})
    .then(r=>r.json()).then(d => {
      if (d.message) { showMsg(d.message, true); closeModuleModal(); loadAll(); }
      else { showMsg(JSON.stringify(d.detail || d), false); }
    });
}

function openCreateUserModal() {
  document.getElementById('uEmail').value = '';
  document.getElementById('uName').value = '';
  document.getElementById('uPassword').value = '';
  document.getElementById('uAutoApprove').checked = true;
  document.getElementById('userModal').style.display = 'block';
  document.getElementById('modalBg').style.display = 'block';
}

function closeUserModal() {
  document.getElementById('userModal').style.display = 'none';
  document.getElementById('modalBg').style.display = 'none';
}

function createUser() {
  const email = document.getElementById('uEmail').value.trim();
  const name = document.getElementById('uName').value.trim();
  const password = document.getElementById('uPassword').value;
  const autoApprove = document.getElementById('uAutoApprove').checked;
  if (!email || !name || !password) { showMsg('请填写完整', false); return; }
  if (password.length < 6) { showMsg('密码至少 6 位', false); return; }
  fetch(API + '/api/admin/user/create', {method:'POST', headers:{'Content-Type':'application/json','X-Admin-Key':KEY},
    body:JSON.stringify({email:email,name:name,password:password,auto_approve:autoApprove})})
    .then(r=>r.json()).then(d => {
      if (d.id) { showMsg('账号创建成功: ' + d.email, true); closeUserModal(); loadAll(); }
      else { showMsg(JSON.stringify(d.detail || d), false); }
    })
    .catch(e => { showMsg('创建失败: ' + e, false); });
}
</script>
</body>
</html>
"""
