import os
from typing import Dict

# C++ Gateway connection settings
GATEWAY_HOST: str = os.getenv("MATCHING_ENGINE_GATEWAY_HOST", "127.0.0.1")
GATEWAY_PORT: int = int(os.getenv("MATCHING_ENGINE_GATEWAY_PORT", "12345"))
GATEWAY_TIMEOUT: float = float(os.getenv("MATCHING_ENGINE_GATEWAY_TIMEOUT", "2.0"))

# Instrument symbol to InstrumentId mapping (matching C++ gateway configuration)
SYMBOL_MAP: Dict[str, int] = {
    "AAPL": 0,
    "RELIANCE": 1,
    "INFY": 2,
    "TATASTEEL": 3,
}

ID_TO_SYMBOL_MAP: Dict[int, str] = {v: k for k, v in SYMBOL_MAP.items()}

# Validation limits
MIN_PRICE: int = 1
MAX_PRICE: int = 100000
MIN_QUANTITY: int = 1
MIN_ORDER_ID: int = 1
