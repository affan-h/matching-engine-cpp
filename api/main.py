from contextlib import asynccontextmanager
from typing import Optional
from fastapi import FastAPI, HTTPException, Depends, Query, Path, status
from fastapi.responses import JSONResponse
from fastapi.exceptions import RequestValidationError
from api.config import GATEWAY_HOST, GATEWAY_PORT, GATEWAY_TIMEOUT, SYMBOL_MAP
from api.models import (
    OrderCreateRequest,
    OrderModifyRequest,
    OrderResponse,
    CancelResponse,
    ModifyResponse,
    OrderBookResponse,
    HealthResponse,
    GatewayStatus,
    OrderTypeEnum,
)
from api.tcp_client import GatewayTcpClient, GatewayUnavailableError, GatewayError

# Global gateway client instance
_gateway_client: Optional[GatewayTcpClient] = None


def get_gateway_client() -> GatewayTcpClient:
    global _gateway_client
    if _gateway_client is None:
        _gateway_client = GatewayTcpClient(
            host=GATEWAY_HOST,
            port=GATEWAY_PORT,
            timeout=GATEWAY_TIMEOUT,
        )
    return _gateway_client


def set_gateway_client(client: Optional[GatewayTcpClient]):
    global _gateway_client
    _gateway_client = client


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup: initialize gateway client
    client = get_gateway_client()
    yield
    # Shutdown: close socket
    client.close()


app = FastAPI(
    title="Matching Engine HTTP REST API",
    description="Client-facing HTTP adapter communicating with the C++ kqueue Matching Engine Gateway via explicit binary wire protocol.",
    version="1.0.0",
    lifespan=lifespan,
)


# ─────────────────────────────────────────────
# Exception Handlers
# ─────────────────────────────────────────────

@app.exception_handler(GatewayUnavailableError)
async def gateway_unavailable_handler(request, exc: GatewayUnavailableError):
    return JSONResponse(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        content={"error": "GatewayUnavailable", "detail": str(exc)},
    )


@app.exception_handler(GatewayError)
async def gateway_error_handler(request, exc: GatewayError):
    return JSONResponse(
        status_code=status.HTTP_502_BAD_GATEWAY,
        content={"error": "GatewayError", "detail": str(exc)},
    )


@app.exception_handler(ValueError)
async def value_error_handler(request, exc: ValueError):
    return JSONResponse(
        status_code=status.HTTP_400_BAD_REQUEST,
        content={"error": "BadRequest", "detail": str(exc)},
    )


# ─────────────────────────────────────────────
# REST Endpoints
# ─────────────────────────────────────────────

@app.post(
    "/orders",
    response_model=OrderResponse,
    status_code=status.HTTP_202_ACCEPTED,
    summary="Submit New Limit or Market Order",
)
def create_order(
    req: OrderCreateRequest,
    gateway: GatewayTcpClient = Depends(get_gateway_client),
):
    symbol = req.symbol.upper()
    side_str = req.side.value
    order_type_str = req.order_type.value

    if req.order_type == OrderTypeEnum.LIMIT:
        tif_str = req.time_in_force.value if req.time_in_force else "GTC"
        inst_id = gateway.submit_limit_order(
            symbol=symbol,
            side=side_str,
            price=req.price,
            quantity=req.quantity,
            tif=tif_str,
        )
        return OrderResponse(
            status="ACCEPTED",
            symbol=symbol,
            instrument_id=inst_id,
            side=side_str,
            order_type=order_type_str,
            price=req.price,
            quantity=req.quantity,
            time_in_force=tif_str,
            message="Limit order successfully submitted to matching engine gateway",
        )
    else:  # Market Order
        inst_id = gateway.submit_market_order(
            symbol=symbol,
            side=side_str,
            quantity=req.quantity,
        )
        return OrderResponse(
            status="ACCEPTED",
            symbol=symbol,
            instrument_id=inst_id,
            side=side_str,
            order_type=order_type_str,
            price=0,
            quantity=req.quantity,
            time_in_force="IOC",
            message="Market order successfully submitted to matching engine gateway",
        )


@app.delete(
    "/orders/{order_id}",
    response_model=CancelResponse,
    status_code=status.HTTP_200_OK,
    summary="Cancel Resting Order",
)
def cancel_order(
    order_id: int = Path(..., ge=1, description="Order ID to cancel (>= 1)"),
    symbol: str = Query("AAPL", description="Instrument symbol (AAPL, RELIANCE, INFY, TATASTEEL)"),
    gateway: GatewayTcpClient = Depends(get_gateway_client),
):
    symbol_upper = symbol.strip().upper()
    if symbol_upper not in SYMBOL_MAP:
        valid_symbols = ", ".join(SYMBOL_MAP.keys())
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Unknown symbol '{symbol}'. Valid symbols: {valid_symbols}",
        )

    inst_id = gateway.submit_cancel_order(symbol=symbol_upper, order_id=order_id)
    return CancelResponse(
        status="ACCEPTED",
        symbol=symbol_upper,
        instrument_id=inst_id,
        order_id=order_id,
        message="Cancel request successfully submitted to matching engine gateway",
    )


@app.patch(
    "/orders/{order_id}",
    response_model=ModifyResponse,
    status_code=status.HTTP_200_OK,
    summary="Modify Resting Order Price and Quantity",
)
def modify_order(
    req: OrderModifyRequest,
    order_id: int = Path(..., ge=1, description="Order ID to modify (>= 1)"),
    gateway: GatewayTcpClient = Depends(get_gateway_client),
):
    symbol_upper = req.symbol.upper()
    inst_id = gateway.submit_modify_order(
        symbol=symbol_upper,
        order_id=order_id,
        new_price=req.new_price,
        new_quantity=req.new_quantity,
    )
    return ModifyResponse(
        status="ACCEPTED",
        symbol=symbol_upper,
        instrument_id=inst_id,
        order_id=order_id,
        new_price=req.new_price,
        new_quantity=req.new_quantity,
        message="Modify request successfully submitted to matching engine gateway",
    )


@app.get(
    "/book/{symbol}",
    response_model=OrderBookResponse,
    status_code=status.HTTP_200_OK,
    summary="Get Order Book Depth for Symbol",
)
def get_order_book(
    symbol: str = Path(..., description="Instrument symbol (AAPL, RELIANCE, INFY, TATASTEEL)"),
):
    symbol_upper = symbol.strip().upper()
    if symbol_upper not in SYMBOL_MAP:
        valid_symbols = ", ".join(SYMBOL_MAP.keys())
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Symbol '{symbol}' not found. Supported symbols: {valid_symbols}",
        )

    inst_id = SYMBOL_MAP[symbol_upper]
    return OrderBookResponse(
        symbol=symbol_upper,
        instrument_id=inst_id,
        bids=[],
        asks=[],
    )


@app.get(
    "/health",
    response_model=HealthResponse,
    summary="Service & TCP Gateway Health Check",
)
def health_check(
    gateway: GatewayTcpClient = Depends(get_gateway_client),
):
    is_connected = gateway.check_health()
    if is_connected:
        return HealthResponse(
            status="healthy",
            gateway=GatewayStatus(
                host=gateway.host,
                port=gateway.port,
                connected=True,
            ),
            symbols=list(SYMBOL_MAP.keys()),
        )
    else:
        return JSONResponse(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            content=HealthResponse(
                status="degraded",
                gateway=GatewayStatus(
                    host=gateway.host,
                    port=gateway.port,
                    connected=False,
                    error="C++ TCP Gateway is unreachable / refusing connections",
                ),
                symbols=list(SYMBOL_MAP.keys()),
            ).model_dump(),
        )
