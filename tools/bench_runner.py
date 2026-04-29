#!/usr/bin/env python3
"""Run a simple two-client NTS speed comparison.

The runner builds the Rust matching engine, starts one exchange, then starts two
pipeline binaries at nearly the same time. On Linux it uses taskset to pin the
exchange and clients to requested cores. On macOS it runs without pinning because
Darwin does not expose a reliable process-to-core pinning CLI.
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run two nts_pipeline processes against one matching engine."
    )
    parser.add_argument(
        "--pipeline-a",
        type=Path,
        default=ROOT / "build" / "nts_pipeline",
        help="First nts_pipeline binary.",
    )
    parser.add_argument(
        "--pipeline-b",
        type=Path,
        default=ROOT / "build" / "nts_pipeline",
        help="Second nts_pipeline binary.",
    )
    parser.add_argument(
        "--exchange",
        type=Path,
        default=ROOT / "matching-engine" / "target" / "release" / "matching-engine",
        help="matching-engine binary.",
    )
    parser.add_argument("--duration", type=float, default=10.0, help="Client run duration seconds.")
    parser.add_argument("--tick-rate", type=int, default=10_000, help="Exchange ticks per second.")
    parser.add_argument(
        "--settle",
        type=float,
        default=0.25,
        help="Seconds to wait after starting exchange before connecting clients.",
    )
    parser.add_argument(
        "--base-port",
        type=int,
        default=24_000,
        help="Base port. Each trial offsets this by 10 to avoid TIME_WAIT conflicts.",
    )
    parser.add_argument("--md-group", default="239.1.1.1")
    parser.add_argument("--ref-group", default="239.1.1.2")
    parser.add_argument(
        "--out-dir",
        type=Path,
        help="Output directory for --keep-logs. Defaults to a temporary directory.",
    )
    parser.add_argument(
        "--timeout-extra",
        type=float,
        default=5.0,
        help="Extra seconds beyond --duration before killing a stuck client.",
    )
    parser.add_argument("--exchange-core", type=int, default=3)
    parser.add_argument("--client-a-core", type=int, default=1)
    parser.add_argument("--client-b-core", type=int, default=2)
    parser.add_argument("--keep-logs", action="store_true", help="Keep raw stderr/stdout logs.")
    return parser.parse_args()


def run_cmd(cmd: list[str], cwd: Path = ROOT) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def build() -> None:
    run_cmd(
        ["cargo", "build", "--manifest-path", "matching-engine/Cargo.toml", "--release"]
    )


def check_binary(path: Path, name: str) -> None:
    if not path.exists():
        raise SystemExit(f"{name} binary not found: {path}")
    if not os.access(path, os.X_OK):
        raise SystemExit(f"{name} binary is not executable: {path}")


def with_core(cmd: list[str], core: int) -> list[str]:
    if platform.system() == "Linux":
        return ["taskset", "-c", str(core), *cmd]
    return cmd


def start_process(
    cmd: list[str],
    stdout_path: Path,
    stderr_path: Path,
    cwd: Path = ROOT,
) -> tuple[subprocess.Popen[bytes], object, object]:
    stdout_file = stdout_path.open("wb")
    stderr_file = stderr_path.open("wb")
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=stdout_file,
        stderr=stderr_file,
        start_new_session=True,
    )
    return proc, stdout_file, stderr_file


def terminate_process(proc: subprocess.Popen[bytes], timeout: float = 2.0) -> None:
    if proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGINT)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=timeout)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=timeout)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        return
    proc.wait(timeout=timeout)


def pipeline_cmd(
    binary: Path,
    duration: float,
    md_port: int,
    ref_port: int,
    order_port: int,
    md_group: str,
    ref_group: str,
    core: int,
) -> list[str]:
    return with_core([
        str(binary),
        "--duration",
        format_duration(duration),
        "--md-port",
        str(md_port),
        "--md-group",
        md_group,
        "--ref-port",
        str(ref_port),
        "--ref-group",
        ref_group,
        "--order-port",
        str(order_port),
    ], core)


def exchange_cmd(
    binary: Path,
    tick_rate: int,
    md_port: int,
    ref_port: int,
    order_port: int,
    md_group: str,
    ref_group: str,
    core: int,
) -> list[str]:
    return with_core([
        str(binary),
        "serve",
        "--tick-rate",
        str(tick_rate),
        "--md-port",
        str(md_port),
        "--md-group",
        md_group,
        "--ref-port",
        str(ref_port),
        "--ref-group",
        ref_group,
        "--order-port",
        str(order_port),
    ], core)


def format_duration(duration: float) -> str:
    if duration.is_integer():
        return str(int(duration))
    return str(duration)


def read_text(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def print_block(title: str, text: str) -> None:
    print(f"\n=== {title} ===")
    stripped = text.strip()
    print(stripped if stripped else "(empty)")


def extract_server_client_report(text: str, client_id: int) -> str:
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line == f"--- Client {client_id} ---":
            start = i
            break
    if start is None:
        return ""

    end = len(lines)
    for i in range(start + 1, len(lines)):
        if lines[i].startswith("--- Client "):
            end = i
            break
    return "\n".join(lines[start:end])


def parse_pipeline_report(text: str) -> dict[str, float | int]:
    metrics: dict[str, float | int] = {}

    if m := re.search(
        r"\[pipeline\] Orders: (\d+) total, (\d+) accepted, (\d+) filled, "
        r"(\d+) missed IOC, (\d+) rejected \(([0-9.]+)% hit\)",
        text,
    ):
        metrics.update(
            {
                "pipeline_orders_total": int(m.group(1)),
                "pipeline_orders_accepted": int(m.group(2)),
                "pipeline_orders_filled": int(m.group(3)),
                "pipeline_missed_ioc": int(m.group(4)),
                "pipeline_rejected": int(m.group(5)),
                "pipeline_hit_rate": float(m.group(6)),
            }
        )

    if m := re.search(
        r"\[pipeline\] Fills:\s+(\d+) events, (\d+) qty "
        r"\((\d+) buys / (\d+) buy qty, (\d+) sells / (\d+) sell qty\)",
        text,
    ):
        metrics.update(
            {
                "pipeline_fills": int(m.group(1)),
                "pipeline_fill_qty": int(m.group(2)),
                "pipeline_buy_fills": int(m.group(3)),
                "pipeline_buy_qty": int(m.group(4)),
                "pipeline_sell_fills": int(m.group(5)),
                "pipeline_sell_qty": int(m.group(6)),
            }
        )

    if m := re.search(
        r"\[pipeline\] PnL:\s+realized ([+-]?[0-9.]+), "
        r"liquidation ([+-]?[0-9.]+), total ([+-]?[0-9.]+)",
        text,
    ):
        metrics.update(
            {
                "pipeline_realized_pnl": float(m.group(1)),
                "pipeline_liquidation_pnl": float(m.group(2)),
                "pipeline_total_pnl": float(m.group(3)),
            }
        )

    if m := re.search(
        r"RecvDone -> OrderSent \(tick-to-order\)\s+(\d+)\s+"
        r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)",
        text,
    ):
        metrics.update(
            {
                "tick_to_order_samples": int(m.group(1)),
                "tick_to_order_min_ns": int(m.group(2)),
                "tick_to_order_p50_ns": int(m.group(3)),
                "tick_to_order_p90_ns": int(m.group(4)),
                "tick_to_order_p99_ns": int(m.group(5)),
                "tick_to_order_p999_ns": int(m.group(6)),
                "tick_to_order_max_ns": int(m.group(7)),
                "tick_to_order_mean_ns": int(m.group(8)),
            }
        )

    return metrics


def parse_server_report(text: str) -> dict[int, dict[str, float | int]]:
    clients: dict[int, dict[str, float | int]] = {}
    current: int | None = None

    for line in text.splitlines():
        if m := re.match(r"--- Client (\d+) ---", line):
            current = int(m.group(1))
            clients[current] = {}
            continue

        if current is None:
            continue

        row = clients[current]
        if m := re.search(
            r"Orders:\s+(\d+) total, (\d+) accepted, (\d+) filled, "
            r"(\d+) missed IOC, (\d+) rejected \(([0-9.]+)% hit\)",
            line,
        ):
            row.update(
                {
                    "server_orders_total": int(m.group(1)),
                    "server_orders_accepted": int(m.group(2)),
                    "server_orders_filled": int(m.group(3)),
                    "server_missed_ioc": int(m.group(4)),
                    "server_rejected": int(m.group(5)),
                    "server_hit_rate": float(m.group(6)),
                }
            )
        elif m := re.search(
            r"Stale:\s+(\d+) fills, (\d+) misses, capture ([0-9.]+)%, "
            r"avg arrival lag ([0-9.]+) ticks",
            line,
        ):
            row.update(
                {
                    "stale_fills": int(m.group(1)),
                    "stale_misses": int(m.group(2)),
                    "stale_capture_rate": float(m.group(3)),
                    "avg_arrival_lag_ticks": float(m.group(4)),
                }
            )
        elif m := re.search(
            r"Fills:\s+(\d+) events, (\d+) qty "
            r"\((\d+) buys / (\d+) buy qty, (\d+) sells / (\d+) sell qty\)",
            line,
        ):
            row.update(
                {
                    "server_fills": int(m.group(1)),
                    "server_fill_qty": int(m.group(2)),
                    "server_buy_fills": int(m.group(3)),
                    "server_buy_qty": int(m.group(4)),
                    "server_sell_fills": int(m.group(5)),
                    "server_sell_qty": int(m.group(6)),
                }
            )
        elif m := re.search(
            r"PnL:\s+realized ([+-]?[0-9.]+), liquidation ([+-]?[0-9.]+), "
            r"total ([+-]?[0-9.]+)",
            line,
        ):
            row.update(
                {
                    "server_realized_pnl": float(m.group(1)),
                    "server_liquidation_pnl": float(m.group(2)),
                    "server_total_pnl": float(m.group(3)),
                }
            )

    return clients


def fnum(row: dict[str, object], key: str) -> float:
    return float(row.get(key, 0.0))


def inum(row: dict[str, object], key: str) -> int:
    return int(row.get(key, 0))


def print_client(label: str, row: dict[str, object]) -> None:
    print(
        f"{label}: "
        f"PnL {fnum(row, 'server_total_pnl'):+.2f}, "
        f"stale fills {inum(row, 'stale_fills')}, "
        f"stale misses {inum(row, 'stale_misses')}, "
        f"hit {fnum(row, 'server_hit_rate'):.1f}%, "
        f"tick-to-order p50 {inum(row, 'tick_to_order_p50_ns')}ns, "
        f"p99 {inum(row, 'tick_to_order_p99_ns')}ns"
    )


def print_summary(summary: dict[str, object]) -> None:
    reports = summary["reports"]

    print_block("Client A Pipeline Report", reports["client_a_pipeline"])
    print_block("Client B Pipeline Report", reports["client_b_pipeline"])


def run_once(args: argparse.Namespace, out_dir: Path) -> dict[str, object]:
    md_port = args.base_port
    ref_port = md_port + 2
    order_port = md_port + 1

    exchange_stdout = out_dir / "exchange.stdout.log"
    exchange_stderr = out_dir / "exchange.stderr.log"
    client_a_stdout = out_dir / "client_a.stdout.log"
    client_a_stderr = out_dir / "client_a.stderr.log"
    client_b_stdout = out_dir / "client_b.stdout.log"
    client_b_stderr = out_dir / "client_b.stderr.log"

    exchange_proc, exchange_out, exchange_err = start_process(
        exchange_cmd(
            args.exchange,
            args.tick_rate,
            md_port,
            ref_port,
            order_port,
            args.md_group,
            args.ref_group,
            args.exchange_core,
        ),
        exchange_stdout,
        exchange_stderr,
    )

    client_procs: list[tuple[subprocess.Popen[bytes], object, object]] = []

    try:
        time.sleep(args.settle)
        proc_a, out_a, err_a = start_process(
            pipeline_cmd(
                args.pipeline_a,
                args.duration,
                md_port,
                ref_port,
                order_port,
                args.md_group,
                args.ref_group,
                args.client_a_core,
            ),
            client_a_stdout,
            client_a_stderr,
        )
        proc_b, out_b, err_b = start_process(
            pipeline_cmd(
                args.pipeline_b,
                args.duration,
                md_port,
                ref_port,
                order_port,
                args.md_group,
                args.ref_group,
                args.client_b_core,
            ),
            client_b_stdout,
            client_b_stderr,
        )
        client_procs = [(proc_a, out_a, err_a), (proc_b, out_b, err_b)]

        for proc, stdout_file, stderr_file in client_procs:
            try:
                proc.wait(timeout=args.duration + args.timeout_extra)
            except subprocess.TimeoutExpired:
                terminate_process(proc)
            stdout_file.close()
            stderr_file.close()

        time.sleep(0.1)
        terminate_process(exchange_proc)
    finally:
        for proc, stdout_file, stderr_file in client_procs:
            terminate_process(proc)
            stdout_file.close()
            stderr_file.close()
        terminate_process(exchange_proc)
        exchange_out.close()
        exchange_err.close()

    exchange_report = read_text(exchange_stderr)
    client_a_report = read_text(client_a_stderr)
    client_b_report = read_text(client_b_stderr)
    server_clients = parse_server_report(exchange_report)
    client_a = parse_pipeline_report(client_a_report)
    client_b = parse_pipeline_report(client_b_report)
    client_a.update(server_clients.get(1, {}))
    client_b.update(server_clients.get(2, {}))

    summary: dict[str, object] = {
        "platform": platform.system(),
        "pinning": "taskset" if platform.system() == "Linux" else "not available on this OS",
        "exchange_core": args.exchange_core,
        "client_a_core": args.client_a_core,
        "client_b_core": args.client_b_core,
        "duration_sec": args.duration,
        "tick_rate": args.tick_rate,
        "client_a": client_a,
        "client_b": client_b,
        "diff": {
            "client_b_minus_a_pnl": float(client_b.get("server_total_pnl", 0.0))
            - float(client_a.get("server_total_pnl", 0.0)),
            "client_b_minus_a_stale_fills": int(client_b.get("stale_fills", 0))
            - int(client_a.get("stale_fills", 0)),
            "client_b_minus_a_tick_to_order_p50_ns": int(
                client_b.get("tick_to_order_p50_ns", 0)
            )
            - int(client_a.get("tick_to_order_p50_ns", 0)),
            "client_b_minus_a_tick_to_order_p99_ns": int(
                client_b.get("tick_to_order_p99_ns", 0)
            )
            - int(client_a.get("tick_to_order_p99_ns", 0)),
        },
        "reports": {
            "client_a_pipeline": client_a_report,
            "client_a_server": extract_server_client_report(exchange_report, 1),
            "client_b_pipeline": client_b_report,
            "client_b_server": extract_server_client_report(exchange_report, 2),
        },
    }
    return summary


def main() -> int:
    args = parse_args()
    build()

    check_binary(args.pipeline_a, "pipeline-a")
    check_binary(args.pipeline_b, "pipeline-b")
    check_binary(args.exchange, "exchange")

    temp_dir: tempfile.TemporaryDirectory[str] | None = None
    if args.keep_logs:
        out_dir = args.out_dir or (ROOT / "results" / "bench" / "latest")
        out_dir.mkdir(parents=True, exist_ok=True)
    else:
        temp_dir = tempfile.TemporaryDirectory(prefix="nts_bench_")
        out_dir = Path(temp_dir.name)

    if platform.system() != "Linux":
        print("warning: process pinning is only enabled on Linux via taskset", file=sys.stderr)

    try:
        summary = run_once(args, out_dir)
        print_summary(summary)
        if args.keep_logs:
            print(f"logs: {out_dir}")
    finally:
        if temp_dir is not None:
            temp_dir.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
