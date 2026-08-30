import socket
import struct
import threading
from typing import Optional, Dict, Any, List
from scripts.client import (
    encode_limit_order,
    encode_market_order,
    encode_cancel_order,
    encode_modify_order,
    encode_query_book,
    encode_query_trades,
    encode_query_order,
    decode_query_book_response,
    decode_query_trades_response,
    decode_query_order_response,
    MessageType,
    Side,
    TimeInForce,
)
from api.config import GATEWAY_HOST, GATEWAY_PORT, GATEWAY_TIMEOUT, SYMBOL_MAP, ID_TO_SYMBOL_MAP


class GatewayError(Exception):
    """Base exception for gateway communication failures."""
    pass


class GatewayUnavailableError(GatewayError):
    """Raised when the TCP gateway is unreachable or refuses connection."""
    pass


class GatewayTimeoutError(GatewayError):
    """Raised when a socket operation times out."""
    pass


class GatewayTcpClient:
    """Thread-safe, resilient TCP client for the C++ kqueue Matching Engine Gateway & Read Model."""

    def __init__(
        self,
        host: str = GATEWAY_HOST,
        port: int = GATEWAY_PORT,
        timeout: float = GATEWAY_TIMEOUT,
    ):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self._lock = threading.RLock()

    def connect(self):
        """Establish a TCP connection to the gateway."""
        with self._lock:
            if self.sock is not None:
                return
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(self.timeout)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.connect((self.host, self.port))
                self.sock = sock
            except (ConnectionRefusedError, socket.gaierror, TimeoutError, OSError) as e:
                self.sock = None
                raise GatewayUnavailableError(f"Cannot connect to C++ Gateway at {self.host}:{self.port} - {e}")

    def _send_bytes(self, data: bytes):
        """Send complete binary frame with automatic single-reconnect on disconnect."""
        with self._lock:
            for attempt in range(2):
                if self.sock is None:
                    self.connect()

                try:
                    self.sock.sendall(data)
                    return
                except (BrokenPipeError, ConnectionResetError, socket.timeout, OSError) as e:
                    self.close()
                    if attempt == 0:
                        continue  # Try reconnecting on next attempt
                    raise GatewayUnavailableError(f"Gateway connection lost during send: {e}")

    def _read_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionResetError("Gateway closed connection while reading")
            buf.extend(chunk)
        return bytes(buf)

    def _read_response_frame(self) -> tuple[int, bytes]:
        hdr = self._read_exact(3)
        payload_len, msg_type = struct.unpack("!H B", hdr)
        payload = self._read_exact(payload_len)
        return msg_type, payload

    # ─────────────────────────────────────────────
    # Command Submissions
    # ─────────────────────────────────────────────

    def submit_limit_order(
        self,
        symbol: str,
        side: str,
        price: int,
        quantity: int,
        tif: str = "GTC",
    ) -> int:
        inst_id = SYMBOL_MAP[symbol]
        w_side = Side.BUY if side.lower() == "buy" else Side.SELL
        tif_upper = tif.upper()
        if tif_upper == "IOC":
            w_tif = TimeInForce.IOC
        elif tif_upper == "FOK":
            w_tif = TimeInForce.FOK
        else:
            w_tif = TimeInForce.GTC

        frame = encode_limit_order(inst_id, w_side, price, quantity, w_tif)
        self._send_bytes(frame)
        return inst_id

    def submit_market_order(
        self,
        symbol: str,
        side: str,
        quantity: int,
    ) -> int:
        inst_id = SYMBOL_MAP[symbol]
        w_side = Side.BUY if side.lower() == "buy" else Side.SELL
        frame = encode_market_order(inst_id, w_side, quantity)
        self._send_bytes(frame)
        return inst_id

    def submit_cancel_order(
        self,
        symbol: str,
        order_id: int,
    ) -> int:
        inst_id = SYMBOL_MAP[symbol]
        frame = encode_cancel_order(inst_id, order_id)
        self._send_bytes(frame)
        return inst_id

    def submit_modify_order(
        self,
        symbol: str,
        order_id: int,
        new_price: int,
        new_quantity: int,
    ) -> int:
        inst_id = SYMBOL_MAP[symbol]
        frame = encode_modify_order(inst_id, order_id, new_price, new_quantity)
        self._send_bytes(frame)
        return inst_id

    # ─────────────────────────────────────────────
    # Query Operations (Read Model)
    # ─────────────────────────────────────────────

    def query_book(self, symbol: str) -> Dict[str, Any]:
        inst_id = SYMBOL_MAP[symbol]
        frame = encode_query_book(inst_id)
        with self._lock:
            for attempt in range(2):
                if self.sock is None:
                    self.connect()
                try:
                    self.sock.sendall(frame)
                    msg_type, payload = self._read_response_frame()
                    if msg_type == MessageType.QUERY_BOOK_RESP:
                        data = decode_query_book_response(payload)
                        data["symbol"] = symbol
                        return data
                    raise GatewayError(f"Unexpected response message type: {msg_type}")
                except (BrokenPipeError, ConnectionResetError, socket.timeout, OSError) as e:
                    self.close()
                    if attempt == 0:
                        continue
                    raise GatewayUnavailableError(f"Gateway connection lost during query: {e}")

    def query_trades(self, symbol: str, limit: int = 50) -> List[Dict[str, Any]]:
        inst_id = SYMBOL_MAP[symbol]
        frame = encode_query_trades(inst_id, limit)
        with self._lock:
            for attempt in range(2):
                if self.sock is None:
                    self.connect()
                try:
                    self.sock.sendall(frame)
                    msg_type, payload = self._read_response_frame()
                    if msg_type == MessageType.QUERY_TRADES_RESP:
                        data = decode_query_trades_response(payload)
                        for t in data["trades"]:
                            t["symbol"] = symbol
                        return data["trades"]
                    raise GatewayError(f"Unexpected response message type: {msg_type}")
                except (BrokenPipeError, ConnectionResetError, socket.timeout, OSError) as e:
                    self.close()
                    if attempt == 0:
                        continue
                    raise GatewayUnavailableError(f"Gateway connection lost during query: {e}")

    def query_order(self, order_id: int) -> Optional[Dict[str, Any]]:
        frame = encode_query_order(order_id)
        with self._lock:
            for attempt in range(2):
                if self.sock is None:
                    self.connect()
                try:
                    self.sock.sendall(frame)
                    msg_type, payload = self._read_response_frame()
                    if msg_type == MessageType.QUERY_ORDER_RESP:
                        data = decode_query_order_response(payload)
                        if data and data.get("found"):
                            inst_id = data["instrument_id"]
                            data["symbol"] = ID_TO_SYMBOL_MAP.get(inst_id, "UNKNOWN")
                            return data
                        return None
                    raise GatewayError(f"Unexpected response message type: {msg_type}")
                except (BrokenPipeError, ConnectionResetError, socket.timeout, OSError) as e:
                    self.close()
                    if attempt == 0:
                        continue
                    raise GatewayUnavailableError(f"Gateway connection lost during query: {e}")

    def check_health(self) -> bool:
        """Actively checks if the TCP gateway is accepting connections."""
        try:
            probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            probe.settimeout(0.5)
            probe.connect((self.host, self.port))
            probe.close()
            return True
        except Exception:
            return False

    def close(self):
        """Close the active client connection."""
        with self._lock:
            if self.sock:
                try:
                    self.sock.close()
                except Exception:
                    pass
                self.sock = None
