"""
Binary TCP Protocol Reference Client for the Matching Engine.

Implements the explicit big-endian wire protocol:
- Frame Header: [2-byte payload_len (BE)] [1-byte msg_type]
- Messages:
    - 0x01 New Limit Order  (14 or 22 bytes payload)
    - 0x02 New Market Order (9 or 17 bytes payload)
    - 0x03 Cancel Order     (12 or 20 bytes payload)
    - 0x04 Modify Order     (20 or 28 bytes payload)
    - 0x05 Ping             (8 bytes payload)
    - 0x06 Pong             (8 bytes payload)
    - 0x10 Query Book       (4 bytes payload)
    - 0x11 Query Trades     (8 bytes payload)
    - 0x12 Query Order      (8 or 9 bytes payload)
    - 0x13 Query Stats      (0 bytes payload)
    - 0x80 Query Book Resp
    - 0x81 Query Trades Resp
    - 0x82 Query Order Resp (56 bytes payload)
    - 0x83 Query Stats Resp (64 bytes payload)
"""

import sys
import socket
import struct
import time
from enum import IntEnum
from typing import Dict, Any, List, Optional


class MessageType(IntEnum):
    HEARTBEAT           = 0x00
    NEW_LIMIT_ORDER     = 0x01
    NEW_MARKET_ORDER    = 0x02
    CANCEL_ORDER        = 0x03
    MODIFY_ORDER        = 0x04
    PING                = 0x05
    PONG                = 0x06

    QUERY_BOOK          = 0x10
    QUERY_TRADES        = 0x11
    QUERY_ORDER         = 0x12
    QUERY_STATS         = 0x13

    QUERY_BOOK_RESP     = 0x80
    QUERY_TRADES_RESP   = 0x81
    QUERY_ORDER_RESP    = 0x82
    QUERY_STATS_RESP    = 0x83
    QUERY_ERROR_RESP    = 0x8F


class Side(IntEnum):
    BUY = 0
    SELL = 1


class TimeInForce(IntEnum):
    GTC = 0
    IOC = 1
    FOK = 2


class OrderStatus(IntEnum):
    NEW              = 1
    PARTIALLY_FILLED = 2
    FILLED           = 3
    CANCELLED        = 4
    REJECTED         = 5


class RejectCode(IntEnum):
    NONE                       = 0
    UNKNOWN_INSTRUMENT         = 1
    INVALID_PRICE_QTY          = 2
    INSUFFICIENT_LIQUIDITY_FOK = 3
    ORDER_NOT_FOUND            = 4
    QUEUE_FULL                 = 5


def encode_limit_order(
    instrument_id: int,
    side: int,
    price: int,
    quantity: int,
    tif: int = TimeInForce.GTC,
    client_order_id: int = 0
) -> bytes:
    """Encodes a New Limit Order frame (17 or 25 bytes total)."""
    if client_order_id == 0:
        payload_len = 14
        msg_type = MessageType.NEW_LIMIT_ORDER
        return struct.pack("!H B I B I I B", payload_len, msg_type, instrument_id, side, price, quantity, tif)
    else:
        payload_len = 22
        msg_type = MessageType.NEW_LIMIT_ORDER
        return struct.pack("!H B Q I B I I B", payload_len, msg_type, client_order_id, instrument_id, side, price, quantity, tif)


def encode_market_order(
    instrument_id: int,
    side: int,
    quantity: int,
    client_order_id: int = 0
) -> bytes:
    """Encodes a New Market Order frame (12 or 20 bytes total)."""
    if client_order_id == 0:
        payload_len = 9
        msg_type = MessageType.NEW_MARKET_ORDER
        return struct.pack("!H B I B I", payload_len, msg_type, instrument_id, side, quantity)
    else:
        payload_len = 17
        msg_type = MessageType.NEW_MARKET_ORDER
        return struct.pack("!H B Q I B I", payload_len, msg_type, client_order_id, instrument_id, side, quantity)


def encode_cancel_order(
    instrument_id: int,
    order_id: int,
    client_order_id: int = 0
) -> bytes:
    """Encodes a Cancel Order frame (15 or 23 bytes total)."""
    if client_order_id == 0:
        payload_len = 12
        msg_type = MessageType.CANCEL_ORDER
        return struct.pack("!H B I Q", payload_len, msg_type, instrument_id, order_id)
    else:
        payload_len = 20
        msg_type = MessageType.CANCEL_ORDER
        return struct.pack("!H B Q I Q", payload_len, msg_type, client_order_id, instrument_id, order_id)


