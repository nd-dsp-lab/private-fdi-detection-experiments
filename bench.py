#!/usr/bin/env python3
import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

def start_server(server_bin, port, devices, sum_interval, target_readings, metrics_file, threads, quiet):
    args = [
        server_bin,
        "--port", str(port),
        "--devices", str(devices),
        "--sum-interval", str(sum_interval),
        "--benchmark-readings", str(target_readings),
        "--metrics", str(metrics_file),
    ]
    if threads:
        args += ["--threads", str(threads)]
    if quiet:
        args += ["--quiet"]
    return subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

def start_clients(num_devices, interval, port):
    procs = []
    for i in range(num_devices):
        device_id = f"meter_{i:06d}"
        p = subprocess.Popen(
            [sys.executable, "-m", "client.main",
             "--device", device_id,
             "--host", "localhost",
             "--port", str(port),
             "--interval", str(interval)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        procs.append(p)
        # tiny stagger to avoid thundering herd in connect()
        if i % 50 == 0:
            time.sleep(0.01)
    return procs

def kill_procs(procs):
    for p in procs:
        if p.poll() is None:
            p.terminate()
    deadline = time.time() + 3
    while time.time() < deadline and any(p.poll() is None for p in procs):
        time.sleep(0.05)
    for p in procs:
        if p.poll() is None:
            p.kill()

def run_trial(server_bin, devices, interval, port, target_readings, threads, quiet):
    metrics_path = Path(f"./metrics_{devices}.json")
    if metrics_path.exists():
        metrics_path.unlink()

    sum_interval = max(10, devices // 10)

    server = start_server(server_bin, port, devices, sum_interval, target_readings, str(metrics_path), threads, quiet)

    # Wait a moment for server to bind
    time.sleep(1.5)

    clients = start_clients(devices, interval, port)

    # Stream server output (optional)
    # Comment out if you want a quiet console
    def drain_server_output(proc):
        for line in proc.stdout:
            # print(line, end="")  # uncomment to see logs
            pass
    # If you want to capture logs in background, you can spin a thread

    # Wait for server finish (it exits when target_readings reached)
    ret = server.wait()
    # Tear down clients
    kill_procs(clients)

    # Give filesystem a moment to flush metrics
    deadline = time.time() + 2
    while time.time() < deadline and not metrics_path.exists():
        time.sleep(0.05)

    if not metrics_path.exists():
        raise RuntimeError("Metrics file not found. Did the server exit early or crash?")

    with metrics_path.open() as f:
        metrics = json.load(f)

    return {
        "devices": devices,
        "seconds": metrics["seconds"],
        "throughput_rps": metrics["throughput_rps"],
        "total_readings": metrics["total_readings"],
        "target": metrics["benchmark_target"],
    }

def main():
    ap = argparse.ArgumentParser(description="Smart Grid benchmark harness")
    ap.add_argument("--server-bin", default="./smart_grid_server", help="Path to server executable")
    ap.add_argument("--min-devices", type=int, default=100)
    ap.add_argument("--max-devices", type=int, default=1000)
    ap.add_argument("--step", type=int, default=100)
    ap.add_argument("--interval", type=int, default=1, help="Client reporting interval (seconds)")
    ap.add_argument("--port", type=int, default=8890)
    ap.add_argument("--target-readings", type=int, default=100000, help="Server stops after this many readings")
    ap.add_argument("--threads", type=int, default=0, help="Server thread count (0=auto)")
    ap.add_argument("--quiet", action="store_true", help="Suppress periodic server logs")
    ap.add_argument("--trials", type=int, default=1, help="Trials per device count")
    args = ap.parse_args()

    results = []
    for devices in range(args.min_devices, args.max_devices + 1, args.step):
        for t in range(args.trials):
            print(f"Running: devices={devices}, trial={t+1}/{args.trials} ...")
            res = run_trial(
                args.server_bin,
                devices,
                args.interval,
                args.port,
                args.target_readings,
                args.threads,
                args.quiet
            )
            results.append(res)
            print(f"  -> {res['throughput_rps']:.2f} rps, {res['seconds']:.3f}s for {res['total_readings']} readings")

    print("\nSummary:")
    for r in results:
        print(f"devices={r['devices']:6d}  throughput={r['throughput_rps']:10.2f} rps  time={r['seconds']:8.3f}s  readings={r['total_readings']}")
    # You could also dump CSV/JSON here.

if __name__ == "__main__":
    main()
    