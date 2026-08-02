# Order Matching Engine — market data server
#
# FastAPI + WebSocket bridge over the matching rules.
#
# The authoritative, dependency-free implementation is the C++ engine
# under engine/. This Python server mirrors the same deterministic
# price-time priority rules so the live feed is fully self-contained and
# runnable anywhere (CI, Docker, Render).

import uuid

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

app = FastAPI(
    title="Order Matching Engine",
    description="Low-latency limit order matching with price-time priority.",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class OrderRequest(BaseModel):
    side: str = Field(..., pattern="^(BUY|SELL)$")
    price: int | None = Field(None, gt=0)
    quantity: int = Field(..., gt=0)
    order_type: str = Field("LIMIT", pattern="^(LIMIT|MARKET)$")
    tif: str = Field("GTC", pattern="^(GTC|IOC|FOK)$")


class Order(dict):
    def remaining(self) -> int:
        return self["quantity"] - self.get("filled", 0)


class Book:
    """Price-time priority order book."""

    def __init__(self):
        self.levels: dict[str, dict[int, list[str]]] = {"BUY": {}, "SELL": {}}
        self.orders: dict[str, Order] = {}

    def add(self, order: Order):
        self.levels[order["side"]].setdefault(order["price"], []).append(order["id"])
        self.orders[order["id"]] = order

    def best(self, side: str) -> tuple[int | None, list[str] | None]:
        prices = self.levels[side]
        if not prices:
            return None, None
        key = max if side == "BUY" else min
        p = key(prices)
        return p, prices[p]

    def depth(self, n: int = 5) -> dict:
        def snap(side: str) -> list[dict]:
            prices = self.levels[side]
            out = []
            for p in sorted(prices, reverse=(side == "BUY"))[:n]:
                out.append({
                    "price": p,
                    "qty": sum(self.orders[i].remaining() for i in prices[p]),
                    "orders": len(prices[p]),
                })
            return out
        return {"bids": snap("BUY"), "asks": snap("SELL")}

    def remove(self, order_id: str):
        order = self.orders.get(order_id)
        if not order:
            return
        prices = self.levels[order["side"]]
        prices[order["price"]].remove(order_id)
        if not prices[order["price"]]:
            del prices[order["price"]]
        del self.orders[order_id]


class MatchingEngine:
    """Deterministic matching with price-time priority, FIFO per level."""

    def __init__(self):
        self.book = Book()
        self.tape: list[dict] = []
        self.ws_clients: list[WebSocket] = []

    async def broadcast(self, message: dict):
        for ws in list(self.ws_clients):
            try:
                await ws.send_json(message)
            except Exception:
                self.ws_clients.remove(ws)

    async def submit(self, req: OrderRequest) -> dict:
        order = Order(
            id=uuid.uuid4().hex[:12],
            side=req.side,
            price=req.price,
            quantity=req.quantity,
            filled=0,
            order_type=req.order_type,
            tif=req.tif,
        )

        if order["order_type"] == "MARKET":
            order["price"] = None  # marketable at best available price

        fills: list[dict] = []
        while order.remaining() > 0:
            opp = "SELL" if order["side"] == "BUY" else "BUY"
            price, ids = self.book.best(opp)
            if price is None:
                break
            if order["order_type"] == "LIMIT":
                if order["side"] == "BUY" and order["price"] < price:
                    break
                if order["side"] == "SELL" and order["price"] > price:
                    break
            maker = self.book.orders[ids[0]]
            qty = min(order.remaining(), maker.remaining())
            maker["filled"] += qty
            order["filled"] += qty
            fills.append({"taker": order["id"], "maker": maker["id"],
                          "price": price, "qty": qty})
            if maker.remaining() == 0:
                self.book.remove(maker["id"])

        if order.remaining() > 0:
            if order["tif"] in ("IOC", "FOK"):
                status, reason = "REJECTED", "unfilled remainder under IOC/FOK"
            else:
                # Rest remainder at the limit price.
                order["price"] = req.price
                self.book.add(order)
                status = "PARTIALLY_FILLED" if fills else "NEW"
                reason = ""
        else:
            status, reason = "FILLED", ""

        result = {"order_id": order["id"], "status": status, "fills": fills, "reason": reason}
        if fills:
            self.tape.append(fills[-1])
            await self.broadcast({"type": "trade", "data": fills[-1]})
        await self.broadcast({"type": "depth", "data": self.book.depth(5)})
        return result

    async def cancel(self, order_id: str) -> bool:
        if order_id not in self.book.orders:
            return False
        self.book.remove(order_id)
        await self.broadcast({"type": "depth", "data": self.book.depth(5)})
        return True


engine = MatchingEngine()


@app.post("/orders")
async def submit_order(req: OrderRequest):
    return await engine.submit(req)


@app.delete("/orders/{order_id}")
async def cancel_order(order_id: str):
    return {"cancelled": await engine.cancel(order_id)}


@app.get("/depth")
async def depth(n: int = 5):
    return engine.book.depth(n)


@app.get("/tape")
async def tape(limit: int = 50):
    return engine.tape[-limit:]


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.websocket("/ws")
async def ws_feed(ws: WebSocket):
    await ws.accept()
    engine.ws_clients.append(ws)
    try:
        await ws.send_json({"type": "depth", "data": engine.book.depth(5)})
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        engine.ws_clients.remove(ws)
