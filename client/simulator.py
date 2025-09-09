#!/usr/bin/env python3
import asyncio
import argparse
import time
import struct
import random
from .aes_manager import AESManager
from .logger import Logger

class BatchedCipherGenerator:
    def __init__(self, rate_per_second, server_host="localhost", server_port=8890):
        self.rate = rate_per_second
        self.host = server_host
        self.port = server_port
        self.aes_manager = AESManager()
        self.message_count = 0
        self.total_errors = 0

        # Calculate optimal batch size and connection count
        self.messages_per_connection = min(50, max(5, rate_per_second // 20))
        self.max_concurrent_connections = min(20, max(5, rate_per_second // 100))

        # Pre-generate keys
        Logger.info(f"Pre-generating keys for rate of {rate_per_second} msgs/sec...")
        device_count = min(rate_per_second, 10000)
        for i in range(device_count):
            device_id = f"meter_{i:06d}"
            self.aes_manager.generate_device_key(device_id)
        Logger.success(f"Key generation complete (batch size: {self.messages_per_connection}, max connections: {self.max_concurrent_connections})")

    def create_cipher_text(self, device_id):
        timestamp = int(time.time()) & 0xFFFFFFFF
        device_num = int(device_id.split('_')[1]) & 0xFFFF

        voltage = 1200 + random.randint(-20, 20)
        current = 167 + random.randint(-10, 10)
        power = 2000 + random.randint(-100, 100)
        frequency = 60 + random.randint(-5, 5)

        data = struct.pack('>IHHHHB', timestamp, device_num, voltage, current, power, frequency)
        encrypted = self.aes_manager.encrypt_fixed_size(device_id, data)
        return f"{device_id}:16\n".encode() + encrypted

    async def send_batch_on_single_connection(self, messages):
        """Send multiple messages on a single connection"""
        sent_count = 0

        for attempt in range(3):
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port),
                    timeout=5.0
                )

                # Send all messages in this batch
                for msg in messages:
                    writer.write(msg)
                    sent_count += 1

                # Flush all at once
                await asyncio.wait_for(writer.drain(), timeout=10.0)

                # Close connection
                writer.close()
                await writer.wait_closed()

                return sent_count

            except Exception as e:
                self.total_errors += len(messages) - sent_count
                if attempt < 2:
                    await asyncio.sleep(0.5 * (attempt + 1))
                    sent_count = 0  # Reset for retry
                else:
                    Logger.error(f"Batch failed after retries: {e}")

        return sent_count

    async def send_all_messages(self, messages):
        """Split messages into batches and send with controlled concurrency"""

        # Split into batches
        batches = []
        for i in range(0, len(messages), self.messages_per_connection):
            batch = messages[i:i + self.messages_per_connection]
            batches.append(batch)

        # Control concurrency with semaphore
        semaphore = asyncio.Semaphore(self.max_concurrent_connections)

        async def send_batch_with_semaphore(batch):
            async with semaphore:
                return await self.send_batch_on_single_connection(batch)

        # Send all batches concurrently
        tasks = [send_batch_with_semaphore(batch) for batch in batches]
        results = await asyncio.gather(*tasks, return_exceptions=True)

        # Count successful sends
        total_sent = 0
        for result in results:
            if isinstance(result, int):
                total_sent += result
            else:
                Logger.error(f"Batch error: {result}")

        return total_sent

    async def run(self):
        Logger.info(f"Starting batched cipher generator at {self.rate} messages/second")

        # Test connection first
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=5.0
            )
            writer.close()
            await writer.wait_closed()
            Logger.success("Server connection test successful")
        except Exception as e:
            Logger.error(f"Cannot connect to server: {e}")
            return

        cycle_count = 0
        device_pool_size = min(self.rate, 10000)

        while True:
            start_time = time.time()
            cycle_count += 1

            # Generate messages for this cycle
            messages = []
            for i in range(self.rate):
                device_id = f"meter_{i % device_pool_size:06d}"
                cipher_text = self.create_cipher_text(device_id)
                messages.append(cipher_text)

            # Send all messages
            try:
                total_sent = await asyncio.wait_for(
                    self.send_all_messages(messages),
                    timeout=45.0
                )
            except asyncio.TimeoutError:
                Logger.warning("Send timeout - some messages lost")
                total_sent = 0

            self.message_count += total_sent

            # Log progress
            if cycle_count % 5 == 0:
                success_rate = (total_sent / self.rate) * 100 if self.rate > 0 else 0
                total_expected = cycle_count * self.rate
                total_error_rate = (self.total_errors / total_expected) * 100 if total_expected > 0 else 0

                Logger.success(f"Cycle {cycle_count}: {total_sent}/{self.rate} sent ({success_rate:.1f}%, cumulative errors: {total_error_rate:.1f}%)")

            # Maintain timing
            elapsed = time.time() - start_time
            sleep_time = max(0, 1.0 - elapsed)
            if sleep_time > 0:
                await asyncio.sleep(sleep_time)

def main():
    parser = argparse.ArgumentParser(description="Batched Cipher Text Generator")
    parser.add_argument("--rate", type=int, required=True, help="Messages per second to generate")
    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", type=int, default=8890, help="Server port")

    # Legacy compatibility
    parser.add_argument("--devices", type=int, help="Legacy: use --rate instead")
    parser.add_argument("--interval", type=int, default=1, help="Legacy: always 1 second")
    parser.add_argument("--batch-size", type=int, help="Legacy: ignored")
    parser.add_argument("--chunk-size", type=int, help="Legacy: ignored")
    parser.add_argument("--no-safety", action="store_true", help="Legacy: ignored")

    args = parser.parse_args()

    # Handle legacy arguments
    if args.devices and not args.rate:
        rate = args.devices // args.interval
        Logger.warning(f"Using legacy --devices argument: {args.devices} devices / {args.interval}s = {rate} msgs/sec")
    else:
        rate = args.rate

    generator = BatchedCipherGenerator(rate, args.host, args.port)

    try:
        asyncio.run(generator.run())
    except KeyboardInterrupt:
        Logger.info("Generator stopped by user")

if __name__ == "__main__":
    main()
    