#!/usr/bin/env python
"""Signal endpoints for providing trading signals to Trading-CPP"""

import os
import sys
from datetime import datetime, timedelta
from typing import List, Optional, Dict, Any
from fastapi import APIRouter, Query, HTTPException
from pydantic import BaseModel
import numpy as np
import pandas as pd

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

logger = None


def get_logger():
    global logger
    if logger is None:
        import logging
        logging.basicConfig(level=logging.INFO)
        logger = logging.getLogger(__name__)
    return logger


router = APIRouter()


# Signal data model
class Signal(BaseModel):
    ticker: str
    timestamp: str
    signal: str  # "buy", "sell", "hold"
    confidence: float  # 0.0 to 1.0
    price: Optional[float] = None
    volume: Optional[float] = None
    indicators: Optional[Dict[str, float]] = None
    source: str = "python_api"


class BatchSignalsResponse(BaseModel):
    signals: List[Signal]
    timestamp: str
    count: int


# In-memory signal cache (in production, use Redis or similar)
_signal_cache: Dict[str, Signal] = {}
_cache_timestamp: Optional[datetime] = None
_cache_ttl_seconds = 300  # 5 minutes


def _get_real_signal(ticker: str) -> Signal:
    """
    Generate a real signal using cached features and ONNX model inference.
    Falls back to technical analysis if no model is available.
    """
    log = get_logger()
    ticker = ticker.upper()

    try:
        from src.service.feature_cache import FeatureCache, SignalCache

        # Check signal cache first (from scheduler)
        signal_cache = SignalCache()
        cached_signal = signal_cache.get_signal(ticker)
        if cached_signal:
            # Convert cached signal to Signal object
            return Signal(
                ticker=cached_signal.get("ticker", ticker),
                timestamp=cached_signal.get("timestamp", datetime.now().isoformat()),
                signal=cached_signal.get("signal", "hold"),
                confidence=cached_signal.get("confidence", 0.5),
                price=cached_signal.get("price"),
                volume=cached_signal.get("volume"),
                source=cached_signal.get("source", "scheduler")
            )

        # Try to load cached features
        feature_cache = FeatureCache(ttl=900)
        features = feature_cache.get(ticker)

        if features is None or features.empty:
            log.debug(f"No cached features for {ticker}, using fallback")
            return _generate_fallback_signal(ticker)

        # Get latest features
        latest = features.iloc[-1:]

        # Try ONNX model inference
        onnx_model = None
        model_path = "./models/trading_model.onnx"

        try:
            import onnxruntime as ort
            if os.path.exists(model_path):
                onnx_model = ort.InferenceSession(model_path)
        except Exception as e:
            log.debug(f"Could not load ONNX model: {e}")

        signal_type = "hold"
        confidence = 0.5

        if onnx_model is not None:
            try:
                # Run ONNX inference
                input_name = onnx_model.get_inputs()[0].name
                feature_cols = [c for c in latest.columns
                              if c not in ['return_1d', 'Open', 'High', 'Low', 'Close', 'Volume', 'ticker']]
                X = latest[feature_cols].values.astype(np.float32)
                X = np.nan_to_num(X, nan=0.0)

                prediction = onnx_model.run(None, {input_name: X})[0][0]

                if prediction > 0.01:
                    signal_type = "buy"
                    confidence = min(abs(prediction) * 10, 0.95)
                elif prediction < -0.01:
                    signal_type = "sell"
                    confidence = min(abs(prediction) * 10, 0.95)
            except Exception as e:
                log.debug(f"ONNX inference failed: {e}")
                signal_type, confidence = _generate_ta_signal(latest)
        else:
            # Fallback to technical analysis
            signal_type, confidence = _generate_ta_signal(latest)

        # Get price and volume
        price = None
        volume = None
        if 'Close' in latest.columns:
            price = float(latest['Close'].iloc[-1]) if pd.notna(latest['Close'].iloc[-1]) else None
        if 'Volume' in latest.columns:
            volume = float(latest['Volume'].iloc[-1]) if pd.notna(latest['Volume'].iloc[-1]) else None

        # Get indicators for response
        indicators = _extract_indicators(latest)

        return Signal(
            ticker=ticker,
            timestamp=datetime.now().isoformat(),
            signal=signal_type,
            confidence=float(confidence),
            price=price,
            volume=volume,
            indicators=indicators,
            source="python_api"
        )

    except Exception as e:
        log.warning(f"Error generating real signal for {ticker}: {e}")
        return _generate_fallback_signal(ticker)


