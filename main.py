import asyncio
import json
import time
import random
import struct
from datetime import datetime, timedelta
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding, hashes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.backends import default_backend
import numpy as np
from dataclasses import dataclass
from typing import Dict, List, Tuple
import multiprocessing as mp
import socket
import threading
import os

@dataclass
class MeterReading:
    device_id: str
    timestamp: float
    voltage: float
    current: float
    power: float
    frequency: float

class AESManager:
    """Handles AES encryption/decryption with per-device keys"""

    def __init__(self):
        self.device_keys: Dict[str, bytes] = {}

    def generate_device_key(self, device_id: str) -> bytes:
        """Generate unique AES key for device"""
        # Use device_id as salt for deterministic but unique keys
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

    def encrypt_data(self, device_id: str, data: bytes) -> bytes:
        """Encrypt data with device-specific key"""
        key = self.device_keys[device_id]
        iv = os.urandom(16)
        cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
        encryptor = cipher.encryptor()

        # Pad data to AES block size
        padder = padding.PKCS7(128).padder()
        padded_data = padder.update(data) + padder.finalize()

        encrypted = encryptor.update(padded_data) + encryptor.finalize()
        return iv + encrypted  # Prepend IV

    def decrypt_data(self, device_id: str, encrypted_data: bytes) -> bytes:
        """Decrypt data with device-specific key"""
        key = self.device_keys[device_id]
        iv = encrypted_data[:16]
        ciphertext = encrypted_data[16:]

        cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
        decryptor = cipher.decryptor()
        padded_data = decryptor.update(ciphertext) + decryptor.finalize()

        # Remove padding
        unpadder = padding.PKCS7(128).unpadder()
        data = unpadder.update(padded_data) + unpadder.finalize()
        return data

class SmartMeterSimulator:
    """Simulates realistic smart meter data patterns"""

    def __init__(self, device_id: str, base_consumption: float = 2000):
        self.device_id = device_id
        self.base_consumption = base_consumption  # Watts
        self.voltage_nominal = 120.0  # Volts
        self.frequency_nominal = 60.0  # Hz

    def generate_reading(self, timestamp: float) -> MeterReading:
        """Generate realistic meter reading with daily patterns"""
        dt = datetime.fromtimestamp(timestamp)
        hour = dt.hour

        # Daily consumption pattern (higher during day, lower at night)
        daily_factor = 0.7 + 0.3 * (1 + np.sin((hour - 6) * np.pi / 12))

        # Add some randomness
        noise_factor = random.uniform(0.9, 1.1)

        power = self.base_consumption * daily_factor * noise_factor
        voltage = self.voltage_nominal + random.uniform(-2, 2)
        frequency = self.frequency_nominal + random.uniform(-0.1, 0.1)
        current = power / voltage

        return MeterReading(
            device_id=self.device_id,
            timestamp=timestamp,
            voltage=voltage,
            current=current,
            power=power,
            frequency=frequency
        )

class EdgeDevice:
    """Simulates an edge device (smart meter)"""

    def __init__(self, device_id: str, server_host: str, server_port: int,
                 reporting_interval: int = 60):
        self.device_id = device_id
        self.server_host = server_host
        self.server_port = server_port
        self.reporting_interval = reporting_interval
        self.simulator = SmartMeterSimulator(device_id)
        self.aes_manager = AESManager()
        self.aes_manager.generate_device_key(device_id)

    def serialize_reading(self, reading: MeterReading) -> bytes:
        """Convert reading to bytes for transmission"""
        data = {
            'device_id': reading.device_id,
            'timestamp': reading.timestamp,
            'voltage': reading.voltage,
            'current': reading.current,
            'power': reading.power,
            'frequency': reading.frequency
        }
        return json.dumps(data).encode()

    async def run(self):
        """Main device loop - generate and send data"""
        print(f"Edge device {self.device_id} starting...")

        while True:
            try:
                # Generate reading
                timestamp = time.time()
                reading = self.simulator.generate_reading(timestamp)

                # Serialize and encrypt
                data = self.serialize_reading(reading)
                encrypted_data = self.aes_manager.encrypt_data(self.device_id, data)

                # Send to server
                await self.send_to_server(encrypted_data)

                # Wait for next interval
                await asyncio.sleep(self.reporting_interval)

            except Exception as e:
                print(f"Device {self.device_id} error: {e}")
                await asyncio.sleep(5)  # Brief pause before retry

    async def send_to_server(self, encrypted_data: bytes):
        """Send encrypted data to central server"""
        try:
            reader, writer = await asyncio.open_connection(
                self.server_host, self.server_port
            )

            # Send device ID and data length first
            header = f"{self.device_id}:{len(encrypted_data)}\n".encode()
            writer.write(header)
            writer.write(encrypted_data)
            await writer.drain()

            writer.close()
            await writer.wait_closed()

        except Exception as e:
            print(f"Device {self.device_id} send error: {e}")

