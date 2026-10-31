# SpineX 发布新版本完整流程

> 服务器自动更新系统已搭建完成。客户端启动 3 秒后自动检查 `https://www.spinex.cn/api/version`，
> 有新版本会弹窗提示用户下载安装。

---

## 一次完整发布的步骤

### 步骤 1：改代码 + 改版本号

改动代码后，**必须同步改两处版本号**（semver 格式 `x.y.z`）：

**a)** `src/app/UpdateChecker.cpp` 第 38 行左右：
```cpp
QString UpdateChecker::currentVersion()
{
    return QStringLiteral("1.0.2");   // ← 改这里
}
```

**b)** `installer/SpineX_setup.iss` 第 5 行：
```
#define AppVersion "1.0.2"
```

两处**必须一致**，否则客户端会反复提示自己是旧版。

### 步骤 2：重新编译 + 编译 .pyd（如果 Python 代码变了）

```bash
# 如果 Python 代码有改动
cd H:/pythonproject/PHD_PROGRAM/SpineX-cpp/python
C:/Users/Administrator/anaconda3/envs/BoneMed/python.exe compile_pyd.py --new-only

# C++ 编译
cd H:/pythonproject/PHD_PROGRAM/SpineX-cpp
cmake --build build --config Release
```

### 步骤 3：清理 build 目录里的废弃文件

```bash
# 复制新编译的 .pyd 到 build/Release/python/（如果编译过）
# 删除裸 .pth（只保留 .pth.enc）、_obsolete、.py 源码等
# 参考之前写过的清理步骤（在本文件最下方的"清理 build 目录"章节）
```

### 步骤 4：用 Inno Setup 打包

用 Inno Setup Compiler 打开 `installer/SpineX_setup.iss`，点击 "Compile"。
输出：`installer/output/SpineX_Setup_v1.0.2.exe`（~600-800 MB）

### 步骤 5：上传到服务器 + 发布（一键流程）

**⚠️ 背景：UK 直连中国大陆上行极慢（SCP/SFTP 实测 0.07 MB/s，615MB 要 6+ 小时；阿里云 OSS 传输加速也没用）。**
**解决方案：WeTransfer 中转 → 服务器 aria2c 16 路并发拉取（实测峰值 26 MiB/s，45 秒完成 615MB）。**
**整个过程已封装进管理后台，只需粘贴 WeTransfer 链接。**

#### ① 本地：上传到 WeTransfer

1. 打开 https://wetransfer.com（免费，2GB 以内）
2. "+ Add files" → 选 `installer/output/SpineX_Setup_vX.X.X.exe`
3. 右上角选 **"Create link"**（不填邮件）
4. Access control: **Public**
5. 点 "Get a link" → 上传（UK → AWS Ireland ~10+ MB/s，1-2 分钟完成）
6. 复制链接：`https://we.tl/t-XXXXXXXX`

#### ② 管理后台：一步发布

打开 `https://www.spinex.cn/spx-console-9f3a7b` → 输入 Admin Key 登录 → 
"应用版本管理" 卡片 → **[+ 发布新版本]** →

| 字段 | 填什么 |
|------|--------|
| 版本号 | `1.0.2` |
| 更新说明 | `- 修复 xxx`<br>`- 优化 yyy`（支持换行） |
| 强制更新 | 通常不勾选 |
| WeTransfer 下载链接 | 粘贴上一步的 `https://we.tl/t-XXXXXXXX` |

点 **"创建并拉取"**：
- 瞬间创建版本记录（is_active=false）
- 服务器后台开 aria2c 16 路并发下载
- 每 5 秒轮询一次，下载完成自动算 SHA256 + 激活
- 弹出 "版本 1.0.2 拉取完成，已激活"

后台返回的 API 路径是 `POST /api/admin/version/fetch`，内部流程:
1. 解析 `we.tl` 短链 → 真实 `wetransfer.com/downloads/{tid}/{shash}`
2. POST `/api/v4/transfers/{tid}/download` 拿 `direct_link`
3. `aria2c -x 16 -s 16 -k 4M` 并发下载到 `/opt/spinex-api/downloads/`
4. 算 SHA256 + 更新 DB + `is_active=true`

#### 故障回退：小文件直传

如果 < 100MB 的小文件，也可以直接用 `POST /api/admin/version` + `/api/admin/version/{id}/upload` 多部分上传（旧接口仍保留）。

### 步骤 6：验证

```bash
curl https://www.spinex.cn/api/version
```

应返回：
```json
{
  "latest": "1.0.2",
  "url": "https://www.spinex.cn/downloads/SpineX_Setup_v1.0.2.exe",
  "sha256": "...",
  "size": 644610628,
  "notes": "...",
  "mandatory": false
}
```

### 步骤 7：客户端自动收到

旧版本 (1.0.1) 用户下次启动 SpineX 会：
1. 3 秒后弹窗 "发现新版本 1.0.2"
2. 用户点"立即更新" → 下载 → SHA256 校验 → 启动 `Setup.exe /SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`
3. Inno Setup 通过 `AppMutex=SpineXSingleInstanceMutex` 自动关闭运行中的 SpineX
4. 覆盖安装 → 重启新版

