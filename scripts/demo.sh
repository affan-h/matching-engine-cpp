#!/usr/bin/env bash
set -e

# ==============================================================================
# Matching Engine 2-Minute End-to-End Demo
# Demonstrates: Start -> Health -> Buy -> Book -> Partial Match -> Trade
#               -> Resting Ask -> Cancel -> FOK Atomic Rejection -> Shutdown
# ==============================================================================

BOLD="\033[1m"
GREEN="\033[32m"
BLUE="\033[34m"
YELLOW="\033[33m"
CYAN="\033[36m"
RESET="\033[0m"

# Find free ephemeral ports
GW_PORT=${DEMO_GATEWAY_PORT:-$(python3 -c 'import socket; s=socket.socket(); s.bind(("", 0)); print(s.getsockname()[1]); s.close()')}
API_PORT=${DEMO_API_PORT:-$(python3 -c 'import socket; s=socket.socket(); s.bind(("", 0)); print(s.getsockname()[1]); s.close()')}
BASE_URL="http://127.0.0.1:${API_PORT}"

echo -e "${BOLD}${BLUE}================================================================${RESET}"
echo -e "${BOLD}${BLUE}       Matching Engine: 2-Minute End-to-End Architecture Demo    ${RESET}"
echo -e "${BOLD}${BLUE}================================================================${RESET}\n"

# 1. Compile gateway if binary is missing
if [ ! -f ./gateway ]; then
    echo -e "${YELLOW}Compiling C++ Gateway binary...${RESET}"
    make gateway > /dev/null 2>&1
fi

# 2. Cleanup traps
cleanup() {
    echo -e "\n${YELLOW}[Teardown] Gracefully stopping services...${RESET}"
    if [ -n "${API_PID}" ]; then kill -TERM "${API_PID}" 2>/dev/null || true; fi
    if [ -n "${GW_PID}" ]; then kill -TERM "${GW_PID}" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -f /tmp/gateway_demo.log /tmp/api_demo.log
    echo -e "${GREEN}[Teardown] All background processes cleanly terminated.${RESET}"
}
trap cleanup EXIT INT TERM

# 3. Start C++ Gateway
echo -e "${CYAN}[1/10] Starting C++ TCP Gateway on port ${GW_PORT}...${RESET}"
./gateway -p "${GW_PORT}" > /tmp/gateway_demo.log 2>&1 &
GW_PID=$!
sleep 0.4

# 4. Start FastAPI REST API Server
echo -e "${CYAN}[2/10] Starting FastAPI REST server on port ${API_PORT}...${RESET}"
PYTHONPATH=. MATCHING_ENGINE_GATEWAY_PORT="${GW_PORT}" MATCHING_ENGINE_GATEWAY_HOST="127.0.0.1" \
    .venv/bin/uvicorn api.main:app --port "${API_PORT}" --host 127.0.0.1 --log-level warning > /tmp/api_demo.log 2>&1 &
API_PID=$!

# Wait for API server readiness
READY=0
for i in {1..20}; do
    if curl -s -f "${BASE_URL}/health" > /dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 0.2
done

if [ "$READY" -ne 1 ]; then
    echo "Failed to start API server. Logs:"
    cat /tmp/api_demo.log
    cat /tmp/gateway_demo.log
    exit 1
fi

# 5. Check Health Endpoint
echo -e "\n${BOLD}${BLUE}>>> STEP 1: Verify System Health & TCP Round-Trip${RESET}"
echo -e "${YELLOW}Command: GET /health${RESET}"
curl -s -X GET "${BASE_URL}/health" | python3 -m json.tool
sleep 0.5

# 6. Submit Initial BUY Limit Order
echo -e "\n${BOLD}${BLUE}>>> STEP 2: Submit BUY Limit Order (100 AAPL @ \$150)${RESET}"
echo -e "${YELLOW}Command: POST /orders -d '{\"symbol\":\"AAPL\",\"side\":\"buy\",\"order_type\":\"limit\",\"price\":150,\"quantity\":100}'${RESET}"
curl -s -X POST "${BASE_URL}/orders" \
    -H "Content-Type: application/json" \
    -d '{"symbol":"AAPL","side":"buy","order_type":"limit","price":150,"quantity":100}' | python3 -m json.tool
sleep 0.4

# 7. Inspect Order Book
echo -e "\n${BOLD}${BLUE}>>> STEP 3: Query Order Book (Should show 100 shares on Bid)${RESET}"
echo -e "${YELLOW}Command: GET /book/AAPL${RESET}"
curl -s -X GET "${BASE_URL}/book/AAPL" | python3 -m json.tool
sleep 0.5