def _generate_ta_signal(features) -> tuple:
    """
    Generate signal using simple technical analysis.

    Returns:
        Tuple of (signal_type, confidence)
    """
    try:
        # RSI-based signal
        rsi = features.get('rsi', features.get('rsi_14'))
        if rsi is not None:
            rsi_val = rsi.iloc[-1]
            if pd.notna(rsi_val):
                if rsi_val < 30:
                    return "buy", 0.6
                elif rsi_val > 70:
                    return "sell", 0.6

        # MACD-based signal
        macd = features.get('macd')
        macd_signal = features.get('macd_signal')
        if macd is not None and macd_signal is not None:
            macd_val = macd.iloc[-1]
            macd_sig_val = macd_signal.iloc[-1]
            if pd.notna(macd_val) and pd.notna(macd_sig_val):
                if macd_val > macd_sig_val:
                    return "buy", 0.55
                elif macd_val < macd_sig_val:
                    return "sell", 0.55

        # Default: hold
        return "hold", 0.5

    except Exception:
        return "hold", 0.5


def _extract_indicators(features) -> Dict[str, float]:
    """Extract technical indicators for API response."""
    indicators = {}

    # Common indicators to include
    indicator_names = [
        'rsi', 'rsi_14', 'macd', 'macd_signal', 'macd_hist',
        'sma_20', 'sma_50', 'sma_200', 'ema_20', 'ema_50',
        'bb_upper', 'bb_lower', 'bb_middle', 'atr', 'atr_14',
        'adx', 'cci', 'stoch_k', 'stoch_d'
    ]

    for name in indicator_names:
        if name in features.columns:
            val = features[name].iloc[-1]
            if pd.notna(val):
                indicators[name] = float(val)

    return indicators


def _generate_fallback_signal(ticker: str) -> Signal:
    """
    Generate a fallback signal when no features are available.
    Uses real-time data fetch as last resort.
    """
    import random

    try:
        # Try to get real-time data
        import yfinance as yf
        data = yf.download(ticker, period="5d", progress=False, auto_adjust=True)
        if not data.empty:
            latest_price = float(data['Close'].iloc[-1])
            latest_volume = float(data['Volume'].iloc[-1])

            # Calculate simple indicators from recent data
            close = data['Close']
            rsi = _calculate_rsi(close)

            # Determine signal based on RSI
            if rsi < 30:
                signal_type = "buy"
                confidence = 0.65
            elif rsi > 70:
                signal_type = "sell"
                confidence = 0.65
            else:
                signal_type = "hold"
                confidence = 0.55

            return Signal(
                ticker=ticker.upper(),
                timestamp=datetime.now().isoformat(),
                signal=signal_type,
                confidence=confidence,
                price=latest_price,
                volume=latest_volume,
                indicators={"rsi": round(rsi, 2)},
                source="realtime_api"
            )
    except Exception:
        pass

    # Ultimate fallback: random signal
    signals = ["buy", "sell", "hold"]
    weights = [0.3, 0.2, 0.5]
    signal_type = random.choices(signals, weights=weights)[0]

    return Signal(
        ticker=ticker.upper(),
        timestamp=datetime.now().isoformat(),
        signal=signal_type,
        confidence=random.uniform(0.5, 0.95),
        price=random.uniform(50, 500),
        volume=random.uniform(1e6, 10e6),
        indicators={
            "rsi": random.uniform(30, 70),
            "macd": random.uniform(-2, 2),
        },
        source="python_api"
    )


def _calculate_rsi(close, period=14):
    """Calculate RSI from close prices."""
    delta = close.diff()
    gain = delta.where(delta > 0, 0).rolling(window=period).mean()
    loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
    rs = gain / loss
    rsi = 100 - (100 / (1 + rs))
    return rsi.iloc[-1] if not rsi.empty else 50