---

## 架构关键点（别忘）

### 客户端侧

- **`src/app/UpdateChecker.h/cpp`** — 核心更新逻辑
- **`src/app/SpineXApp.cpp`** 构造函数最后 `QTimer::singleShot(3000, ...)` → 启动 3 秒后检查
- **`src/main.cpp`** — 创建 `CreateMutexA("SpineXSingleInstanceMutex")` 供安装器检测
- **`installer/SpineX_setup.iss`** — `AppMutex` + `CloseApplications=yes` + `RestartApplications=yes`

### 服务器侧

- **`server/models.py`** — `AppVersion` 表
- **`server/routers/version.py`** — 所有版本相关路由
  - `GET /api/version` — 公开，客户端用
  - `GET /downloads/{filename}` — 公开下载
  - `POST /api/admin/version` — 创建元数据
  - `POST /api/admin/version/{id}/upload` — 上传 exe（multipart，流式）
  - `GET /api/admin/versions` — 列表
  - `DELETE /api/admin/version/{id}` — 删除
  - `PATCH /api/admin/version/{id}/toggle` — 启用/停用
- **`server/routers/admin_ui.py`** — 管理后台「应用版本管理」卡片
- **服务器路径**：
  - 代码：`/opt/spinex-api/`
  - exe 存放：`/opt/spinex-api/downloads/`
  - 服务：`systemctl restart spinex-api`
  - DB：PostgreSQL `spinex_db`，用 `sudo -u postgres psql -d spinex_db`

### 关键环境变量

服务器 `DOWNLOAD_BASE_URL` 默认 `https://www.spinex.cn`，可通过 env 覆盖。

---

## 服务器部署快速脚本

```python
# deploy_server.py —— 本地运行，改完 server/ 代码后推送到生产
import paramiko
from scp import SCPClient

FILES = [
    'server/schemas.py',
    'server/models.py',
    'server/main.py',
    'server/routers/admin.py',
    'server/routers/admin_ui.py',
    'server/routers/auth.py',
    'server/routers/license.py',
    'server/routers/payment.py',
    'server/routers/payment_ui.py',
    'server/routers/version.py',
]

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('47.102.154.128', username='root', password='REDACTED_SET_IN_ENVIRONMENT')
REMOTE = '/opt/spinex-api'
with SCPClient(c.get_transport()) as scp:
    for f in FILES:
        scp.put(f'H:/pythonproject/PHD_PROGRAM/SpineX-cpp/{f}',
                f'{REMOTE}/{f.replace("server/","")}')

c.exec_command('systemctl restart spinex-api')
c.close()
```

---

## 清理 build 目录（每次打包前执行）

```bash
BUILD="H:/pythonproject/PHD_PROGRAM/SpineX-cpp/build/Release/python"

# 删除已废弃的模块
rm -rf "$BUILD/Lib/DLFE" "$BUILD/Lib/BCSlection" "$BUILD/Function/BC_Slection" "$BUILD/_obsolete"
rm -f "$BUILD/run_seg_daemon"* "$BUILD/compile_pyd"*

# 只保留加密模型 .pth.enc，删裸 .pth
rm -f "$BUILD/Model/Hybrid_DualEncoder.pth"
rm -f "$BUILD/Model/nnUNet_stage1.pth"
rm -f "$BUILD/Model/nnUNet_stage2.pth"
rm -f "$BUILD/Model/DL-FE.pth.enc"  # 旧模型

# 删掉所有 .py 源码（只保留 __init__.py）
find "$BUILD" -name "*.py" ! -name "__init__.py" ! -path "*/nnUNet/*" ! -path "*/Model/*" -delete
```

---

## 故障排查

### 客户端不弹更新提示

1. 检查 `https://www.spinex.cn/api/version` 是否可访问
2. 检查本地 `currentVersion()` 是否真的小于服务器版本
3. 查看启动日志 (`QDebug` 输出) — UpdateChecker 会 log 错误

### 上传 exe 超时

- HTTP 上传有 Nginx/FastAPI 超时限制（默认 60s）
- 大文件（>100MB）用 SCP 直传

### 安装器没自动关闭旧 SpineX

- 确认 `installer/SpineX_setup.iss` 有 `AppMutex=SpineXSingleInstanceMutex`
- 确认 `src/main.cpp` 有 `CreateMutexA(nullptr, FALSE, "SpineXSingleInstanceMutex")`
- 两边名字必须**完全一致**

### Inno Setup 不识别为升级（当成全新安装）

- `AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}` 必须**不变**
- 改了 AppId 就会被当成新产品，装到不同目录

---

## 服务器密码和地址

- 服务器 IP: `47.102.154.128`
- 用户: `root`
- 密码: `REDACTED_SET_IN_ENVIRONMENT`
- 代码目录: `/opt/spinex-api/`
- 下载目录: `/opt/spinex-api/downloads/`
- 服务: `spinex-api.service`
- 数据库: `spinex_db` (PostgreSQL)
- Admin Key: `REDACTED_SET_IN_ENVIRONMENT`
- 管理后台: `https://www.spinex.cn/spx-console-9f3a7b`
