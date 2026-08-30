import pytest
import subprocess
import time
import socket
from typing import Optional, Dict, Any, List
from fastapi.testclient import TestClient
from api.main import app, get_gateway_client
from api.tcp_client import GatewayTcpClient, GatewayUnavailableError
from api.config import SYMBOL_MAP


class MockGatewayTcpClient(GatewayTcpClient):
    """In-memory mock gateway client for isolated unit testing."""

    def __init__(self, should_fail: bool = False):
        super().__init__(host="127.0.0.1", port=9999)
        self.should_fail = should_fail
        self.submitted_events = []
        self.mock_books: Dict[str, Dict[str, Any]] = {
            "AAPL": {
                "symbol": "AAPL",
                "instrument_id": 0,
                "sequence": 1,
                "timestamp": 123456789,
                "bids": [{"price": 150, "quantity": 10}],
                "asks": [{"price": 155, "quantity": 20}],
            }
        }
        self.mock_trades: Dict[str, List[Dict[str, Any]]] = {
            "AAPL": [
                {
                    "trade_id": 1,
                    "symbol": "AAPL",
                    "buy_order_id": 10,
                    "sell_order_id": 20,
                    "price": 150,
                    "quantity": 10,
                    "aggressor_side": "buy",
                    "timestamp": 123456789,
                }
            ]
        }
        self.mock_orders: Dict[int, Dict[str, Any]] = {
            42: {
                "order_id": 42,
                "symbol": "AAPL",
                "instrument_id": 0,
                "side": "buy",
                "price": 150,
                "original_quantity": 10,
                "remaining_quantity": 0,
                "filled_quantity": 10,
                "status": "FILLED",
                "timestamp": 123456789,
            }
        }

    def connect(self):
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway is unreachable")

    def _send_bytes(self, data: bytes):
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway connection failure")
        self.submitted_events.append(data)

    def query_book(self, symbol: str) -> Dict[str, Any]:
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway query failure")
        return self.mock_books.get(symbol, {
            "symbol": symbol,
            "instrument_id": SYMBOL_MAP[symbol],
            "sequence": 0,
            "timestamp": 0,
            "bids": [],
            "asks": []
        })

    def query_trades(self, symbol: str, limit: int = 50) -> List[Dict[str, Any]]:
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway query failure")
        return self.mock_trades.get(symbol, [])[:limit]

    def query_order(self, order_id: int) -> Optional[Dict[str, Any]]:
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway query failure")
        return self.mock_orders.get(order_id)

    def check_health(self) -> bool:
        return not self.should_fail


@pytest.fixture
def mock_client():
    client = MockGatewayTcpClient(should_fail=False)
    app.dependency_overrides[get_gateway_client] = lambda: client
    yield client
    app.dependency_overrides.clear()


@pytest.fixture
def failing_mock_client():
    client = MockGatewayTcpClient(should_fail=True)
    app.dependency_overrides[get_gateway_client] = lambda: client
    yield client
    app.dependency_overrides.clear()


@pytest.fixture
def api_test_client():
    return TestClient(app)


# ─────────────────────────────────────────────
# Unit Tests with Mock Gateway
# ─────────────────────────────────────────────

def test_valid_limit_order(api_test_client, mock_client):
    """Test 1: Valid Limit Order submission."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "price": 150,
        "quantity": 10,
        "time_in_force": "GTC"
    })
    assert response.status_code == 202
    data = response.json()
    assert data["status"] == "ACCEPTED"
    assert data["symbol"] == "AAPL"
    assert data["instrument_id"] == 0
    assert data["side"] == "buy"
    assert data["order_type"] == "limit"
    assert data["price"] == 150
    assert data["quantity"] == 10
    assert data["time_in_force"] == "GTC"
    assert len(mock_client.submitted_events) == 1


def test_valid_market_order(api_test_client, mock_client):
    """Test 2: Valid Market Order submission."""
    response = api_test_client.post("/orders", json={
        "symbol": "RELIANCE",
        "side": "sell",
        "order_type": "market",
        "quantity": 25
    })
    assert response.status_code == 202
    data = response.json()
    assert data["status"] == "ACCEPTED"
    assert data["symbol"] == "RELIANCE"
    assert data["instrument_id"] == 1
    assert data["side"] == "sell"
    assert data["order_type"] == "market"
    assert data["time_in_force"] == "IOC"
    assert len(mock_client.submitted_events) == 1


def test_ioc_limit_order(api_test_client, mock_client):
    """Test 3: IOC Limit Order."""
    response = api_test_client.post("/orders", json={
        "symbol": "INFY",
        "side": "buy",
        "order_type": "limit",
        "price": 1400,
        "quantity": 5,
        "time_in_force": "IOC"
    })
    assert response.status_code == 202
    assert response.json()["time_in_force"] == "IOC"


def test_fok_limit_order(api_test_client, mock_client):
    """Test 4: FOK Limit Order."""
    response = api_test_client.post("/orders", json={
        "symbol": "TATASTEEL",
        "side": "sell",
        "order_type": "limit",
        "price": 140,
        "quantity": 50,
        "time_in_force": "FOK"
    })
    assert response.status_code == 202
    assert response.json()["time_in_force"] == "FOK"


def test_invalid_symbol(api_test_client, mock_client):
    """Test 5: Rejection of unknown instrument symbol."""
    response = api_test_client.post("/orders", json={
        "symbol": "UNKNOWN_STOCK",
        "side": "buy",
        "order_type": "limit",
        "price": 100,
        "quantity": 10
    })
    assert response.status_code == 422


def test_invalid_side(api_test_client, mock_client):
    """Test 6: Rejection of invalid side."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "hold",
        "order_type": "limit",
        "price": 100,
        "quantity": 10
    })
    assert response.status_code == 422