class CentralServer:
    """Central processing server managing all edge devices"""

    def __init__(self, host: str = 'localhost', port: int = 8889):
        self.host = host
        self.port = port
        self.aes_manager = AESManager()
        self.device_count = 0
        self.readings_received = 0
        self.start_time = time.time()

    def register_device(self, device_id: str):
        """Register a new device and generate its key"""
        self.aes_manager.generate_device_key(device_id)
        self.device_count += 1
        print(f"Registered device {device_id} (total: {self.device_count})")

    def deserialize_reading(self, data: bytes) -> MeterReading:
        """Convert bytes back to reading object"""
        json_data = json.loads(data.decode())
        return MeterReading(**json_data)

    async def handle_client(self, reader, writer):
        """Handle incoming data from edge device"""
        try:
            # Read header (device_id:data_length)
            header_line = await reader.readline()
            header = header_line.decode().strip()
            device_id, data_length = header.split(':')
            data_length = int(data_length)

            # Register device if new
            if device_id not in self.aes_manager.device_keys:
                self.register_device(device_id)

            # Read encrypted data
            encrypted_data = await reader.read(data_length)

            # Decrypt and process
            decrypted_data = self.aes_manager.decrypt_data(device_id, encrypted_data)
            reading = self.deserialize_reading(decrypted_data)

            # Process reading (placeholder for FDI detection)
            await self.process_reading(reading)

            self.readings_received += 1

            # Print stats periodically
            if self.readings_received % 100 == 0:
                elapsed = time.time() - self.start_time
                rate = self.readings_received / elapsed
                print(f"Processed {self.readings_received} readings "
                      f"from {self.device_count} devices "
                      f"({rate:.1f} readings/sec)")

        except Exception as e:
            print(f"Server error handling client: {e}")
        finally:
            writer.close()
            await writer.wait_closed()

    async def process_reading(self, reading: MeterReading):
        """Process meter reading - placeholder for FDI detection"""
        # This is where you'll implement your FDI detection algorithms
        # For now, just basic validation
        if reading.power < 0 or reading.voltage < 100 or reading.voltage > 140:
            print(f"Anomaly detected in device {reading.device_id}: "
                  f"Power={reading.power:.1f}W, Voltage={reading.voltage:.1f}V")

    async def run(self):
        """Start the central server"""
        server = await asyncio.start_server(
            self.handle_client, self.host, self.port
        )

        addr = server.sockets[0].getsockname()
        print(f"Central server running on {addr[0]}:{addr[1]}")

        async with server:
            await server.serve_forever()

def run_edge_device(device_id: str, server_host: str, server_port: int,
                    reporting_interval: int):
    """Run edge device in separate process"""
    device = EdgeDevice(device_id, server_host, server_port, reporting_interval)
    asyncio.run(device.run())

def run_simulation(num_devices: int = 100, reporting_interval: int = 60):
    """Run complete simulation with multiple edge devices"""
    server_host = 'localhost'
    server_port = 8890

    # Start central server in separate process
    server = CentralServer(server_host, server_port)
    server_process = mp.Process(target=lambda: asyncio.run(server.run()))
    server_process.start()

    # Give server time to start
    time.sleep(2)

    # Start edge devices
    device_processes = []
    for i in range(num_devices):
        device_id = f"meter_{i:04d}"
        process = mp.Process(
            target=run_edge_device,
            args=(device_id, server_host, server_port, reporting_interval)
        )
        process.start()
        device_processes.append(process)

        # Stagger device starts to avoid overwhelming server
        time.sleep(0.1)

    print(f"Started {num_devices} edge devices with {reporting_interval}s intervals")

    try:
        # Let simulation run
        server_process.join()
    except KeyboardInterrupt:
        print("\nShutting down simulation...")
        server_process.terminate()
        for process in device_processes:
            process.terminate()

if __name__ == "__main__":
    # Run simulation with 50 devices reporting every 2 seconds
    run_simulation(num_devices=50, reporting_interval=2)
