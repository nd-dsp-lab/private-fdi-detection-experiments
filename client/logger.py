from datetime import datetime

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
        