def encode_modify_order(
    instrument_id: int,
    order_id: int,
    new_price: int,
    new_quantity: int,
    client_order_id: int = 0
) -> bytes:
    """Encodes a Modify Order frame (23 or 31 bytes total)."""
    if client_order_id == 0:
        payload_len = 20
        msg_type = MessageType.MODIFY_ORDER
        return struct.pack("!H B I Q I I", payload_len, msg_type, instrument_id, order_id, new_price, new_quantity)
    else:
        payload_len = 28
        msg_type = MessageType.MODIFY_ORDER
        return struct.pack("!H B Q I Q I I", payload_len, msg_type, client_order_id, instrument_id, order_id, new_price, new_quantity)


def encode_ping(nonce: int = 0) -> bytes:
    """Encodes a Ping frame (11 bytes total)."""
    payload_len = 8
    msg_type = MessageType.PING
    return struct.pack("!H B Q", payload_len, msg_type, nonce)


def encode_query_book(instrument_id: int) -> bytes:
    """Encodes a Query Book frame (7 bytes total)."""
    payload_len = 4
    msg_type = MessageType.QUERY_BOOK
    return struct.pack("!H B I", payload_len, msg_type, instrument_id)


def encode_query_trades(instrument_id: int, limit: int = 50) -> bytes:
    """Encodes a Query Trades frame (11 bytes total)."""
    payload_len = 8
    msg_type = MessageType.QUERY_TRADES
    return struct.pack("!H B I I", payload_len, msg_type, instrument_id, limit)


def encode_query_order(order_id: int, by_client_id: bool = False) -> bytes:
    """Encodes a Query Order frame (11 or 12 bytes total)."""
    if not by_client_id:
        payload_len = 8
        msg_type = MessageType.QUERY_ORDER
        return struct.pack("!H B Q", payload_len, msg_type, order_id)
    else:
        payload_len = 9
        msg_type = MessageType.QUERY_ORDER
        return struct.pack("!H B B Q", payload_len, msg_type, 1, order_id)


def encode_query_stats() -> bytes:
    """Encodes a Query Stats frame (3 bytes total)."""
    payload_len = 0
    msg_type = MessageType.QUERY_STATS
    return struct.pack("!H B", payload_len, msg_type)


def decode_query_book_response(payload: bytes) -> Dict[str, Any]:
    """Decodes a QueryBookResponse payload."""
    inst_id, seq, ts, b_count, a_count = struct.unpack_from("!I Q Q B B", payload, 0)
    offset = 22

    bids: List[Dict[str, int]] = []
    for _ in range(b_count):
        px, qty = struct.unpack_from("!I I", payload, offset)
        bids.append({"price": px, "quantity": qty})
        offset += 8

    asks: List[Dict[str, int]] = []
    for _ in range(a_count):
        px, qty = struct.unpack_from("!I I", payload, offset)
        asks.append({"price": px, "quantity": qty})
        offset += 8

    return {
        "instrument_id": inst_id,
        "sequence": seq,
        "timestamp": ts,
        "bids": bids,
        "asks": asks,
    }


def decode_query_trades_response(payload: bytes) -> Dict[str, Any]:
    """Decodes a QueryTradesResponse payload."""
    inst_id, count = struct.unpack_from("!I H", payload, 0)
    offset = 6

    trades: List[Dict[str, Any]] = []
    for _ in range(count):
        tid, buy_id, sell_id, px, qty, side_raw, ts = struct.unpack_from("!Q Q Q I I B Q", payload, offset)
        trades.append({
            "trade_id": tid,
            "buy_order_id": buy_id,
            "sell_order_id": sell_id,
            "price": px,
            "quantity": qty,
            "aggressor_side": "buy" if side_raw == 0 else "sell",
            "timestamp": ts,
        })
        offset += 41

    return {
        "instrument_id": inst_id,
        "trades": trades,
    }