def test_invalid_quantity(api_test_client, mock_client):
    """Test 7: Rejection of zero or negative quantity."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "price": 100,
        "quantity": 0
    })
    assert response.status_code == 422


def test_invalid_price(api_test_client, mock_client):
    """Test 8: Rejection of zero price and missing price for limit order."""
    r1 = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "price": 0,
        "quantity": 10
    })
    assert r1.status_code == 422

    r2 = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "quantity": 10
    })
    assert r2.status_code == 422


def test_cancel_order(api_test_client, mock_client):
    """Test 9: Cancel order submission."""
    response = api_test_client.delete("/orders/42?symbol=AAPL")
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "ACCEPTED"
    assert data["symbol"] == "AAPL"
    assert data["order_id"] == 42
    assert len(mock_client.submitted_events) == 1


def test_modify_order(api_test_client, mock_client):
    """Test 10: Modify order submission."""
    response = api_test_client.patch("/orders/42", json={
        "symbol": "AAPL",
        "new_price": 105,
        "new_quantity": 8
    })
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "ACCEPTED"
    assert data["order_id"] == 42
    assert data["new_price"] == 105
    assert data["new_quantity"] == 8
    assert len(mock_client.submitted_events) == 1


def test_gateway_unavailable(api_test_client, failing_mock_client):
    """Test 11: Gateway unreachable returns 503."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "price": 150,
        "quantity": 10
    })
    assert response.status_code == 503
    assert response.json()["error"] == "GatewayUnavailable"


def test_book_endpoint(api_test_client, mock_client):
    """Test 12: Order book endpoint for known and unknown symbols."""
    r_ok = api_test_client.get("/book/AAPL")
    assert r_ok.status_code == 200
    data = r_ok.json()
    assert data["symbol"] == "AAPL"
    assert data["instrument_id"] == 0
    assert len(data["bids"]) == 1
    assert data["bids"][0]["price"] == 150
    assert len(data["asks"]) == 1
    assert data["asks"][0]["price"] == 155

    r_err = api_test_client.get("/book/UNKNOWN")
    assert r_err.status_code == 404


def test_trades_endpoint(api_test_client, mock_client):
    """Test 13: Trades history endpoint."""
    r_ok = api_test_client.get("/trades/AAPL?limit=10")
    assert r_ok.status_code == 200
    data = r_ok.json()
    assert data["symbol"] == "AAPL"
    assert data["instrument_id"] == 0
    assert len(data["trades"]) == 1
    assert data["trades"][0]["trade_id"] == 1
    assert data["trades"][0]["price"] == 150

    r_err = api_test_client.get("/trades/UNKNOWN")
    assert r_err.status_code == 404


def test_orders_endpoint(api_test_client, mock_client):
    """Test 14: Order query endpoint for existing and nonexistent orders."""
    r_ok = api_test_client.get("/orders/42")
    assert r_ok.status_code == 200
    data = r_ok.json()
    assert data["order_id"] == 42
    assert data["symbol"] == "AAPL"
    assert data["status"] == "FILLED"
    assert data["filled_quantity"] == 10

    r_err = api_test_client.get("/orders/9999")
    assert r_err.status_code == 404


def test_health_endpoint(api_test_client, mock_client):
    """Test 15: Health endpoint when gateway is healthy vs degraded."""
    r_healthy = api_test_client.get("/health")
    assert r_healthy.status_code == 200
    assert r_healthy.json()["status"] == "healthy"
    assert r_healthy.json()["gateway"]["connected"] is True


def test_health_endpoint_degraded(api_test_client, failing_mock_client):
    """Test 16: Health endpoint degraded."""
    r_degraded = api_test_client.get("/health")
    assert r_degraded.status_code == 503
    assert r_degraded.json()["status"] == "degraded"
    assert r_degraded.json()["gateway"]["connected"] is False


def test_cancel_order_invalid_symbol(api_test_client, mock_client):
    """Test 17: Cancel with invalid symbol returns 400."""
    response = api_test_client.delete("/orders/1?symbol=INVALID_SYMBOL")
    assert response.status_code == 400
    assert "Unknown symbol" in response.json()["detail"]


def test_cancel_order_invalid_order_id(api_test_client, mock_client):
    """Test 18: Cancel with order_id=0 returns 422."""
    response = api_test_client.delete("/orders/0?symbol=AAPL")
    assert response.status_code == 422


