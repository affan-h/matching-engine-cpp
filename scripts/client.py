"""
Binary TCP Protocol Reference Client for the Matching Engine.

Implements the explicit big-endian wire protocol:
- Frame Header: [2-byte payload_len (BE)] [1-byte msg_type]
- Messages:
    - 0x01 New Limit Order  (14 bytes payload)
    - 0x02 New Market Order (9 bytes payload)
    - 0x03 Cancel Order     (12 bytes payload)
    - 0x04 Modify Order     (20 bytes payload)
"""

import sys
import socket
import struct
import time
from enum import IntEnum


class MessageType(IntEnum):
    NEW_LIMIT_ORDER = 0x01
    NEW_MARKET_ORDER = 0x02
    CANCEL_ORDER = 0x03
    MODIFY_ORDER = 0x04


class Side(IntEnum):
    BUY = 0
    SELL = 1


class TimeInForce(IntEnum):
    GTC = 0
    IOC = 1
    FOK = 2


def encode_limit_order(
    instrument_id: int,
    side: int,
    price: int,
    quantity: int,
    tif: int = TimeInForce.GTC
) -> bytes:
    """Encodes a New Limit Order frame (17 bytes total)."""
    payload_len = 14
    msg_type = MessageType.NEW_LIMIT_ORDER
    return struct.pack("!H B I B I I B", payload_len, msg_type, instrument_id, side, price, quantity, tif)


def encode_market_order(
    instrument_id: int,
    side: int,
    quantity: int
) -> bytes:
    """Encodes a New Market Order frame (12 bytes total)."""
    payload_len = 9
    msg_type = MessageType.NEW_MARKET_ORDER
    return struct.pack("!H B I B I", payload_len, msg_type, instrument_id, side, quantity)


def encode_cancel_order(
    instrument_id: int,
    order_id: int
) -> bytes:
    """Encodes a Cancel Order frame (15 bytes total)."""
    payload_len = 12
    msg_type = MessageType.CANCEL_ORDER
    return struct.pack("!H B I Q", payload_len, msg_type, instrument_id, order_id)


def encode_modify_order(
    instrument_id: int,
    order_id: int,
    new_price: int,
    new_quantity: int
) -> bytes:
    """Encodes a Modify Order frame (23 bytes total)."""
    payload_len = 20
    msg_type = MessageType.MODIFY_ORDER
    return struct.pack("!H B I Q I I", payload_len, msg_type, instrument_id, order_id, new_price, new_quantity)


class ExchangeClient:
    """Client for connecting and sending binary orders to the TCP Gateway."""

    def __init__(self, host: str = "127.0.0.1", port: int = 12345):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))

    def send_limit_order(self, instrument_id: int, side: Side, price: int, quantity: int, tif: TimeInForce = TimeInForce.GTC):
        frame = encode_limit_order(instrument_id, side, price, quantity, tif)
        self.sock.sendall(frame)

    def send_market_order(self, instrument_id: int, side: Side, quantity: int):
        frame = encode_market_order(instrument_id, side, quantity)
        self.sock.sendall(frame)

    def send_cancel_order(self, instrument_id: int, order_id: int):
        frame = encode_cancel_order(instrument_id, order_id)
        self.sock.sendall(frame)

    def send_modify_order(self, instrument_id: int, order_id: int, new_price: int, new_quantity: int):
        frame = encode_modify_order(instrument_id, order_id, new_price, new_quantity)
        self.sock.sendall(frame)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--demo":
        port = int(sys.argv[2]) if len(sys.argv) > 2 else 12345
        print(f"[Python Client] Connecting to TCP Gateway at 127.0.0.1:{port}...")
        client = ExchangeClient(port=port)
        client.connect()

        print("[Python Client] Sending resting Buy Limit: AAPL (0) @ $150, qty 10...")
        client.send_limit_order(0, Side.BUY, 150, 10, TimeInForce.GTC)
        time.sleep(0.1)

        print("[Python Client] Sending matching Sell Limit: AAPL (0) @ $150, qty 10...")
        client.send_limit_order(0, Side.SELL, 150, 10, TimeInForce.GTC)
        time.sleep(0.1)

        print("[Python Client] Sending Market Order: INFY (2) SELL, qty 50...")
        client.send_market_order(2, Side.SELL, 50)
        time.sleep(0.1)

        print("[Python Client] Closing connection.")
        client.close()
        print("[Python Client] Demo completed successfully.")
    else:
        # Self-test encoding
        limit_frame = encode_limit_order(1, Side.BUY, 100, 10, TimeInForce.GTC)
        print(f"Limit order frame ({len(limit_frame)} bytes): {limit_frame.hex()}")
        assert len(limit_frame) == 17

        market_frame = encode_market_order(1, Side.SELL, 5)
        print(f"Market order frame ({len(market_frame)} bytes): {market_frame.hex()}")
        assert len(market_frame) == 12

        cancel_frame = encode_cancel_order(1, 42)
        print(f"Cancel order frame ({len(cancel_frame)} bytes): {cancel_frame.hex()}")
        assert len(cancel_frame) == 15

        modify_frame = encode_modify_order(1, 42, 105, 8)
        print(f"Modify order frame ({len(modify_frame)} bytes): {modify_frame.hex()}")
        assert len(modify_frame) == 23

        print("All python encoder self-tests passed!")