def decode_query_order_response(payload: bytes) -> Optional[Dict[str, Any]]:
    """Decodes a QueryOrderResponse payload."""
    found, oid, cl_oid, inst_id, side_raw, px, orig_qty, rem_qty, filled_qty, status_raw, rej_raw, ts, seq = struct.unpack_from("!B Q Q I B I I I I B B Q Q", payload, 0)
    if not found:
        return None

    status_map = {
        1: "NEW",
        2: "PARTIALLY_FILLED",
        3: "FILLED",
        4: "CANCELLED",
        5: "REJECTED"
    }

    reject_map = {
        0: "NONE",
        1: "UNKNOWN_INSTRUMENT",
        2: "INVALID_PRICE_QTY",
        3: "INSUFFICIENT_LIQUIDITY_FOK",
        4: "ORDER_NOT_FOUND",
        5: "QUEUE_FULL"
    }

    return {
        "found": True,
        "order_id": oid,
        "client_order_id": cl_oid,
        "instrument_id": inst_id,
        "side": "buy" if side_raw == 0 else "sell",
        "price": px,
        "original_quantity": orig_qty,
        "remaining_quantity": rem_qty,
        "filled_quantity": filled_qty,
        "status": status_map.get(status_raw, "UNKNOWN"),
        "reject_code": reject_map.get(rej_raw, "UNKNOWN"),
        "timestamp": ts,
        "sequence": seq,
    }


def decode_query_stats_response(payload: bytes) -> Dict[str, Any]:
    """Decodes a QueryStatsResponse payload."""
    trades, vol, accepted, filled, cancelled, rejected, last_seq, tracked_ords, reg_syms = struct.unpack_from("!Q Q Q Q Q Q Q I I", payload, 0)
    return {
        "total_trades": trades,
        "total_volume": vol,
        "total_orders_accepted": accepted,
        "total_orders_filled": filled,
        "total_orders_cancelled": cancelled,
        "total_orders_rejected": rejected,
        "last_sequence": last_seq,
        "tracked_orders_count": tracked_ords,
        "registered_symbols_count": reg_syms,
    }


class ExchangeClient:
    """Client for connecting and sending binary orders/queries to the TCP Gateway."""

    def __init__(self, host: str = "127.0.0.1", port: int = 12345):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))

    def send_limit_order(self, instrument_id: int, side: Side, price: int, quantity: int, tif: TimeInForce = TimeInForce.GTC, client_order_id: int = 0):
        frame = encode_limit_order(instrument_id, side, price, quantity, tif, client_order_id)
        self.sock.sendall(frame)

    def send_market_order(self, instrument_id: int, side: Side, quantity: int, client_order_id: int = 0):
        frame = encode_market_order(instrument_id, side, quantity, client_order_id)
        self.sock.sendall(frame)

    def send_cancel_order(self, instrument_id: int, order_id: int, client_order_id: int = 0):
        frame = encode_cancel_order(instrument_id, order_id, client_order_id)
        self.sock.sendall(frame)

    def send_modify_order(self, instrument_id: int, order_id: int, new_price: int, new_quantity: int, client_order_id: int = 0):
        frame = encode_modify_order(instrument_id, order_id, new_price, new_quantity, client_order_id)
        self.sock.sendall(frame)

    def ping(self, nonce: int = 0) -> int:
        frame = encode_ping(nonce)
        self.sock.sendall(frame)
        msg_type, payload = self._read_response_frame()
        assert msg_type == MessageType.PONG
        (resp_nonce,) = struct.unpack("!Q", payload)
        return resp_nonce

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

    def query_book(self, instrument_id: int) -> Dict[str, Any]:
        frame = encode_query_book(instrument_id)
        self.sock.sendall(frame)
        msg_type, payload = self._read_response_frame()
        assert msg_type == MessageType.QUERY_BOOK_RESP
        return decode_query_book_response(payload)

    def query_trades(self, instrument_id: int, limit: int = 50) -> Dict[str, Any]:
        frame = encode_query_trades(instrument_id, limit)
        self.sock.sendall(frame)
        msg_type, payload = self._read_response_frame()
        assert msg_type == MessageType.QUERY_TRADES_RESP
        return decode_query_trades_response(payload)

    def query_order(self, order_id: int, by_client_id: bool = False) -> Optional[Dict[str, Any]]:
        frame = encode_query_order(order_id, by_client_id=by_client_id)
        self.sock.sendall(frame)
        msg_type, payload = self._read_response_frame()
        assert msg_type == MessageType.QUERY_ORDER_RESP
        return decode_query_order_response(payload)

    def query_stats(self) -> Dict[str, Any]:
        frame = encode_query_stats()
        self.sock.sendall(frame)
        msg_type, payload = self._read_response_frame()
        assert msg_type == MessageType.QUERY_STATS_RESP
        return decode_query_stats_response(payload)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None