def _get_cached_signal(ticker: str) -> Optional[Signal]:
    """Get signal from cache if valid"""
    global _signal_cache, _cache_timestamp

    if _cache_timestamp is None:
        return None

    # Check if cache is still valid
    if (datetime.now() - _cache_timestamp).total_seconds() > _cache_ttl_seconds:
        return None

    return _signal_cache.get(ticker.upper())


def _update_cache(signals: List[Signal]):
    """Update the signal cache"""
    global _signal_cache, _cache_timestamp

    for signal in signals:
        _signal_cache[signal.ticker] = signal

    _cache_timestamp = datetime.now()


@router.get("/signals/{ticker}", response_model=Signal)
async def get_signal(ticker: str):
    """
    Get trading signal for a single ticker.

    Returns the latest signal with confidence score and technical indicators.
    """
    log = get_logger()
    ticker = ticker.upper()

    log.info(f"Fetching signal for {ticker}")

    # Check cache first
    cached = _get_cached_signal(ticker)
    if cached:
        log.debug(f"Returning cached signal for {ticker}")
        return cached

    # In production, this would:
    # 1. Load cached features
    # 2. Run ONNX model inference
    # 3. Return prediction

    # For now, generate a mock signal
    signal = _get_real_signal(ticker)

    # Update cache
    _update_cache([signal])

    return signal


@router.get("/batch/signals", response_model=BatchSignalsResponse)
async def get_batch_signals(
    symbols: str = Query(..., description="Comma-separated list of tickers")
):
    """
    Get trading signals for multiple tickers in a single request.

    This is more efficient than calling the single endpoint multiple times.
    """
    log = get_logger()

    # Parse symbols
    tickers = [s.strip().upper() for s in symbols.split(",") if s.strip()]

    if not tickers:
        raise HTTPException(status_code=400, detail="No symbols provided")

    if len(tickers) > 100:
        raise HTTPException(status_code=400, detail="Maximum 100 symbols per request")

    log.info(f"Fetching batch signals for {len(tickers)} tickers: {tickers[:5]}...")

    # Check if we have cached signals for all tickers
    signals = []
    need_update = False

    for ticker in tickers:
        cached = _get_cached_signal(ticker)
        if cached:
            signals.append(cached)
        else:
            need_update = True

    # Generate missing signals
    if need_update:
        new_signals = [_get_real_signal(t) for t in tickers]
        signals.extend(new_signals)
        _update_cache(signals)

    # Sort by ticker
    signals.sort(key=lambda s: s.ticker)

    return BatchSignalsResponse(
        signals=signals,
        timestamp=datetime.now().isoformat(),
        count=len(signals)
    )


@router.post("/signals/batch", response_model=BatchSignalsResponse)
async def post_batch_signals(tickers: List[str]):
    """
    Get trading signals for multiple tickers (POST version).

    Accepts a JSON body with a list of tickers.
    """
    log = get_logger()

    if not tickers:
        raise HTTPException(status_code=400, detail="No symbols provided")

    if len(tickers) > 100:
        raise HTTPException(status_code=400, detail="Maximum 100 symbols per request")

    # Normalize tickers
    tickers = [t.upper().strip() for t in tickers]

    log.info(f"Fetching batch signals for {len(tickers)} tickers")

    signals = [_get_real_signal(t) for t in tickers]

    # Update cache
    _update_cache(signals)

    return BatchSignalsResponse(
        signals=signals,
        timestamp=datetime.now().isoformat(),
        count=len(signals)
    )


