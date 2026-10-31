#!/usr/bin/env python3
"""
Compile .py -> .pyd (Windows) / .so (Linux) using Cython for IP protection.
Run from the python/ directory with BoneMed conda env.

Usage: python compile_pyd.py [--all] [--new-only]
  --all       Compile ALL .py files (except nnUNet, __init__.py)
  --new-only  Compile only new/updated files that don't have .pyd yet (default)
"""

import os
import sys
import glob
import shutil
import argparse
from pathlib import Path

# Compiled extension suffix: Windows uses .pyd, Linux uses .so
EXT = ".pyd" if os.name == "nt" else ".so"

# Files to compile (relative to python/ dir)
NEW_FILES = [
    "Lib/Hybrid/net/HybridModel_Sparse.py",
    "Lib/Hybrid/net/SparsePhysics.py",
    "Lib/Hybrid/net/model_MSGC.py",
    "Lib/Hybrid/dataUtils/DataLoader.py",
    "Function/DL_FE/Inference_Hybrid.py",
    "spinex_ai/stress_inference.py",
    "run_stress.py",
]

# All compilable files (everything except __init__.py, nnUNet, and this script)
SKIP_PATTERNS = [
    "__init__.py",
    "compile_pyd.py",
    "nnUNet/",
    "Model/",
    ".pyd",
    ".so",
]

def should_skip(path):
    for pat in SKIP_PATTERNS:
        if pat in str(path):
            return True
    return False

def find_all_py(root):
    result = []
    for py in Path(root).rglob("*.py"):
        rel = str(py.relative_to(root)).replace("\\", "/")
        if not should_skip(rel):
            result.append(rel)
    return result

def compile_one(py_file, root):
    """Compile a single .py file to .pyd using Cython."""
    rel = py_file
    src = os.path.join(root, rel)
    if not os.path.exists(src):
        print(f"  SKIP (not found): {rel}")
        return False

    module_name = Path(rel).stem
    module_dir = os.path.dirname(src)

    print(f"  Compiling: {rel} ...", end=" ", flush=True)

    # Create temp setup.py for this file
    setup_code = f"""
from setuptools import setup, Extension
from Cython.Build import cythonize
import numpy
ext = Extension(
    "{module_name}",
    sources=["{os.path.basename(src)}"],
    include_dirs=[numpy.get_include()],
)
setup(ext_modules=cythonize(
    [ext],
    compiler_directives={{
        'language_level': '3',
        'annotation_typing': False,  # don't enforce dict/list/etc. annotations strictly
    }},
))
"""
    setup_path = os.path.join(module_dir, "_tmp_setup.py")
    with open(setup_path, "w") as f:
        f.write(setup_code)

    # Run cython build
    import subprocess
    result = subprocess.run(
        [sys.executable, "_tmp_setup.py", "build_ext", "--inplace"],
        cwd=module_dir,
        capture_output=True, text=True, timeout=120
    )

    # Cleanup temp files
    for tmp in glob.glob(os.path.join(module_dir, "_tmp_setup*")):
        os.remove(tmp)
    for tmp in glob.glob(os.path.join(module_dir, f"{module_name}.c")):
        os.remove(tmp)
    build_dir = os.path.join(module_dir, "build")
    if os.path.isdir(build_dir):
        shutil.rmtree(build_dir)

    # Check if the compiled extension was created (.pyd on Windows, .so on Linux)
    ext_files = glob.glob(os.path.join(module_dir, f"{module_name}*{EXT}"))
    if ext_files:
        print(f"OK → {os.path.basename(ext_files[0])}")
        return True
    else:
        print(f"FAILED")
        if result.stderr:
            # Print last few lines of error
            lines = result.stderr.strip().split("\n")
            for line in lines[-5:]:
                print(f"    {line}")
        return False

def encrypt_model(model_path, key=None):
    """Encrypt .pth → .pth.enc using Fernet."""
    from cryptography.fernet import Fernet

    if key is None:
        key = os.environ.get("SPINEX_MODEL_KEY")
    if not key:
        # Generate a new key
        key = Fernet.generate_key().decode()
        print(f"  Generated key: {key}")

    enc_path = model_path + ".enc"
    print(f"  Encrypting: {model_path} → {enc_path} ...", end=" ", flush=True)

    f = Fernet(key.encode() if isinstance(key, str) else key)
    with open(model_path, "rb") as fp:
        data = fp.read()
    encrypted = f.encrypt(data)
    with open(enc_path, "wb") as fp:
        fp.write(encrypted)

    orig_mb = len(data) / 1024 / 1024
    enc_mb = len(encrypted) / 1024 / 1024
    print(f"OK ({orig_mb:.1f}MB → {enc_mb:.1f}MB)")
    return key

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true", help="Compile all .py files")
    parser.add_argument("--new-only", action="store_true", default=True,
                        help="Compile only new files (default)")
    parser.add_argument("--encrypt", action="store_true", help="Also encrypt model files")
    parser.add_argument("--key", type=str, default=None, help="Encryption key")
    args = parser.parse_args()

    root = os.path.dirname(os.path.abspath(__file__))
    os.chdir(root)

    if args.all:
        files = find_all_py(root)
        print(f"Compiling ALL {len(files)} .py files...")
    else:
        files = NEW_FILES
        print(f"Compiling {len(files)} new files...")

    ok, fail = 0, 0
    for f in files:
        if compile_one(f, root):
            ok += 1
        else:
            fail += 1

    print(f"\nDone: {ok} compiled, {fail} failed")

    if args.encrypt:
        print("\n--- Model Encryption ---")
        hybrid_pth = os.path.join(root, "Model", "Hybrid_DualEncoder.pth")
        if os.path.exists(hybrid_pth):
            key = encrypt_model(hybrid_pth, args.key)
            print(f"\n  Key (save this!): {key}")
        else:
            print(f"  Model not found: {hybrid_pth}")

if __name__ == "__main__":
    main()
