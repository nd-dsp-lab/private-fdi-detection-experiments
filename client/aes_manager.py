from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding, hashes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.backends import default_backend

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

        cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
        encryptor = cipher.encryptor()

        padder = padding.PKCS7(128).padder()
        padded_data = padder.update(data) + padder.finalize()

        encrypted = encryptor.update(padded_data) + encryptor.finalize()
        return encrypted