def test_modify_order_invalid_inputs(api_test_client, mock_client):
    """Test 19: Modify with invalid order_id, price, qty, or symbol."""
    r1 = api_test_client.patch("/orders/0", json={
        "symbol": "AAPL", "new_price": 100, "new_quantity": 10
    })
    assert r1.status_code == 422

    r2 = api_test_client.patch("/orders/1", json={
        "symbol": "AAPL", "new_price": 0, "new_quantity": 10
    })
    assert r2.status_code == 422

    r3 = api_test_client.patch("/orders/1", json={
        "symbol": "AAPL", "new_price": 100, "new_quantity": 0
    })
    assert r3.status_code == 422

    r4 = api_test_client.patch("/orders/1", json={
        "symbol": "NONEXISTENT", "new_price": 100, "new_quantity": 10
    })
    assert r4.status_code == 422


def test_market_order_with_price_rejected(api_test_client, mock_client):
    """Test 20: Market order specifying non-zero price is rejected."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "market",
        "price": 100,
        "quantity": 10
    })
    assert response.status_code == 422


# ─────────────────────────────────────────────
# Real End-to-End Integration Test (Command & Query Plane)
# ─────────────────────────────────────────────

def get_free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_for_port(port: int, host: str = "127.0.0.1", timeout: float = 3.0) -> bool:
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.2)
            s.connect((host, port))
            s.close()
            return True
        except Exception:
            time.sleep(0.05)
    return False


def test_real_gateway_end_to_end():
    """
    Test 21: Full End-to-End Command and Query Plane:
      1. POST /orders (Limit Buy) -> MatchingEngine -> SPSC Outbound -> Projector -> ReadModel
      2. GET /book/AAPL -> ReadModel L2 snapshot
      3. GET /orders/1 -> ReadModel order state
      4. POST /orders (Matching Limit Sell) -> Trade executed
      5. GET /trades/AAPL -> ReadModel trade history
      6. GET /orders/1 and GET /orders/2 -> ReadModel FILLED states
    """
    test_port = get_free_port()
    proc = subprocess.Popen(
        ["./gateway", str(test_port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert wait_for_port(test_port), f"C++ Gateway failed to listen on port {test_port}"

    try:
        real_client = GatewayTcpClient(host="127.0.0.1", port=test_port)
        app.dependency_overrides[get_gateway_client] = lambda: real_client
        http_client = TestClient(app)

        # 1. Health Check
        health_resp = http_client.get("/health")
        assert health_resp.status_code == 200
        assert health_resp.json()["status"] == "healthy"

        # 2. Submit Limit Buy AAPL 150 @ 10
        buy_resp = http_client.post("/orders", json={
            "symbol": "AAPL",
            "side": "buy",
            "order_type": "limit",
            "price": 150,
            "quantity": 10,
            "time_in_force": "GTC"
        })
        assert buy_resp.status_code == 202
        assert buy_resp.json()["status"] == "ACCEPTED"

        time.sleep(0.1)

        # 3. Query L2 Book for AAPL
        book_resp = http_client.get("/book/AAPL")
        assert book_resp.status_code == 200
        book_data = book_resp.json()
        assert book_data["symbol"] == "AAPL"
        assert len(book_data["bids"]) >= 1
        assert book_data["bids"][0]["price"] == 150
        assert book_data["bids"][0]["quantity"] == 10

        # 4. Query Order 1 Status (Resting / New)
        order1_resp = http_client.get("/orders/1")
        assert order1_resp.status_code == 200
        o1_data = order1_resp.json()
        assert o1_data["order_id"] == 1
        assert o1_data["symbol"] == "AAPL"
        assert o1_data["status"] == "NEW"
        assert o1_data["remaining_quantity"] == 10

        # 5. Submit matching Limit Sell AAPL 150 @ 10
        sell_resp = http_client.post("/orders", json={
            "symbol": "AAPL",
            "side": "sell",
            "order_type": "limit",
            "price": 150,
            "quantity": 10,
            "time_in_force": "GTC"
        })
        assert sell_resp.status_code == 202

        time.sleep(0.1)

        # 6. Query Trade History for AAPL
        trades_resp = http_client.get("/trades/AAPL")
        assert trades_resp.status_code == 200
        trades_data = trades_resp.json()
        assert len(trades_data["trades"]) >= 1
        latest_trade = trades_data["trades"][0]
        assert latest_trade["price"] == 150
        assert latest_trade["quantity"] == 10
        assert latest_trade["buy_order_id"] == 1
        assert latest_trade["sell_order_id"] == 2

        # 7. Query Order 1 & Order 2 Status (Now FILLED)
        o1_filled = http_client.get("/orders/1").json()
        assert o1_filled["status"] == "FILLED"
        assert o1_filled["remaining_quantity"] == 0
        assert o1_filled["filled_quantity"] == 10

        o2_filled = http_client.get("/orders/2").json()
        assert o2_filled["status"] == "FILLED"
        assert o2_filled["remaining_quantity"] == 0
        assert o2_filled["filled_quantity"] == 10

    finally:
        app.dependency_overrides.clear()
        real_client.close()
        proc.terminate()
        proc.wait(timeout=2)
