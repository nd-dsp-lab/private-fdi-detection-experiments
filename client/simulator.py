#!/usr/bin/env python3
# client/simulator.py (fixed connection handling)
import asyncio
import argparse
import time
import random
import struct
import psutil
import os
from datetime import datetime
import numpy as np
from .aes_manager import AESManager
from .logger import Logger

class BulkDeviceSimulator:
    def __init__(self, num_devices, server_host, server_port, interval,
                 batch_size=100, chunk_size=25, safety_mode=True):
        self.num_devices = num_devices
        self.server_host = server_host
        self.server_port = server_port
        self.interval = interval
        self.batch_size = batch_size
        self.chunk_size = min(chunk_size, batch_size)
        self.safety_mode = safety_mode

        self.aes_manager = AESManager()
        self.total_messages = 0
        self.total_errors = 0
        self.cycle_count = 0
        self.connection_semaphore = asyncio.Semaphore(50)  # Limit concurrent connections

        # Pre-generate device keys
        Logger.info(f"Pre-generating keys for {num_devices} devices...")
        self.device_keys = {}
        for i in range(num_devices):
            device_id = f"meter_{i:06d}"
            self.aes_manager.generate_device_key(device_id)
            self.device_keys[device_id] = i

        Logger.success(f"Initialized {num_devices} device profiles")

    def generate_bulk_readings(self):
        """Generate readings for all devices at once"""
        timestamp = time.time()
        dt = datetime.fromtimestamp(timestamp)
        hour = dt.hour

        daily_factor = 0.7 + 0.3 * (1 + np.sin((hour - 6) * np.pi / 12))

        readings = []
        for i in range(self.num_devices):
            device_id = f"meter_{i:06d}"
            device_factor = 0.8 + 0.4 * (i % 100) / 100
            noise_factor = random.uniform(0.9, 1.1)

            power = 2000 * daily_factor * noise_factor * device_factor
            voltage = 120.0 + random.uniform(-2, 2)
            frequency = 60.0 + random.uniform(-0.1, 0.1)
            current = power / voltage

            reading = {
                'device_id': device_id,
                'timestamp': timestamp,
                'voltage': voltage,
                'current': current,
                'power': power,
                'frequency': frequency
            }
            readings.append(reading)

        return readings

    def serialize_and_encrypt_batch(self, readings):
        """Process a batch of readings efficiently"""
        encrypted_messages = []

        for reading in readings:
            timestamp_int = int(reading['timestamp']) & 0xFFFFFFFF
            voltage_int = int(reading['voltage'] * 10) & 0xFFFF
            current_int = int(reading['current'] * 10) & 0xFFFF
            power_int = int(reading['power']) & 0xFFFF
            frequency_int = int(reading['frequency'] * 10) & 0xFF
            device_num = self.device_keys[reading['device_id']] & 0xFFFF

            data = struct.pack('>IHHHHB',
                               timestamp_int, device_num, voltage_int,
                               current_int, power_int, frequency_int)

            encrypted_data = self.aes_manager.encrypt_fixed_size(reading['device_id'], data)
            header = f"{reading['device_id']}:16\n".encode()
            message = header + encrypted_data
            encrypted_messages.append(message)

        return encrypted_messages

    async def send_chunk(self, messages):
        """Send a chunk with improved error handling and connection management"""
        async with self.connection_semaphore:  # Limit concurrent connections
            max_retries = 3
            retry_delay = 0.1

            for attempt in range(max_retries):
                try:
                    # Use shorter timeout and connection limit
                    reader, writer = await asyncio.wait_for(
                        asyncio.open_connection(self.server_host, self.server_port),
                        timeout=2.0
                    )

                    # Send all messages in the chunk
                    for message in messages:
                        writer.write(message)

                    # Ensure data is sent
                    await asyncio.wait_for(writer.drain(), timeout=5.0)

                    # Proper connection cleanup
                    writer.close()
                    await writer.wait_closed()

                    return True

                except asyncio.TimeoutError:
                    if attempt < max_retries - 1:
                        await asyncio.sleep(retry_delay * (2 ** attempt))
                        continue
                    else:
                        return False

                except ConnectionRefusedError:
                    Logger.warning("Connection refused - server may be overloaded")
                    if attempt < max_retries - 1:
                        await asyncio.sleep(retry_delay * (2 ** attempt))
                        continue
                    else:
                        return False

                except Exception as e:
                    if attempt < max_retries - 1:
                        await asyncio.sleep(retry_delay * (2 ** attempt))
                        continue
                    else:
                        if self.total_errors % 50 == 1:  # Log occasionally
                            Logger.warning(f"Chunk send failed after {max_retries} attempts: {type(e).__name__}")
                        return False

    async def send_batch(self, messages):
        """Send a batch of messages using chunked connections with rate limiting"""
        tasks = []
        successful = 0

        # Split into smaller chunks and add delays between chunks
        for i in range(0, len(messages), self.chunk_size):
            chunk = messages[i:i + self.chunk_size]
            task = asyncio.create_task(self.send_chunk(chunk))
            tasks.append(task)

            # Small delay between chunk creation to avoid overwhelming server
            if i > 0 and i % (self.chunk_size * 4) == 0:
                await asyncio.sleep(0.01)

        # Wait for all chunks with timeout
        try:
            results = await asyncio.wait_for(
                asyncio.gather(*tasks, return_exceptions=True),
                timeout=30.0
            )

            successful = sum(1 for r in results if r is True)
            failed = len(results) - successful
            self.total_errors += failed

        except asyncio.TimeoutError:
            Logger.warning("Batch send timeout - some messages may have been lost")
            # Cancel remaining tasks
            for task in tasks:
                if not task.done():
                    task.cancel()

        return successful

    async def run(self):
        Logger.info(f"Starting bulk simulation with {self.num_devices} devices...")

        # Test connection first
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.server_host, self.server_port),
                timeout=5.0
            )
            writer.close()
            await writer.wait_closed()
            Logger.success("Server connection test successful")
        except Exception as e:
            Logger.error(f"Cannot connect to server: {e}")
            return

        try:
            while True:
                cycle_start = time.time()

                # Generate all readings
                readings = self.generate_bulk_readings()

                # Process in batches with rate limiting
                total_sent = 0
                for i in range(0, len(readings), self.batch_size):
                    batch = readings[i:i + self.batch_size]
                    encrypted_messages = self.serialize_and_encrypt_batch(batch)
                    sent_count = await self.send_batch(encrypted_messages)
                    total_sent += sent_count

                    # Small delay between batches to prevent overwhelming
                    if i + self.batch_size < len(readings):
                        await asyncio.sleep(0.05)

                self.total_messages += total_sent
                self.cycle_count += 1

                # Log results
                if self.cycle_count % 5 == 0:
                    success_rate = (total_sent / self.num_devices) * 100 if self.num_devices > 0 else 0
                    Logger.success(f"Cycle {self.cycle_count}: {total_sent}/{self.num_devices} sent ({success_rate:.1f}%)")

                # Wait for next interval
                cycle_time = time.time() - cycle_start
                sleep_time = max(0, self.interval - cycle_time)
                if sleep_time > 0:
                    await asyncio.sleep(sleep_time)

        except KeyboardInterrupt:
            Logger.info("Shutting down simulator...")

def main():
    parser = argparse.ArgumentParser(description="Bulk Smart Grid Device Simulator")
    parser.add_argument("--devices", type=int, required=True, help="Number of devices to simulate")
    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", type=int, default=8890, help="Server port")
    parser.add_argument("--interval", type=int, default=3, help="Reporting interval (default: 3s)")
    parser.add_argument("--batch-size", type=int, default=100, help="Batch size for processing (default: 100)")
    parser.add_argument("--chunk-size", type=int, default=25, help="Chunk size for connections (default: 25)")
    parser.add_argument("--no-safety", action="store_true", help="Disable safety monitoring")

    args = parser.parse_args()

    simulator = BulkDeviceSimulator(
        args.devices, args.host, args.port, args.interval,
        args.batch_size, args.chunk_size, not args.no_safety
    )

    try:
        asyncio.run(simulator.run())
    except KeyboardInterrupt:
        Logger.info("Simulator stopped by user")

if __name__ == "__main__":
    main()
    