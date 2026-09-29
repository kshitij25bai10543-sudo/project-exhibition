"""StormSense AI — demo backend (FastAPI).
Every value served here is a DEMO SIMULATION: a scripted storm, not real weather
data and not a measure of prediction accuracy. The ConvLSTM model is not connected.
Run:  python main.py            (or: python -m uvicorn main:app --reload)
Docs: http://localhost:8000/docs
"""
import math, time, uuid
from pathlib import Path
from typing import Optional
try:
    from typing import Literal          # Python 3.8+
except ImportError:                     # pragma: no cover
    from typing_extensions import Literal
from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

DISCLAIMER = "DEMO SIMULATION — not real-time data."
app = FastAPI(title="StormSense AI (demo backend)", version="0.1.0", description=DISCLAIMER)
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# Scripted track: (lead hour, lat, lon) — NOW, +12H, +24H, +36H, +48H, +72H
TRACK = [(0, 16.0, 66.0), (12, 17.4, 67.6), (24, 18.6, 69.0), (36, 20.0, 70.4), (48, 21.2, 71.6), (72, 23.0, 73.2)]
SPAN = 72


def _pos(h: float):
    h = max(0.0, min(float(SPAN), h))
    for (h0, a0, o0), (h1, a1, o1) in zip(TRACK, TRACK[1:]):
        if h0 <= h <= h1:
            u = (h - h0) / (h1 - h0)
            return a0 + (a1 - a0) * u, o0 + (o1 - o0) * u
    return TRACK[-1][1], TRACK[-1][2]


def _km(a1, o1, a2, o2):
    p = math.pi / 180
    d = math.sin((a2 - a1) * p / 2) ** 2 + math.cos(a1 * p) * math.cos(a2 * p) * math.sin((o2 - o1) * p / 2) ** 2
    return 12742 * math.asin(min(1.0, math.sqrt(d)))


def _state(h: float):
    q = max(0.0, min(1.0, h / SPAN))
    lat, lon = _pos(h)
    wind = round(128 - 10 * q + 4 * math.sin(h))
    risk = "HIGH" if q < .55 else "ELEVATED" if q < .85 else "MODERATE"
    return {"hour": round(h, 1), "lat": round(lat, 3), "lon": round(lon, 3), "wind_kmh": wind,
            "pressure_hpa": round(964 + 8 * q), "rainfall_mm": round(182 + 70 * q), "risk": risk,
            "disclaimer": DISCLAIMER}


class Subscription(BaseModel):
    name: str = Field(..., min_length=1, max_length=128)
    audience: Literal["farmer", "municipal", "responder"] = "farmer"
    lat: float = Field(..., ge=-90, le=90)
    lon: float = Field(..., ge=-180, le=180)
    radius_km: float = Field(150, gt=0, le=1000)


SUBS: dict = {}  # in-memory store; swap for PostgreSQL/PostGIS in production


@app.get("/api/health")
def health():
    return {"status": "online", "mode": "demo", "disclaimer": DISCLAIMER, "time": time.time()}


@app.get("/api/storm")
def storm(hour: float = Query(0, ge=0, le=SPAN, description="Lead hour along the scripted track")):
    return _state(hour)


@app.get("/api/storm/track")
def track():
    return {"disclaimer": DISCLAIMER,
            "points": [{"label": "NOW" if h == 0 else f"+{h}H", **{"hour": h, "lat": a, "lon": o}} for h, a, o in TRACK]}


@app.get("/api/forecast/heatmap")
def heatmap(hour: float = Query(0, ge=0, le=SPAN), size: int = Query(20, ge=5, le=60)):
    """Gaussian risk field (0–1) around the simulated storm centre on a lat/lon grid."""
    lat0, lat1, lon0, lon1 = 12.0, 26.0, 62.0, 76.0
    c = _pos(hour)
    cells = []
    for i in range(size):
        row = []
        for j in range(size):
            la = lat0 + (lat1 - lat0) * i / (size - 1)
            lo = lon0 + (lon1 - lon0) * j / (size - 1)
            row.append(round(math.exp(-(_km(la, lo, *c) / 220) ** 2), 3))
        cells.append(row)
    return {"hour": hour, "bbox": [lat0, lon0, lat1, lon1], "grid": cells, "disclaimer": DISCLAIMER}


@app.post("/api/alerts/subscribe", status_code=201)
def subscribe(s: Subscription):
    sid = uuid.uuid4().hex[:8]
    SUBS[sid] = s.model_dump() if hasattr(s, "model_dump") else s.dict()  # pydantic v2 / v1
    return {"id": sid, **SUBS[sid]}


@app.get("/api/alerts/subscriptions")
def subs():
    return [{"id": k, **v} for k, v in SUBS.items()]


@app.delete("/api/alerts/subscribe/{sid}")
def unsubscribe(sid: str):
    if SUBS.pop(sid, None) is None:
        raise HTTPException(404, "Subscription not found")
    return {"deleted": sid}


@app.get("/api/alerts")
def alerts(sub_id: Optional[str] = None):
    """Geo-fenced alerts: any subscription whose radius the simulated track enters within 72 h."""
    items = []
    for sid, s in SUBS.items():
        if sub_id and sid != sub_id:
            continue
        hit = next((h for h in range(0, SPAN + 1) if _km(s["lat"], s["lon"], *_pos(h)) <= s["radius_km"]), None)
        if hit is not None:
            items.append({"subscription_id": sid, "name": s["name"], "audience": s["audience"],
                          "title": "WEATHER ALERT (DEMO)", "message": "HIGH-RISK STORM CELL DETECTED",
                          "lead_time_hours": hit, "disclaimer": DISCLAIMER})
    return {"count": len(items), "alerts": items, "disclaimer": DISCLAIMER}


# Serve the exhibition site (index.html + style.js live one folder up) from the same origin.
_site = Path(__file__).resolve().parent.parent
if (_site / "index.html").exists():
    app.mount("/", StaticFiles(directory=_site, html=True), name="site")


if __name__ == "__main__":  # lets you start it with plain `python main.py` on any OS
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8000)
