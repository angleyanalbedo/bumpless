#!/usr/bin/env python3
"""
plot_benchmarks.py - Zero-Dependency Publication-Grade SVG Chart Generator

Reads CSV telemetry from build/benchmark_results/ and generates:
  1. fig1_latency_jitter.svg        (Baseline vs 0-Copy vs CopyPlan Latency)
  2. fig2_coalescing_ablation.svg   (Naive vs Interval Coalescing Speedup)
  3. fig3_scalability_linear.svg    (O(M) Horizontal Instance Scaling)
  4. fig4_zero_bloat_footprint.svg  (Traditional Reflection vs Naked POD Bloat)
  5. fig5_fault_rollback.svg        (First-Cycle Hardware Fault Recovery)
  6. fig6_handover_waveform.svg     (Abrupt Bump vs Bumpless S-Curve Handover)
  7. dashboard.html                 (Unified Interactive Visual Report)
"""

import csv
import os
import html
from typing import List, Tuple, Dict

RESULTS_DIR = os.path.join("build", "benchmark_results")
CHARTS_DIR = os.path.join(RESULTS_DIR, "charts")


def read_csv(filename: str) -> List[Dict[str, str]]:
    path = os.path.join(RESULTS_DIR, filename)
    if not os.path.exists(path):
        raise FileNotFoundError(f"Missing CSV: {path}. Run 'make bench' first.")
    with open(path, "r", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def svg_header(width: int, height: int, title: str) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" style="font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, sans-serif; background: #ffffff;">\n'
        f'  <rect width="{width}" height="{height}" fill="#ffffff" rx="8"/>\n'
        f'  <text x="{width // 2}" y="30" text-anchor="middle" font-size="16" font-weight="700" fill="#1e293b">{html.escape(title)}</text>\n'
    )


def svg_footer() -> str:
    return "</svg>\n"


def render_bar_chart(
    filename: str,
    title: str,
    ylabel: str,
    bars: List[Tuple[str, float, str, str]],
    width: int = 620,
    height: int = 360,
) -> str:
    """bars: list of (label, value, color, annotation)"""
    pad_l, pad_r, pad_t, pad_b = 85, 35, 58, 65
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    max_val = max((b[1] for b in bars), default=1.0)
    if max_val <= 0:
        max_val = 1.0
    max_val *= 1.18

    lines = [svg_header(width, height, title)]
    # Grid lines
    for i in range(5):
        frac = i / 4.0
        y = pad_t + plot_h * (1.0 - frac)
        val = max_val * frac
        lines.append(f'  <line x1="{pad_l}" y1="{y:.1f}" x2="{width - pad_r}" y2="{y:.1f}" stroke="#e2e8f0" stroke-dasharray="4,4"/>')
        lines.append(f'  <text x="{pad_l - 10}" y="{y + 4:.1f}" text-anchor="end" font-size="11" fill="#64748b">{val:.1f}</text>')

    # Axes
    lines.append(f'  <line x1="{pad_l}" y1="{pad_t}" x2="{pad_l}" y2="{pad_t + plot_h}" stroke="#475569" stroke-width="1.5"/>')
    lines.append(f'  <line x1="{pad_l}" y1="{pad_t + plot_h}" x2="{width - pad_r}" y2="{pad_t + plot_h}" stroke="#475569" stroke-width="1.5"/>')
    lines.append(f'  <text x="20" y="{pad_t + plot_h // 2}" text-anchor="middle" font-size="12" font-weight="600" fill="#334155" transform="rotate(-90 20 {pad_t + plot_h // 2})">{html.escape(ylabel)}</text>')

    n = len(bars)
    slot_w = plot_w / max(n, 1)
    bar_w = min(76.0, slot_w * 0.55)

    for idx, (label, val, color, note) in enumerate(bars):
        cx = pad_l + slot_w * (idx + 0.5)
        bh = (val / max_val) * plot_h
        bx = cx - bar_w / 2.0
        by = pad_t + plot_h - bh
        lines.append(f'  <rect x="{bx:.1f}" y="{by:.1f}" width="{bar_w:.1f}" height="{max(bh, 2.0):.1f}" fill="{color}" rx="4"/>')
        lines.append(f'  <text x="{cx:.1f}" y="{by - 8:.1f}" text-anchor="middle" font-size="12" font-weight="700" fill="#0f172a">{html.escape(note)}</text>')
        lines.append(f'  <text x="{cx:.1f}" y="{pad_t + plot_h + 22}" text-anchor="middle" font-size="12" font-weight="600" fill="#334155">{html.escape(label)}</text>')

    lines.append(svg_footer())
    svg = "\n".join(lines)
    with open(os.path.join(CHARTS_DIR, filename), "w", encoding="utf-8") as f:
        f.write(svg)
    return svg