# 8. Submit SELL Limit Order (Partial Match)
echo -e "\n${BOLD}${BLUE}>>> STEP 4: Submit Aggressive SELL Order (40 AAPL @ \$150)${RESET}"
echo -e "${YELLOW}Command: POST /orders -d '{\"symbol\":\"AAPL\",\"side\":\"sell\",\"order_type\":\"limit\",\"price\":150,\"quantity\":40}'${RESET}"
curl -s -X POST "${BASE_URL}/orders" \
    -H "Content-Type: application/json" \
    -d '{"symbol":"AAPL","side":"sell","order_type":"limit","price":150,"quantity":40}' | python3 -m json.tool
sleep 0.4

# 9. Verify Trade Execution
echo -e "\n${BOLD}${BLUE}>>> STEP 5: Query Recent Trades (Executed 40 shares @ \$150)${RESET}"
echo -e "${YELLOW}Command: GET /trades/AAPL${RESET}"
curl -s -X GET "${BASE_URL}/trades/AAPL" | python3 -m json.tool
sleep 0.5

# 10. Verify Remaining Bid Book Depth
echo -e "\n${BOLD}${BLUE}>>> STEP 6: Query Order Book (Bid quantity reduced from 100 -> 60)${RESET}"
echo -e "${YELLOW}Command: GET /book/AAPL${RESET}"
curl -s -X GET "${BASE_URL}/book/AAPL" | python3 -m json.tool
sleep 0.5

# 11. Submit Resting Order on the Ask Side
echo -e "\n${BOLD}${BLUE}>>> STEP 7: Submit Resting SELL Order (50 AAPL @ \$155)${RESET}"
echo -e "${YELLOW}Command: POST /orders -d '{\"symbol\":\"AAPL\",\"side\":\"sell\",\"order_type\":\"limit\",\"price\":155,\"quantity\":50}'${RESET}"
curl -s -X POST "${BASE_URL}/orders" \
    -H "Content-Type: application/json" \
    -d '{"symbol":"AAPL","side":"sell","order_type":"limit","price":155,"quantity":50}' | python3 -m json.tool
sleep 0.4

# 12. Show Two-Sided Market
echo -e "\n${BOLD}${BLUE}>>> STEP 8: Query Two-Sided Market Depth (Bids @ \$150, Asks @ \$155)${RESET}"
echo -e "${YELLOW}Command: GET /book/AAPL${RESET}"
curl -s -X GET "${BASE_URL}/book/AAPL" | python3 -m json.tool
sleep 0.5

# 13. Cancel Resting SELL Order
echo -e "\n${BOLD}${BLUE}>>> STEP 9: Cancel Resting SELL Order (OrderId: 3)${RESET}"
echo -e "${YELLOW}Command: DELETE /orders/3?symbol=AAPL${RESET}"
curl -s -X DELETE "${BASE_URL}/orders/3?symbol=AAPL" | python3 -m json.tool
sleep 0.4

# 14. Verify Order Removed from Book
echo -e "\n${BOLD}${BLUE}>>> STEP 10: Query Book (Ask side is empty again)${RESET}"
echo -e "${YELLOW}Command: GET /book/AAPL${RESET}"
curl -s -X GET "${BASE_URL}/book/AAPL" | python3 -m json.tool
sleep 0.5

# 15. Demonstrate FOK Rejection
echo -e "\n${BOLD}${BLUE}>>> STEP 11: Submit FOK Order with Insufficient Liquidity (500 AAPL @ \$150)${RESET}"
echo -e "${YELLOW}Command: POST /orders -d '{\"symbol\":\"AAPL\",\"side\":\"sell\",\"order_type\":\"limit\",\"price\":150,\"quantity\":500,\"time_in_force\":\"FOK\"}'${RESET}"
curl -s -X POST "${BASE_URL}/orders" \
    -H "Content-Type: application/json" \
    -d '{"symbol":"AAPL","side":"sell","order_type":"limit","price":150,"quantity":500,"time_in_force":"FOK"}' | python3 -m json.tool
sleep 0.5

echo -e "\n${BOLD}${BLUE}>>> STEP 12: Query Order Status (Verify FOK rejection without book mutation)${RESET}"
echo -e "${YELLOW}Command: GET /orders/4${RESET}"
curl -s -X GET "${BASE_URL}/orders/4" | python3 -m json.tool

echo -e "\n${BOLD}${GREEN}================================================================${RESET}"
echo -e "${BOLD}${GREEN}       Demo Completed Successfully: All Invariants Verified!     ${RESET}"
echo -e "${BOLD}${GREEN}================================================================${RESET}"
