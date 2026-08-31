from enum import Enum
from typing import Optional, List, Dict, Any
import time
from pydantic import BaseModel, Field, field_validator, model_validator
from api.config import SYMBOL_MAP, MIN_PRICE, MAX_PRICE, MIN_QUANTITY, MIN_ORDER_ID


class SideEnum(str, Enum):
    BUY = "buy"
    SELL = "sell"


class OrderTypeEnum(str, Enum):
    LIMIT = "limit"
    MARKET = "market"


class TimeInForceEnum(str, Enum):
    GTC = "GTC"
    IOC = "IOC"
    FOK = "FOK"


class OrderCreateRequest(BaseModel):
    symbol: str = Field(..., description="Instrument symbol (e.g. AAPL, RELIANCE, INFY, TATASTEEL)")
    side: SideEnum = Field(..., description="Order side ('buy' or 'sell')")
    order_type: OrderTypeEnum = Field(..., description="Order type ('limit' or 'market')")
    price: Optional[int] = Field(None, description="Limit price (required for limit orders, 1..100000)")
    quantity: int = Field(..., ge=MIN_QUANTITY, description="Order quantity (>= 1)")
    time_in_force: Optional[TimeInForceEnum] = Field(None, description="Time in force (GTC, IOC, FOK)")
    client_order_id: Optional[int] = Field(None, ge=1, description="Optional client-provided correlation ID")

    @field_validator("symbol")
    @classmethod
    def validate_symbol(cls, v: str) -> str:
        v_upper = v.strip().upper()
        if v_upper not in SYMBOL_MAP:
            valid_symbols = ", ".join(SYMBOL_MAP.keys())
            raise ValueError(f"Unknown symbol '{v}'. Valid symbols are: {valid_symbols}")
        return v_upper

    @model_validator(mode="after")
    def validate_order_type_and_price(self) -> "OrderCreateRequest":
        if self.order_type == OrderTypeEnum.LIMIT:
            if self.price is None:
                raise ValueError("Price is required for limit orders")
            if self.price < MIN_PRICE or self.price > MAX_PRICE:
                raise ValueError(f"Limit price must be between {MIN_PRICE} and {MAX_PRICE}")
            if self.time_in_force is None:
                self.time_in_force = TimeInForceEnum.GTC
        elif self.order_type == OrderTypeEnum.MARKET:
            if self.price is not None and self.price != 0:
                raise ValueError("Price must not be specified for market orders")
            self.price = 0
            if self.time_in_force is None:
                self.time_in_force = TimeInForceEnum.IOC
        return self


class OrderModifyRequest(BaseModel):
    symbol: str = Field(..., description="Instrument symbol")
    new_price: int = Field(..., ge=MIN_PRICE, le=MAX_PRICE, description="New limit price (1..100000)")
    new_quantity: int = Field(..., ge=MIN_QUANTITY, description="New quantity (>= 1)")
    client_order_id: Optional[int] = Field(None, ge=1, description="Optional client correlation ID")

    @field_validator("symbol")
    @classmethod
    def validate_symbol(cls, v: str) -> str:
        v_upper = v.strip().upper()
        if v_upper not in SYMBOL_MAP:
            valid_symbols = ", ".join(SYMBOL_MAP.keys())
            raise ValueError(f"Unknown symbol '{v}'. Valid symbols are: {valid_symbols}")
        return v_upper


class OrderResponse(BaseModel):
    status: str = "ACCEPTED"
    symbol: str
    instrument_id: int
    client_order_id: Optional[int] = None
    side: str
    order_type: str
    price: Optional[int]
    quantity: int
    time_in_force: str
    message: str


class CancelResponse(BaseModel):
    status: str = "ACCEPTED"
    symbol: str
    instrument_id: int
    order_id: int
    client_order_id: Optional[int] = None
    message: str


class ModifyResponse(BaseModel):
    status: str = "ACCEPTED"
    symbol: str
    instrument_id: int
    order_id: int
    client_order_id: Optional[int] = None
    new_price: int
    new_quantity: int
    message: str


class PriceLevelItem(BaseModel):
    price: int
    quantity: int


class OrderBookResponse(BaseModel):
    symbol: str
    instrument_id: int
    sequence: int = 0
    timestamp: int = 0
    bids: List[PriceLevelItem] = Field(default_factory=list)
    asks: List[PriceLevelItem] = Field(default_factory=list)


class TradeItem(BaseModel):
    trade_id: int
    symbol: Optional[str] = None
    buy_order_id: int
    sell_order_id: int
    price: int
    quantity: int
    aggressor_side: str
    timestamp: int


class TradesResponse(BaseModel):
    symbol: str
    instrument_id: int
    trades: List[TradeItem] = Field(default_factory=list)


class OrderStateResponse(BaseModel):
    order_id: int
    client_order_id: int = 0
    symbol: str
    instrument_id: int
    side: str
    price: int
    original_quantity: int
    remaining_quantity: int
    filled_quantity: int
    status: str
    reject_code: str = "NONE"
    timestamp: int
    sequence: int = 0


class SystemMetricsResponse(BaseModel):
    total_trades: int
    total_volume: int
    total_orders_accepted: int
    total_orders_filled: int
    total_orders_cancelled: int
    total_orders_rejected: int
    last_sequence: int
    tracked_orders_count: int
    registered_symbols_count: int
    gateway_connected: bool


class ReadModelStatus(BaseModel):
    active: bool
    last_sequence: int
    registered_symbols: int
    tracked_orders: int
    total_trades: int


class GatewayStatus(BaseModel):
    host: str
    port: int
    connected: bool
    rtt_ms: Optional[float] = None
    error: Optional[str] = None


class HealthResponse(BaseModel):
    status: str
    ready: bool = True
    gateway: GatewayStatus
    read_model: Optional[ReadModelStatus] = None
    symbols: List[str]
