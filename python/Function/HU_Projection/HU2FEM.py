#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HU2E投影模块 (Phantomless版本)
CT + mask → (重采样 mask) → Phantomless HU2E → IDW 插值 → 稠密 E_field

改进版：使用GMM phantomless校准方法进行HU到E的映射

主要改动：
- 移除皮质骨/松质骨分割
- 使用GMM自动校准 HU → 密度
- 统一公式：E = 4730 * rho^1.56
"""

import os
import numpy as np
import nibabel as nib
import pandas as pd
import meshio
from scipy.spatial import cKDTree
from scipy.ndimage import map_coordinates
# GaussianMixture no longer needed — using 3-point anatomical calibration
import matplotlib.pyplot as plt

# 导入重采样模块
from Lib.Hu2E.Resample import resample_mask_to_034mm


# ==========================================
# Phantomless 校准函数 (带可视化)
# ==========================================
def _robust_mean(x, trim=0.1):
    """Trimmed mean for robust tissue HU estimation."""
    x = np.asarray(x, dtype=np.float32).ravel()
    n = x.size
    if n == 0:
        return np.nan
    if n < 50:
        return float(np.median(x))
    lo, hi = int(trim * n), int((1 - trim) * n)
    if hi <= lo:
        return float(np.median(x))
    x_part = np.partition(x, (lo, hi - 1))
    return float(np.mean(x_part[lo:hi]))


def _get_largest_component(mask):
    """Return the largest connected component of a binary mask."""
    from scipy.ndimage import label as ndlabel
    labeled, num = ndlabel(mask)
    if num <= 1:
        return mask
    counts = np.bincount(labeled.ravel())
    counts[0] = 0
    return labeled == np.argmax(counts)


def phantomless_calib(ct_data, n_samples=800000, plot=False, save_path=None, ct_name="CT",
                      mask_data=None, spacing=None):
    """
    3-point anatomical phantomless calibration with vertebra-guided tissue targeting.

    Uses anatomical landmarks to precisely identify:
      - Air:    outside body (HU < -800)
      - Fat:    subcutaneous shell region (-200 < HU < -50)
      - Muscle: erector spinae posterior to vertebra (0 < HU < 100, 10-50mm from centroid)

    Fits weighted linear correction: HU_std = slope * HU_raw + intercept
    so that air→-1000, fat→-100, muscle→40.

    Parameters:
        ct_data:   3D CT array
        mask_data: 3D segmentation mask (optional, enables anatomical targeting)
        spacing:   voxel spacing in mm (optional, needed for erector spinae targeting)

    Returns:
        slope, intercept: HU correction parameters
    """
    targets = {"air": -1000.0, "fat": -100.0, "muscle": 40.0}
    weights = {"air": 3.0, "fat": 1.0, "muscle": 1.5}

    refs = {}

    # ---- Anatomical targeting (when mask available) ----
    if mask_data is not None and spacing is not None:
        # Find best vertebra for calibration ROI targeting
        # Priority: L1 (label=20) → closest to L1 → largest vertebra
        labels_present = np.unique(mask_data)
        labels_present = labels_present[labels_present > 0]

        if len(labels_present) > 0:
            L1_LABEL = 20  # L1 in standard nnU-Net vertebra labeling
            if L1_LABEL in labels_present:
                best_label = L1_LABEL
            else:
                # Find closest to L1 (labels 14-26 are T12-S1, 20=L1)
                best_label = min(labels_present, key=lambda l: abs(l - L1_LABEL))
            print(f"[Calib] Using label {best_label} as calibration reference"
                  f"{' (L1)' if best_label == L1_LABEL else ' (closest to L1)'}")
            vert_mask = (mask_data == best_label)
            coords = np.argwhere(vert_mask)

            if coords.shape[0] > 0:
                # Define ROI: +-20 slices around vertebra centroid
                z_center = int(np.mean(coords[:, 2]))
                half_width = 20
                z_min = max(0, z_center - half_width)
                z_max = min(ct_data.shape[2] - 1, z_center + half_width)

                roi = ct_data[:, :, z_min:z_max+1].copy()
                vert_roi = (mask_data[:, :, z_min:z_max+1] == best_label)

                # Body mask
                body = roi > -500
                body = binary_closing(body, structure=generate_binary_structure(3, 1))
                body = _get_largest_component(body)

                shell = body & (~binary_erosion(body, iterations=8))
                deep = binary_erosion(body, iterations=5)

                # Vertebra centroid for erector spinae targeting
                vert_coords = np.argwhere(vert_roi)
                vcx = int(np.mean(vert_coords[:, 0]))
                vcy = int(np.mean(vert_coords[:, 1]))

                # Distance from vertebra centroid in mm
                yy, xx = np.meshgrid(np.arange(roi.shape[1]), np.arange(roi.shape[0]))
                dist_mm = np.sqrt(((xx - vcx) * spacing[0])**2 + ((yy - vcy) * spacing[1])**2)
                dist_3d = np.broadcast_to(dist_mm[:, :, np.newaxis], roi.shape)

                # Erector spinae zone: posterior to vertebra, 10-50mm, lateral < 40mm
                posterior_3d = np.broadcast_to((yy < vcy - 5)[:, :, np.newaxis], roi.shape)
                lateral_mm = np.abs(xx - vcx) * spacing[0]
                lateral_3d = np.broadcast_to((lateral_mm < 40)[:, :, np.newaxis], roi.shape)
                erector_zone = (dist_3d > 10) & (dist_3d < 50) & posterior_3d & lateral_3d

                # Air: outside body
                air_m = (roi < -800) & (~body)
                if air_m.sum() > 500:
                    refs["air"] = _robust_mean(roi[air_m])

                # Fat: subcutaneous shell, exclude bone
                fat_m = (-200 < roi) & (roi < -50) & shell & (~vert_roi)
                from scipy.ndimage import binary_opening
                fat_m = binary_opening(fat_m)
                if fat_m.sum() > 500:
                    refs["fat"] = _robust_mean(roi[fat_m])

                # Muscle: erector spinae (posterior to vertebra)
                mus_m = (0 < roi) & (roi < 100) & deep & (~vert_roi) & erector_zone
                if mus_m.sum() > 500:
                    refs["muscle"] = _robust_mean(roi[mus_m])

                print(f"[Calib] Anatomical targeting: label={best_label}, "
                      f"z={z_center}±{half_width}")

    # ---- Fallback: simple HU range (no mask) ----
    if len(refs) < 2:
        print("[Calib] Anatomical targeting insufficient, using HU-range fallback")
        flat = ct_data.flatten()
        if "air" not in refs:
            air_vals = flat[flat < -800]
            if air_vals.size > 500:
                refs["air"] = _robust_mean(air_vals)
        if "fat" not in refs:
            fat_vals = flat[(flat > -200) & (flat < -50)]
            if fat_vals.size > 500:
                refs["fat"] = _robust_mean(fat_vals)
        if "muscle" not in refs:
            muscle_vals = flat[(flat > 0) & (flat < 100)]
            if muscle_vals.size > 500:
                refs["muscle"] = _robust_mean(muscle_vals)

    print(f"[Calib] Detected refs: { {k: f'{v:.1f}' for k, v in refs.items()} }")

    # ---- Weighted linear fit: HU_std = a * HU_raw + b ----
    xs, ys, ws = [], [], []
    for k in ("air", "fat", "muscle"):
        if k in refs and k in targets:
            xs.append(refs[k])
            ys.append(targets[k])
            ws.append(weights[k])

    if len(xs) < 2:
        print("[WARN] < 2 reference tissues found, using identity calibration")
        return 1.0, 0.0

    x = np.asarray(xs, dtype=np.float64)
    y = np.asarray(ys, dtype=np.float64)
    w = np.asarray(ws, dtype=np.float64)

    A = np.column_stack([x, np.ones_like(x)])
    Aw = A * np.sqrt(w[:, None])
    yw = y * np.sqrt(w)
    coef, *_ = np.linalg.lstsq(Aw, yw, rcond=None)
    slope, intercept = float(coef[0]), float(coef[1])

    if not np.isfinite(slope) or not np.isfinite(intercept) or slope <= 0:
        print("[WARN] Invalid calibration, using identity")
        return 1.0, 0.0

    print(f"[Calib] HU_std = {slope:.4f} * HU_raw + {intercept:.2f}")

    return slope, intercept


def plot_calibration(valid_data, gmm, means, stds, weights, slope, intercept,
                     save_path=None, ct_name="CT"):
    """
    绘制校准可视化图 (显示空气、脂肪、肌肉三峰)

    参数:
        valid_data: 有效的HU值数组 (应包含-1100~200范围)
        gmm: 训练好的GMM模型
        means: 排序后的GMM均值 [air, fat, muscle]
        stds: 排序后的GMM标准差
        weights: 排序后的GMM权重
        slope, intercept: 校准参数
        save_path: 保存路径
        ct_name: CT文件名
    """
    fig, axes = plt.subplots(1, 2, figsize=(16, 6))

    # ===== 左图: 三峰直方图分析 =====
    ax1 = axes[0]

    # 扩展范围以显示空气峰
    hist_range = (-1100, 250)
    n_bins = 120

    # 绘制直方图
    hist_data = valid_data[(valid_data > hist_range[0]) & (valid_data < hist_range[1])]
    counts, bin_edges, _ = ax1.hist(
        hist_data, bins=n_bins, range=hist_range,
        color='lightgray', alpha=0.9, edgecolor='gray', linewidth=0.3,
        label='Histogram'
    )

    # 绘制GMM各分量曲线
    x_range = np.linspace(hist_range[0], hist_range[1], 1000)
    bin_width = (hist_range[1] - hist_range[0]) / n_bins
    n_hist = len(hist_data)

    colors = ['#2196F3', '#4CAF50', '#F44336']  # 蓝、绿、红
    labels = ['Air', 'Fat', 'Muscle']

    gmm_total = np.zeros_like(x_range)

    for i, (m, s, w, c, lbl) in enumerate(zip(means, stds, weights, colors, labels)):
        # 高斯分量
        component = w * np.exp(-0.5 * ((x_range - m) / s) ** 2) / (s * np.sqrt(2 * np.pi))
        component_scaled = component * n_hist * bin_width
        gmm_total += component_scaled

        # 绘制分量曲线
        ax1.plot(x_range, component_scaled, '--', color=c, linewidth=2,
                 label=f'GMM {lbl} (~{m:.0f}): μ={m:.1f}')

        # 绘制垂直虚线标记峰值
        ax1.axvline(x=m, color=c, linestyle=':', linewidth=1.5, alpha=0.7)

    # 绘制GMM总曲线
    ax1.plot(x_range, gmm_total, 'k-', linewidth=2, label='GMM Total')

    ax1.set_xlabel('CT Value (HU)', fontsize=12)
    ax1.set_ylabel('Pixel Count', fontsize=12)
    ax1.set_title(f'Soft Tissue Histogram Analysis\n{ct_name}', fontsize=12)
    ax1.legend(loc='upper right', fontsize=9)
    ax1.set_xlim(hist_range)
    ax1.grid(True, alpha=0.3)

    # ===== 右图: 校准曲线 =====
    ax2 = axes[1]

    # 已知密度
    known_densities = [0.0, 0.93, 1.05]

    # 绘制校准线
    hu_line = np.array([-1100, 300])
    rho_line = hu_line * slope + intercept
    ax2.plot(hu_line, rho_line, 'b-', linewidth=2.5,
             label=f'Calibration: ρ = {slope:.6f}×HU + {intercept:.4f}')

    # 标记三个校准点
    for m, d, c, lbl in zip(means, known_densities, colors, labels):
        ax2.scatter([m], [d], s=200, c=c, marker='o', edgecolors='black',
                    linewidth=2, zorder=5)
        # 标注文字
        offset_x = 60 if m > -500 else 80
        offset_y = 0.08 if d < 0.5 else -0.12
        ax2.annotate(f'{lbl}\nHU={m:.0f}\nρ={d:.2f}',
                     xy=(m, d), xytext=(m + offset_x, d + offset_y),
                     fontsize=10, ha='left',
                     bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8),
                     arrowprops=dict(arrowstyle='->', color='gray', lw=1))

    ax2.set_xlabel('CT Value (HU)', fontsize=12)
    ax2.set_ylabel('Density (g/cm³)', fontsize=12)
    ax2.set_title('Phantomless Calibration Curve', fontsize=12)
    ax2.legend(loc='lower right', fontsize=10)
    ax2.set_xlim(-1200, 400)
    ax2.set_ylim(-0.1, 1.4)
    ax2.grid(True, alpha=0.3)
    ax2.axhline(y=0, color='gray', linestyle='-', linewidth=0.5)
    ax2.axhline(y=1.0, color='gray', linestyle=':', linewidth=1, alpha=0.5)
    ax2.axvline(x=0, color='gray', linestyle='-', linewidth=0.5)

    plt.tight_layout()

    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"[HU2E] 校准图已保存: {save_path}")
    else:
        plt.show()

    plt.close()


def plot_E_distribution(E_vals, save_path=None, ct_name="CT"):
    """
    绘制E值累积分布图 (CDF)

    参数:
        E_vals: 弹性模量数组
        save_path: 保存路径
        ct_name: CT文件名
    """
    fig, ax = plt.subplots(figsize=(8, 6))

    # 计算CDF
    sorted_E = np.sort(E_vals)
    cdf = np.arange(1, len(sorted_E) + 1) / len(sorted_E)
    ax.plot(sorted_E, cdf, 'b-', linewidth=2.5)

    # 标记百分位数
    percentiles = [25, 50, 75, 95]
    for p in percentiles:
        val = np.percentile(E_vals, p)
        ax.axhline(y=p / 100, color='gray', linestyle='--', linewidth=0.8, alpha=0.5)
        ax.scatter([val], [p / 100], s=100, c='red', zorder=5, edgecolors='darkred', linewidth=1)
        ax.annotate(f'P{p}: {val:.0f}', xy=(val, p / 100),
                    xytext=(val + (max(E_vals) - min(E_vals)) * 0.05, p / 100 - 0.03),
                    fontsize=10, ha='left')

    ax.set_xlabel('Elastic Modulus (MPa)', fontsize=12)
    ax.set_ylabel('Cumulative Probability', fontsize=12)
    ax.set_title(f'E Value CDF\n{ct_name}', fontsize=12)
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 1.05)
    ax.set_xlim(min(sorted_E) * 0.95, max(sorted_E) * 1.05)

    plt.tight_layout()

    if save_path:
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"[HU2E] E值CDF图已保存: {save_path}")
    else:
        plt.show()

    plt.close()


# ==========================================
# HU → E 转换函数
# ==========================================
def hu_to_E_phantomless(HU_vals, slope, intercept, coef=4730.0, exp=1.56, E_min=100.0, E_max=25000.0):
    """
    Phantomless 3-point calibration chain:
      HU_raw → HU_std (corrected) → rho_QCT → rho_ash → E (Keller 1994)

    Parameters:
        slope, intercept: from 3-point HU correction (HU_std = slope * HU_raw + intercept)
        coef, exp: Keller parameters (default: E = 10200 * rho_ash^2.01)
        E_min, E_max: clamping range

    References:
        - Kopperdahl, Morgan & Keaveny (2002): rho_ash = 0.8772 * rho_QCT + 0.07895
        - Keller (1994): E = 10200 * rho_ash^2.01
    """
    HU_vals = np.asarray(HU_vals, dtype=np.float64)

    # Step 1: HU correction
    hu_std = slope * HU_vals + intercept

    # Step 2: HU_std → rho_QCT (g/cm3)
    rho_qct = hu_std / 1000.0

    # Step 3: rho_QCT → rho_ash (Kopperdahl et al. 2002)
    rho_ash = 0.8772 * rho_qct + 0.07895
    rho_ash = np.maximum(rho_ash, 0.0)

    # Step 4: rho_ash → E (Keller 1994)
    E = coef * np.power(np.maximum(rho_ash, 1e-10), exp)
    E = np.clip(E, E_min, E_max)
    return E.astype(np.float32)


# ==========================================
# IDW 插值函数
# ==========================================
def interp_idw(src_xyz, src_E, query_xyz, k=8, p=2.0, eps=1e-12):
    """反距离加权插值"""
    tree = cKDTree(src_xyz)
    k = min(k, src_xyz.shape[0])
    d, idx = tree.query(query_xyz, k=k, workers=-1)
    w = 1.0 / np.maximum(d, eps) ** p
    w /= np.sum(w, axis=1, keepdims=True)
    vals = np.sum(w * src_E[idx], axis=1)
    return vals.astype(np.float32)


# ==========================================
# CT采样函数
# ==========================================
def sample_ct_at_points(ct_data, ct_affine, xyz_world, order=1):
    """在世界坐标点处采样CT值"""
    ct_inv_affine = np.linalg.inv(ct_affine)
    n_points = xyz_world.shape[0]
    xyz_homo = np.c_[xyz_world, np.ones(n_points)]
    ijk_ct = (ct_inv_affine @ xyz_homo.T)[:3, :]
    HU_vals = map_coordinates(ct_data, ijk_ct, order=order, mode='nearest')
    return HU_vals.astype(np.float32)


# ==========================================
# 主函数
# ==========================================
def HU2E_Projection(
        CT_File=None,
        Mask_File=None,
        Out_Dir=None,
        voxel_size=0.5,
        idw_k=8,
        idw_p=2.0,
        # Phantomless 参数
        calib_samples=800000,
        rho_E_coef=10200.0,
        rho_E_exp=2.01,
        # 全局参数
        E_min=100.0,
        E_max=25000.0,
        # 采样参数
        sample_step=1,
        use_idw=True,
        # 可视化参数
        plot_calibration=False,
        plot_E_dist=False,
        # 进度回调
        progress_callback=None,
):
    """
    HU2E投影函数 (Phantomless版本)

    新增参数:
        plot_calibration: 是否绘制校准图
        plot_E_dist: 是否绘制E值分布图
    """
    # ===== 参数设置 =====
    ct_path = CT_File
    mask_dir = Out_Dir
    mask_path = Mask_File

    os.makedirs(mask_dir, exist_ok=True)
    name = os.path.basename(mask_path).split('.')[0]
    ct_name = os.path.basename(ct_path)
    mask_resampled = os.path.join(mask_dir, f"{name}_resample.nii.gz")

    # ===== 1️⃣ 重采样 mask =====
    print(f"\n[HU2E] === [1/3] 重采样 mask 到 {voxel_size}mm ===")
    if progress_callback:
        progress_callback(15, "Resampling Mask...", None)
    resample_mask_to_034mm(mask_path, out_path=mask_resampled, voxel_size=voxel_size)
    print(f"[HU2E] 重采样完成: {mask_resampled}")

    # ===== 2️⃣ Phantomless HU2E 映射 =====
    print(f"\n[HU2E] === [2/3] Phantomless HU2E 映射 ===")
    if progress_callback:
        progress_callback(35, "Phantomless calibration & HU2E mapping...", None)

    # 加载CT数据 — must use as_closest_canonical so affine matches the canonical filtered mask
    ct_img = nib.as_closest_canonical(nib.load(ct_path))
    ct_data = ct_img.get_fdata()
    ct_affine = ct_img.affine

    # 加载mask用于解剖定位校准
    mask_img_calib = nib.as_closest_canonical(nib.load(mask_path))
    mask_data_calib = mask_img_calib.get_fdata()
    ct_spacing = ct_img.header.get_zooms()[:3]

    # Phantomless 3-point anatomical calibration
    calib_plot_path = os.path.join(mask_dir, f"{name}_calibration.png") if plot_calibration else None
    slope, intercept = phantomless_calib(
        ct_data, n_samples=calib_samples,
        plot=plot_calibration, save_path=calib_plot_path, ct_name=ct_name,
        mask_data=mask_data_calib, spacing=ct_spacing,
    )
    print(f"[HU2E] Phantomless校准: slope={slope:.6f}, intercept={intercept:.6f}")
    print(f"[HU2E] E值公式: E = {rho_E_coef} * rho^{rho_E_exp}")
    print(f"[HU2E] E值范围: {E_min} ~ {E_max} MPa")

    # 加载重采样后的mask — canonical to match CT
    mask_img = nib.as_closest_canonical(nib.load(mask_resampled))
    mask_data = mask_img.get_fdata()
    mask_affine = mask_img.affine

    # 获取mask内的所有体素坐标 (ijk)
    ii_all, jj_all, kk_all = np.where(mask_data > 0)
    n_total = len(ii_all)
    print(f"[HU2E] Mask总体素数: {n_total:,}")

    # 转换为世界坐标
    homog_all = np.c_[ii_all, jj_all, kk_all, np.ones_like(ii_all)]
    XYZ_all = (mask_affine @ homog_all.T).T[:, :3].astype(np.float32)

    # 根据sample_step创建稀疏采样
    step = max(1, int(sample_step))
    if step > 1:
        sel = np.arange(n_total) % step == 0
        XYZ_sparse = XYZ_all[sel]
    else:
        XYZ_sparse = XYZ_all

    n_sparse = len(XYZ_sparse)
    print(f"[HU2E] 稀疏采样点数: {n_sparse:,} (step={step})")

    # 在稀疏点处采样CT的HU值
    HU_sparse = sample_ct_at_points(ct_data, ct_affine, XYZ_sparse, order=1)

    # HU → E (Phantomless方法)
    E_sparse = hu_to_E_phantomless(
        HU_sparse, slope, intercept,
        coef=rho_E_coef, exp=rho_E_exp,
        E_min=E_min, E_max=E_max
    )

    print(f"[HU2E] 稀疏E值统计:")
    print(f"       HU范围: {HU_sparse.min():.1f} ~ {HU_sparse.max():.1f}")
    print(f"       E范围: {E_sparse.min():.1f} ~ {E_sparse.max():.1f} MPa")

    # ===== 3️⃣ IDW 插值生成稠密点云 =====
    print(f"\n[HU2E] === [3/3] IDW 插值 (k={idw_k}, p={idw_p}) ===")
    if progress_callback:
        progress_callback(65, "IDW interpolation...", None)

    if use_idw and step > 1:
        print(f"[HU2E] IDW: {n_sparse:,} 稀疏点 → {n_total:,} 稠密点")
        E_dense = interp_idw(XYZ_sparse, E_sparse, XYZ_all, k=idw_k, p=idw_p)
        XYZ_final = XYZ_all
        E_final = E_dense
    else:
        if step == 1:
            print(f"[HU2E] 无需IDW (sample_step=1, 已计算全部点)")
        else:
            print(f"[HU2E] IDW已禁用，使用稀疏点云")
        XYZ_final = XYZ_sparse
        E_final = E_sparse

    print(f"[HU2E] 最终E值范围: {E_final.min():.1f} ~ {E_final.max():.1f} MPa")
    print(f"[HU2E] 最终点数: {len(E_final):,}")

    # 绘制E值分布图
    if plot_E_dist:
        E_plot_path = os.path.join(mask_dir, f"{name}_E_distribution.png")
        plot_E_distribution(E_final, save_path=E_plot_path, ct_name=ct_name)

    if progress_callback:
        progress_callback(90, "Done", None)

    # ===== 输出内存对象 =====
    csv_obj = pd.DataFrame({
        "NodeIndex": np.arange(1, XYZ_final.shape[0] + 1),
        "X_mm": XYZ_final[:, 0],
        "Y_mm": XYZ_final[:, 1],
        "Z_mm": XYZ_final[:, 2],
        "E_MPa": E_final,
    })

    vertices = np.arange(XYZ_final.shape[0]).reshape(-1, 1)
    vtu_dense = meshio.Mesh(
        points=XYZ_final.astype(np.float64),
        cells=[("vertex", vertices)],
        point_data={"E_MPa": E_final.astype(np.float32)},
    )

    xyzE = np.column_stack((XYZ_final, E_final)).astype(np.float32)

    print(f"[HU2E] === HU2E投影完成 (Phantomless) ===\n")

    # 返回结果，包含校准参数
    result = {
        'csv': csv_obj,
        'vtu': vtu_dense,
        'xyzE': xyzE,
        'slope': slope,
        'intercept': intercept,
        'E_vals': E_final,
    }

    # 返回时附带中间数据，供快速重算使用
    intermediates = {
        'HU_sparse':  HU_sparse,
        'XYZ_sparse': XYZ_sparse,
        'XYZ_all':    XYZ_all,
        'use_idw':    use_idw and step > 1,
        'idw_k': idw_k,
        'idw_p': idw_p,
        'E_min': E_min,
        'E_max': E_max,
        'Out_Dir': mask_dir,
        'name': name,
    }
    return csv_obj, vtu_dense, xyzE, slope, intercept, intermediates


def recompute_E_from_hu(intermediates, slope, intercept,
                        rho_E_coef=4730.0, rho_E_exp=1.56,
                        progress_callback=None):
    """Re-run only HU→E + IDW with new parameters (skip resample & calibration).

    Parameters
    ----------
    intermediates : dict   returned by HU2E_Projection as the 6th element
    slope/intercept        new ρ calibration values
    rho_E_coef/exp         new E formula coefficients
    """
    HU_sparse  = intermediates['HU_sparse']
    XYZ_sparse = intermediates['XYZ_sparse']
    XYZ_all    = intermediates['XYZ_all']
    use_idw    = intermediates['use_idw']
    idw_k      = intermediates['idw_k']
    idw_p      = intermediates['idw_p']
    E_min      = intermediates['E_min']
    E_max      = intermediates['E_max']
    mask_dir   = intermediates['Out_Dir']
    name       = intermediates['name']

    if progress_callback:
        progress_callback(20, "Re-applying HU→E mapping...", "")

    E_sparse = hu_to_E_phantomless(
        HU_sparse, slope, intercept,
        coef=rho_E_coef, exp=rho_E_exp,
        E_min=E_min, E_max=E_max,
    )

    if progress_callback:
        progress_callback(60, "IDW interpolation...", "")

    if use_idw:
        E_final  = interp_idw(XYZ_sparse, E_sparse, XYZ_all,
                              k=idw_k, p=idw_p)
        XYZ_final = XYZ_all
    else:
        E_final   = E_sparse
        XYZ_final = XYZ_sparse

    if progress_callback:
        progress_callback(90, "Building output...", "")

    csv_obj = pd.DataFrame({
        "NodeIndex": np.arange(1, XYZ_final.shape[0] + 1),
        "X_mm": XYZ_final[:, 0],
        "Y_mm": XYZ_final[:, 1],
        "Z_mm": XYZ_final[:, 2],
        "E_MPa": E_final,
    })

    vertices  = np.arange(XYZ_final.shape[0]).reshape(-1, 1)
    vtu_dense = meshio.Mesh(
        points=XYZ_final.astype(np.float64),
        cells=[("vertex", vertices)],
        point_data={"E_MPa": E_final.astype(np.float32)},
    )

    xyzE = np.column_stack((XYZ_final, E_final)).astype(np.float32)
    return csv_obj, vtu_dense, xyzE


# ==========================================
# 独立校准可视化函数 (可单独调用)
# ==========================================
def visualize_calibration(ct_path, save_dir=None):
    """
    单独对CT进行校准可视化

    参数:
        ct_path: CT文件路径
        save_dir: 保存目录 (None则显示)
    """
    ct_name = os.path.basename(ct_path)
    print(f"[Calibration] 正在分析: {ct_name}")

    ct_img = nib.load(ct_path)
    ct_data = ct_img.get_fdata()

    save_path = None
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        name = os.path.splitext(os.path.splitext(ct_name)[0])[0]  # 去掉 .nii.gz
        save_path = os.path.join(save_dir, f"{name}_calibration.png")

    slope, intercept = phantomless_calib(
        ct_data, plot=True, save_path=save_path, ct_name=ct_name
    )

    print(f"[Calibration] slope={slope:.6f}, intercept={intercept:.6f}")
    return slope, intercept


# if __name__ == "__main__":
#     # 测试示例 - 单独校准可视化
#     # visualize_calibration("path/to/ct.nii.gz", save_dir="./output")
#
#     # 完整流程
#     csv_obj, vtu_dense, xyzE = HU2E_Projection(
#         CT_File=r"H:\python project\PHD_PROGRAM\New_ML-FE\Data\Clinical_nii_file\sub-verse504_dir-iso_ct.nii.gz",
#         Mask_File=r"H:\python project\PHD_PROGRAM\New_ML-FE\Data\Clinical_mask_nii_file\sub-verse504_dir-iso_ct_mask.nii.gz",
#         Out_Dir=r"H:\python project\PHD_PROGRAM\New_ML-FE\Data\Result",
#         voxel_size=0.5,
#         idw_k=8,
#         idw_p=2.0,
#         plot_calibration=True,  # 开启校准图
#         plot_E_dist=True,  # 开启E值分布图
#     )
#     print(f"输出形状: CSV={csv_obj.shape}, xyzE={xyzE.shape}")


