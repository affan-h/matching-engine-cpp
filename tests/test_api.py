import pytest
import subprocess
import time
import socket
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

    def connect(self):
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway is unreachable")

    def _send_bytes(self, data: bytes):
        if self.should_fail:
            raise GatewayUnavailableError("Mock gateway connection failure")
        self.submitted_events.append(data)

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
    # Zero price
    r1 = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "limit",
        "price": 0,
        "quantity": 10
    })
    assert r1.status_code == 422

    # Missing price for limit
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
    # Known symbol
    r_ok = api_test_client.get("/book/AAPL")
    assert r_ok.status_code == 200
    assert r_ok.json()["symbol"] == "AAPL"
    assert r_ok.json()["instrument_id"] == 0

    # Unknown symbol
    r_err = api_test_client.get("/book/UNKNOWN")
    assert r_err.status_code == 404


def test_health_endpoint(api_test_client, mock_client):
    """Test 13: Health endpoint when gateway is healthy vs degraded."""
    # Healthy
    r_healthy = api_test_client.get("/health")
    assert r_healthy.status_code == 200
    assert r_healthy.json()["status"] == "healthy"
    assert r_healthy.json()["gateway"]["connected"] is True


def test_health_endpoint_degraded(api_test_client, failing_mock_client):
    # Degraded
    r_degraded = api_test_client.get("/health")
    assert r_degraded.status_code == 503
    assert r_degraded.json()["status"] == "degraded"
    assert r_degraded.json()["gateway"]["connected"] is False


def test_cancel_order_invalid_symbol(api_test_client, mock_client):
    """Test 14: Cancel with invalid symbol returns 400."""
    response = api_test_client.delete("/orders/1?symbol=INVALID_SYMBOL")
    assert response.status_code == 400
    assert "Unknown symbol" in response.json()["detail"]


def test_cancel_order_invalid_order_id(api_test_client, mock_client):
    """Test 15: Cancel with order_id=0 returns 422."""
    response = api_test_client.delete("/orders/0?symbol=AAPL")
    assert response.status_code == 422


def test_modify_order_invalid_inputs(api_test_client, mock_client):
    """Test 16: Modify with invalid order_id, price, qty, or symbol."""
    # Invalid order_id = 0
    r1 = api_test_client.patch("/orders/0", json={
        "symbol": "AAPL", "new_price": 100, "new_quantity": 10
    })
    assert r1.status_code == 422

    # Invalid new_price = 0
    r2 = api_test_client.patch("/orders/1", json={
        "symbol": "AAPL", "new_price": 0, "new_quantity": 10
    })
    assert r2.status_code == 422

    # Invalid new_quantity = 0
    r3 = api_test_client.patch("/orders/1", json={
        "symbol": "AAPL", "new_price": 100, "new_quantity": 0
    })
    assert r3.status_code == 422

    # Invalid symbol
    r4 = api_test_client.patch("/orders/1", json={
        "symbol": "NONEXISTENT", "new_price": 100, "new_quantity": 10
    })
    assert r4.status_code == 422


def test_market_order_with_price_rejected(api_test_client, mock_client):
    """Test 17: Market order specifying non-zero price is rejected."""
    response = api_test_client.post("/orders", json={
        "symbol": "AAPL",
        "side": "buy",
        "order_type": "market",
        "price": 100,
        "quantity": 10
    })
    assert response.status_code == 422


# ─────────────────────────────────────────────
# Real End-to-End Integration Test
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
    Test 14: Real Integration:
      HTTP request -> FastAPI -> Python TCP client -> Real C++ kqueue Gateway -> MatchingEngine
    """
    # 1. Start the C++ gateway on a free port
    test_port = get_free_port()
    proc = subprocess.Popen(
        ["./gateway", str(test_port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert wait_for_port(test_port), f"C++ Gateway failed to listen on port {test_port}"

    try:
        # 2. Configure a real TCP client pointing to this gateway
        real_client = GatewayTcpClient(host="127.0.0.1", port=test_port)
        app.dependency_overrides[get_gateway_client] = lambda: real_client
        http_client = TestClient(app)

        # 3. Check health over real TCP
        health_resp = http_client.get("/health")
        assert health_resp.status_code == 200
        assert health_resp.json()["status"] == "healthy"

        # 4. Submit Limit Buy AAPL 150 @ 10 via HTTP
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

        # 5. Submit matching Limit Sell AAPL 150 @ 10 via HTTP
        sell_resp = http_client.post("/orders", json={
            "symbol": "AAPL",
            "side": "sell",
            "order_type": "limit",
            "price": 150,
            "quantity": 10,
            "time_in_force": "GTC"
        })
        assert sell_resp.status_code == 202
        assert sell_resp.json()["status"] == "ACCEPTED"

        time.sleep(0.2)

        # 6. Submit Market Order on INFY
        market_resp = http_client.post("/orders", json={
            "symbol": "INFY",
            "side": "sell",
            "order_type": "market",
            "quantity": 50
        })
        assert market_resp.status_code == 202

        # 7. Submit Cancel
        cancel_resp = http_client.delete("/orders/1?symbol=AAPL")
        assert cancel_resp.status_code == 200

        # 8. Submit Modify
        modify_resp = http_client.patch("/orders/2", json={
            "symbol": "AAPL",
            "new_price": 155,
            "new_quantity": 20
        })
        assert modify_resp.status_code == 200

    finally:
        app.dependency_overrides.clear()
        proc.terminate()
        proc.wait(timeout=2)
