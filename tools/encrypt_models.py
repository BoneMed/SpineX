"""
加密 nnUNet 模型文件 (.pth -> .pth.enc)
使用与 C++ 端相同的密钥
"""
import os
from cryptography.fernet import Fernet

KEY = os.environ["SPINEX_MODEL_KEY"].encode("ascii")

MODEL_DIR = r"H:\pythonproject\PHD_PROGRAM\SpineX-cpp\python\Model"
models = ["nnUNet_stage1.pth", "nnUNet_stage2.pth"]

f = Fernet(KEY)
for name in models:
    pth_path = os.path.join(MODEL_DIR, name)
    enc_path = pth_path + ".enc"
    if not os.path.exists(pth_path):
        print(f"  SKIP: {name} not found")
        continue
    print(f"  Encrypting {name} ...", end="", flush=True)
    with open(pth_path, "rb") as fp:
        data = fp.read()
    encrypted = f.encrypt(data)
    with open(enc_path, "wb") as fp:
        fp.write(encrypted)
    print(f" OK ({len(data)/1024/1024:.1f} MB -> {len(encrypted)/1024/1024:.1f} MB)")

print("\nDone.")
