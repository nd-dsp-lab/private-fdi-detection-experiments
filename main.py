#!/usr/bin/env python3
import asyncio
import sys
import time
import random
import struct
import argparse
from datetime import datetime
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding, hashes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.backends import default_backend
import numpy as np

# ANSI color codes
RESET = '\033[0m'
RED = '\033[31m'
GREEN = '\033[32m'
YELLOW = '\033[33m'
BLUE = '\033[34m'
CYAN = '\033[36m'
BOLD = '\033[1m'

class Logger:
    @staticmethod
    def timestamp():
        return datetime.now().strftime("%H:%M:%S")

    @staticmethod
    def info(msg):
        print(f"{CYAN}[{Logger.timestamp()}] [INFO]{RESET} {msg}")

    @staticmethod
    def success(msg):
        print(f"{GREEN}[{Logger.timestamp()}] [OK]{RESET} {msg}")

    @staticmethod
    def warning(msg):
        print(f"{YELLOW}[{Logger.timestamp()}] [WARN]{RESET} {msg}")

    @staticmethod
    def error(msg):
        print(f"{RED}[{Logger.timestamp()}] [ERROR]{RESET} {msg}")

class AESManager:
    def __init__(self):
        self.device_keys = {}

    def generate_device_key(self, device_id):
        salt = device_id.encode()[:16].ljust(16, b'0')
        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=32,
            salt=salt,
            iterations=100000,
            backend=default_backend()
        )
        key = kdf.derive(f"smart_meter_{device_id}".encode())
        self.device_keys[device_id] = key
        return key

    def encrypt_fixed_size(self, device_id, data):
        key = self.device_keys[device_id]

        # Generate deterministic IV
        iv_seed = hashes.Hash(hashes.SHA256(), backend=default_backend())
        iv_seed.update(key + device_id.encode())
        iv = iv_seed.finalize()[:16]

        if len(data) > 15:
            data = data[:15]

        cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
        encryptor = cipher.encryptor()

        padder = padding.PKCS7(128).padder()
        padded_data = padder.update(data) + padder.finalize()

        encrypted = encryptor.update(padded_data) + encryptor.finalize()
        return encrypted

class EdgeDevice:
    def __init__(self, device_id, server_host, server_port, interval):
        self.device_id = device_id
        self.server_host = server_host
        self.server_port = server_port
        self.interval = interval
        self.aes_manager = AESManager()
        self.aes_manager.generate_device_key(device_id)
        self.message_count = 0

    def generate_reading(self):
        timestamp = time.time()
        dt = datetime.fromtimestamp(timestamp)
        hour = dt.hour

        daily_factor = 0.7 + 0.3 * (1 + np.sin((hour - 6) * np.pi / 12))
        noise_factor = random.uniform(0.9, 1.1)

        power = 2000 * daily_factor * noise_factor
        voltage = 120.0 + random.uniform(-2, 2)
        frequency = 60.0 + random.uniform(-0.1, 0.1)
        current = power / voltage

        return {
            'timestamp': timestamp,
            'voltage': voltage,
            'current': current,
            'power': power,
            'frequency': frequency
        }

    def serialize_compact(self, reading):
        timestamp_int = int(reading['timestamp']) & 0xFFFFFFFF
        voltage_int = int(reading['voltage'] * 10) & 0xFFFF
        current_int = int(reading['current'] * 10) & 0xFFFF
        power_int = int(reading['power']) & 0xFFFF
        frequency_int = int(reading['frequency'] * 10) & 0xFF

        device_num = int(self.device_id.split('_')[1]) & 0xFFFF

        data = struct.pack('>IHHHHB',
                          timestamp_int, device_num, voltage_int,
                          current_int, power_int, frequency_int)
        return data

    async def run(self):
        Logger.info(f"Device {self.device_id} starting...")

        while True:
            try:
                reading = self.generate_reading()
                data = self.serialize_compact(reading)
                encrypted_data = self.aes_manager.encrypt_fixed_size(self.device_id, data)

                await self.send_to_server(encrypted_data)
                self.message_count += 1

                if self.message_count % 20 == 0:
                    Logger.success(f"{self.device_id}: Sent {self.message_count} messages (Power: {reading['power']:.1f}W)")

                await asyncio.sleep(self.interval)

            except Exception as e:
                Logger.error(f"{self.device_id}: {e}")
                await asyncio.sleep(5)

    async def send_to_server(self, encrypted_data):
        try:
            reader, writer = await asyncio.open_connection(self.server_host, self.server_port)

            header = f"{self.device_id}:16\n".encode()
            writer.write(header)
            writer.write(encrypted_data)
            await writer.drain()

            writer.close()
            await writer.wait_closed()

        except Exception as e:
            if self.message_count % 100 == 0:  # Reduce error spam
                Logger.warning(f"{self.device_id}: Connection error")

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
    