import asyncio
import time
import random
import struct
from datetime import datetime
import numpy as np
from .aes_manager import AESManager
from .logger import Logger

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
            if self.message_count % 100 == 0:
                Logger.warning(f"{self.device_id}: Connection error")
                