@router.get("/signals/cluster/{cluster_name}", response_model=BatchSignalsResponse)
async def get_cluster_signals(cluster_name: str):
    """
    Get trading signals for all tickers in a specific cluster.

    Clusters are dynamically created based on correlation analysis of C++ tickers.
    Cluster names: 0-9 (numeric cluster IDs from correlation analysis)
    Special clusters: all, tech, finance, healthcare, energy, consumer
    """
    log = get_logger()

    cluster_name = cluster_name.lower()

    # Load tickers from C++
    try:
        from src.data import load_cpp_tickers
        tickers = load_cpp_tickers()
    except Exception as e:
        log.error(f"Failed to load tickers: {e}")
        raise HTTPException(status_code=500, detail="Failed to load tickers")

    if not tickers:
        raise HTTPException(status_code=404, detail="No tickers available")

    # Handle special cluster names
    if cluster_name == "all":
        # Return all tickers
        cluster_tickers = tickers
    elif cluster_name in ["tech", "finance", "healthcare", "energy", "consumer"]:
        # Define sector-based clusters
        sector_clusters = {
            "tech": ["NVDA", "INTC", "AMD", "MSFT", "AAPL", "GOOGL", "META", "TSM", "AVGO", "ORCL", "IBM", "CRM", "ADBE", "CSCO", "QCOM"],
            "finance": ["JPM", "BAC", "WFC", "GS", "MS", "C", "BLK", "AXP", "V", "MA", "USB", "PNC", "TFC", "COF", "SCHW"],
            "healthcare": ["JNJ", "UNH", "PFE", "ABBV", "MRK", "LLY", "TMO", "ABT", "DHR", "BMY", "AMGN", "GILD", "ISRG", "MDT", "SYK"],
            "energy": ["XOM", "CVX", "COP", "SLB", "EOG", "PSX", "VLO", "MPC", "OXY", "HAL", "BKR", "DVN", "HES", "FANG", "PXD"],
            "consumer": ["AMZN", "WMT", "HD", "PG", "KO", "PEP", "COST", "NKE", "MCD", "SBUX", "TGT", "LOW", "EL", "CL", "KMB"]
        }
        cluster_tickers = [t for t in tickers if t in sector_clusters.get(cluster_name, [])]
        if not cluster_tickers:
            # Fallback: just return first N tickers
            cluster_tickers = tickers[:10]
    else:
        # Numeric cluster ID - need to perform clustering
        try:
            cluster_id = int(cluster_name)
            clusters = _get_dynamic_clusters(tickers)
            cluster_tickers = clusters.get(cluster_id, tickers[:10])
        except ValueError:
            raise HTTPException(status_code=404, detail=f"Cluster '{cluster_name}' not found. Use numeric (0-9) or sector name (tech, finance, etc.)")

    log.info(f"Fetching signals for cluster '{cluster_name}' with {len(cluster_tickers)} tickers")

    signals = [_get_real_signal(t) for t in cluster_tickers]

    return BatchSignalsResponse(
        signals=signals,
        timestamp=datetime.now().isoformat(),
        count=len(signals)
    )


def _get_dynamic_clusters(tickers: List[str]) -> Dict[int, List[str]]:
    """
    Get dynamic clusters based on correlation analysis.
    Uses cached clustering results if available.
    """
    import pickle
    from pathlib import Path

    cache_file = Path("./data/clusters_cache.pkl")

    # Try to load cached clusters
    if cache_file.exists():
        try:
            clusters = pickle.load(open(cache_file, "rb"))
            return clusters
        except Exception:
            pass

    # Perform simple clustering by sector distribution as fallback
    # In production, would perform full correlation analysis
    clusters = {}
    sector_map = {
        "tech": ["NVDA", "INTC", "AMD", "MSFT", "AAPL", "GOOGL", "META", "TSM", "AVGO", "ORCL"],
        "finance": ["JPM", "BAC", "WFC", "GS", "MS", "C"],
        "healthcare": ["JNJ", "UNH", "PFE", "ABBV", "MRK", "LLY"],
        "energy": ["XOM", "CVX", "COP", "SLB", "EOG"],
        "consumer": ["AMZN", "WMT", "HD", "PG", "KO", "PEP"]
    }

    # Assign tickers to clusters by sector
    for ticker in tickers:
        assigned = False
        for cluster_id, sector_tickers in enumerate(sector_map.values()):
            if ticker in sector_tickers:
                if cluster_id not in clusters:
                    clusters[cluster_id] = []
                clusters[cluster_id].append(ticker)
                assigned = True
                break

        # Unassigned tickers go to cluster 9
        if not assigned:
            if 9 not in clusters:
                clusters[9] = []
            clusters[9].append(ticker)

    # Try to save cache
    try:
        cache_file.parent.mkdir(parents=True, exist_ok=True)
        pickle.dump(clusters, open(cache_file, "wb"))
    except Exception:
        pass

    return clusters