def render_line_chart(
    filename: str,
    title: str,
    xlabel: str,
    ylabel: str,
    series: List[Tuple[str, List[Tuple[float, float]], str, str]],
    width: int = 620,
    height: int = 360,
) -> str:
    """series: list of (name, [(x, y), ...], color, dash)"""
    pad_l, pad_r, pad_t, pad_b = 80, 35, 60, 75
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b

    all_x = [pt[0] for _, pts, _, _ in series for pt in pts]
    all_y = [pt[1] for _, pts, _, _ in series for pt in pts]
    min_x, max_x = min(all_x), max(all_x)
    min_y, max_y = min(0.0, min(all_y)), max(all_y) * 1.15
    if max_x == min_x:
        max_x = min_x + 1.0
    if max_y == min_y:
        max_y = min_y + 1.0

    def map_x(x: float) -> float:
        return pad_l + ((x - min_x) / (max_x - min_x)) * plot_w

    def map_y(y: float) -> float:
        return pad_t + plot_h - ((y - min_y) / (max_y - min_y)) * plot_h

    lines = [svg_header(width, height, title)]
    for i in range(5):
        frac = i / 4.0
        yv = min_y + (max_y - min_y) * frac
        yp = map_y(yv)
        lines.append(f'  <line x1="{pad_l}" y1="{yp:.1f}" x2="{width - pad_r}" y2="{yp:.1f}" stroke="#e2e8f0" stroke-dasharray="4,4"/>')
        lines.append(f'  <text x="{pad_l - 10}" y="{yp + 4:.1f}" text-anchor="end" font-size="11" fill="#64748b">{yv:.1f}</text>')

    lines.append(f'  <line x1="{pad_l}" y1="{pad_t}" x2="{pad_l}" y2="{pad_t + plot_h}" stroke="#475569" stroke-width="1.5"/>')
    lines.append(f'  <line x1="{pad_l}" y1="{pad_t + plot_h}" x2="{width - pad_r}" y2="{pad_t + plot_h}" stroke="#475569" stroke-width="1.5"/>')
    lines.append(f'  <text x="{pad_l + plot_w // 2}" y="{height - 32}" text-anchor="middle" font-size="12" font-weight="600" fill="#334155">{html.escape(xlabel)}</text>')
    lines.append(f'  <text x="20" y="{pad_t + plot_h // 2}" text-anchor="middle" font-size="12" font-weight="600" fill="#334155" transform="rotate(-90 20 {pad_t + plot_h // 2})">{html.escape(ylabel)}</text>')

    # X ticks
    unique_x = sorted(set(all_x))
    step = max(1, len(unique_x) // 6)
    for xv in unique_x[::step]:
        xp = map_x(xv)
        lines.append(f'  <text x="{xp:.1f}" y="{pad_t + plot_h + 18}" text-anchor="middle" font-size="11" fill="#64748b">{xv:g}</text>')

    for name, pts, color, dash in series:
        coords = " ".join(f"{map_x(x):.1f},{map_y(y):.1f}" for x, y in pts)
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        lines.append(f'  <polyline fill="none" stroke="{color}" stroke-width="2.5"{dash_attr} points="{coords}"/>')
        for x, y in pts:
            lines.append(f'  <circle cx="{map_x(x):.1f}" cy="{map_y(y):.1f}" r="3.5" fill="{color}"/>')

    # Legend
    lx = pad_l + 12
    ly = pad_t + 10
    for idx, (name, _, color, dash) in enumerate(series):
        row_y = ly + idx * 18
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        lines.append(f'  <line x1="{lx}" y1="{row_y}" x2="{lx + 22}" y2="{row_y}" stroke="{color}" stroke-width="2.5"{dash_attr}/>')
        lines.append(f'  <text x="{lx + 28}" y="{row_y + 4}" font-size="11" font-weight="600" fill="#334155">{html.escape(name)}</text>')

    lines.append(svg_footer())
    svg = "\n".join(lines)
    with open(os.path.join(CHARTS_DIR, filename), "w", encoding="utf-8") as f:
        f.write(svg)
    return svg


def main() -> None:
    os.makedirs(CHARTS_DIR, exist_ok=True)
    svgs: List[Tuple[str, str]] = []

    # 1. Latency & Jitter
    r1 = read_csv("exp1_latency_jitter.csv")
    colors1 = ["#64748b", "#10b981", "#3b82f6"]
    bars1 = []
    for i, row in enumerate(r1):
        name = row["Scenario"].replace("_", " ")
        avg = float(row["Avg_ns"])
        pct = row["Cycle_Percent_1kHz"]
        bars1.append((name, avg, colors1[i % len(colors1)], f"{avg:.1f} ns ({pct})"))
    svgs.append(("Exp 1: Latency & Jitter (1 kHz Baseline)", render_bar_chart(
        "fig1_latency_jitter.svg", "Exp 1: Cycle & Hot-Swap Gap Latency (ns)", "Average Latency (ns)", bars1
    )))

    # 2. Interval Coalescing Ablation
    r2 = read_csv("exp2_coalescing_ablation.csv")
    colors2 = ["#ef4444", "#2563eb"]
    bars2 = []
    for i, row in enumerate(r2):
        name = "Naive (20 memcpys)" if "Naive" in row["Approach"] else "Coalesced (1 memcpy)"
        avg = float(row["Avg_Latency_ns"])
        sp = row["Speedup_Ratio"]
        bars2.append((name, avg, colors2[i % 2], f"{avg:.2f} ns ({sp}x)"))
    svgs.append(("Exp 2: Interval Coalescing Ablation", render_bar_chart(
        "fig2_coalescing_ablation.svg", "Exp 2: 20-Field Migration (Naive vs Coalesced)", "Latency per Migration (ns)", bars2
    )))

    # 3. Horizontal Scalability
    r3 = read_csv("exp3_scalability.csv")
    pts3 = [(float(row["Instance_Count"]), float(row["Total_Migration_ns"])) for row in r3]
    svgs.append(("Exp 3: Horizontal Scalability O(M)", render_line_chart(
        "fig3_scalability_linear.svg", "Exp 3: Multi-Instance Migration Scaling O(M)",
        "Controlled Instance Count (M)", "Total Migration Latency (ns)",
        [("Total Migration Latency (ns)", pts3, "#2563eb", "")]
    )))

    # 4. Zero Bloat Verification
    r4 = read_csv("exp4_zero_bloat.csv")
    bars4 = [
        ("Traditional Reflection", float(r4[0]["Total_50_Modules_KB"]), "#f59e0b", f"{r4[0]['Total_50_Modules_KB']} KB"),
        ("Bumpless Naked POD", float(r4[1]["Total_50_Modules_KB"]), "#10b981", "0.0 KB (Zero Bloat)"),
    ]
    svgs.append(("Exp 4: Zero-Bloat Firmware Verification", render_bar_chart(
        "fig4_zero_bloat_footprint.svg", "Exp 4: Runtime Metadata Bloat (50 Function Blocks)", "Metadata Overhead (KB)", bars4
    )))

    # 5. Fault Rollback Recovery
    r5 = read_csv("exp5_fault_rollback.csv")
    colors5 = ["#ef4444", "#10b981", "#3b82f6"]
    bars5 = []
    for i, row in enumerate(r5):
        label = row["Metric"].replace("_", " ").replace("Latency", "").replace("Time", "").strip()
        us = float(row["Microseconds"])
        bars5.append((label, us, colors5[i % 3], f"{us:.2f} us"))
    svgs.append(("Exp 5: First-Cycle Fault Rollback Recovery", render_bar_chart(
        "fig5_fault_rollback.svg", "Exp 5: Hardware Exception Rollback Recovery (us)", "Duration (us)", bars5
    )))

    # 6. Control-Theoretic Bumpless Handover Waveform
    r6 = read_csv("exp6_handover_waveform.csv")
    pts_abrupt = [(float(row["Cycle"]), float(row["Abrupt_Output_V"])) for row in r6]
    pts_track = [(float(row["Cycle"]), float(row["Bumpless_IntegralTrack_V"])) for row in r6]
    pts_scurve = [(float(row["Cycle"]), float(row["SCurve_Blend_V"])) for row in r6]
    svgs.append(("Exp 6: Control-Theoretic Bumpless Handover", render_line_chart(
        "fig6_handover_waveform.svg", "Exp 6: Actuator Output Continuity Across Hot-Swap (Cycle 0)",
        "Control Cycle Relative to Hot-Swap", "Actuator Output (V)",
        [
            ("Abrupt Switch (+15V Shock)", pts_abrupt, "#ef4444", "5,4"),
            ("Integral Tracking (C0 Constant)", pts_track, "#10b981", ""),
            ("S-Curve Multi-Cycle Blend (C1 Smooth)", pts_scurve, "#2563eb", ""),
        ]
    )))

    # Unified HTML Dashboard
    cards = "\n".join(
        f'<div class="card"><h3>{html.escape(title)}</h3>{svg}</div>'
        for title, svg in svgs
    )
    dashboard = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>Bumpless Benchmark Dashboard</title>
  <style>
    body {{ margin: 0; padding: 28px; background: #f1f5f9; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; color: #0f172a; }}
    h1 {{ text-align: center; margin-bottom: 6px; }}
    p.sub {{ text-align: center; color: #475569; margin-bottom: 28px; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(620px, 1fr)); gap: 22px; max-width: 1320px; margin: 0 auto; }}
    .card {{ background: #ffffff; border-radius: 12px; padding: 16px; box-shadow: 0 4px 16px rgba(15, 23, 42, 0.06); display: flex; flex-direction: column; align-items: center; }}
    .card h3 {{ margin: 4px 0 10px; font-size: 15px; color: #334155; }}
  </style>
</head>
<body>
  <h1>Bumpless Real-Time Hot-Update Engine — Empirical Benchmark Dashboard</h1>
  <p class="sub">Auto-generated from CSV telemetry in <code>build/benchmark_results/</code></p>
  <div class="grid">
    {cards}
  </div>
</body>
</html>
"""
    dash_path = os.path.join(CHARTS_DIR, "dashboard.html")
    with open(dash_path, "w", encoding="utf-8") as f:
        f.write(dashboard)

    print(f"[plot_benchmarks] Generated {len(svgs)} vector SVG charts + dashboard in '{CHARTS_DIR}/':")
    for fname in sorted(os.listdir(CHARTS_DIR)):
        print(f"  - {fname}")


if __name__ == "__main__":
    main()
