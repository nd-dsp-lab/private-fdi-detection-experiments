#!/usr/bin/env python3
import asyncio
import argparse
from .device import EdgeDevice

def main():
    parser = argparse.ArgumentParser(description="Smart Meter Edge Device")
    parser.add_argument("--device", required=True, help="Device ID")
    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", type=int, default=8890, help="Server port")
    parser.add_argument("--interval", type=int, default=2, help="Reporting interval")

    args = parser.parse_args()

    device = EdgeDevice(args.device, args.host, args.port, args.interval)
    asyncio.run(device.run())

if __name__ == "__main__":
    main()
    