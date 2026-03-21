# Architecture Gaps: Missing Links Between Components

---

## Current System Overview

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    CURRENT STATE                                            │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                             │
│  ┌─────────────────────┐                      ┌─────────────────────┐                      │
│  │   TELEGRAM CHAT     │                      │   TELEGRAM CHAT     │                      │
│  └─────────┬───────────┘                      └─────────┬───────────┘                      │
│            │                                            │                                   │
│            │ Commands: /run, /signals, /fundamentals    │ Alerts: BUY/SELL/HOLD           │
│            ▼                                            ▼                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    telegram_listener.py (Python)                             │            │
│  │  - Polls Telegram API every 2.5 min                                        │            │
│  │  - Reads live_signals.csv                                                  │            │
│  │  - Fetches Yahoo Finance directly                                          │            │
│  │  - Can run C++ bot via subprocess                                          │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            │ Reads: live_signals.csv                                            │
│            │ Runs: trading_bot.exe                                              │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TRADING-CPP (C++)                                        │            │
│  │                                                                              │            │
│  │  - LiveSignalsRunner: fetches data, generates signals                        │            │
│  │  - Technical Analysis: RSI, MACD, ADX, Bollinger                            │            │
│  │  - MLPredictor: native C++ ML model                                         │            │
│  │  - SentimentAnalyzer: GGUF sentiment (2.3GB)                               │            │
│  │  - ONNXPredictor: loads ONNX models (if trained)                           │            │
│  │  - Writes: live_signals.csv                                                 │            │
│  │  - Sends: Telegram alerts via TelegramNotifier                            │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

                                        ▼ MISSING LINKS ▼

┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    DESIRED STATE                                           │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                             │
│  ┌─────────────────────┐                      ┌─────────────────────┐                      │
│  │   TELEGRAM CHAT     │                      │   TELEGRAM CHAT     │                      │
│  └─────────┬───────────┘                      └─────────┬───────────┘                      │
│            │                                            │                                   │
│            │ Commands                                    │ Alerts                            │
│            ▼                                            ▼                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    telegram_listener.py                                      │            │
│  │                                                                              │            │
│  │  Should call:                                                                │            │
│  │  - GET /api/signals/{ticker}  ← Python API (new)                          │            │
│  │  - GET /api/sentiment/{ticker} ← Python API (new)                         │            │
│  │                                                                              │            │
│  │  Currently calls: (MISSING CONNECTION)                                      │            │
│  │  - Reads live_signals.csv (C++ output)                                     │            │
│  │  - Fetches Yahoo Finance directly                                          │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            │ MISSING: Should fetch from Python API                               │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TRADING-PYTHON API (FastAPI) - NEW                        │            │
│  │                                                                              │            │
│  │  Endpoints:                                                                  │            │
│  │  GET /api/signals/{ticker}     → Returns signal (mock for now)            │            │
│  │  GET /api/batch/signals        → Batch signals                             │            │
│  │  GET /api/sentiment/{ticker}   → Returns sentiment (mock for now)         │            │
│  │  GET /api/health               → Health check                              │            │
│  │                                                                              │            │
│  │  Background Jobs (scheduler.py - UPDATED March 2026):                      │            │
│  │  - scrape_news()       → Currently works via NewsScraper                  │            │
│  │  - update_features()   → ✅ IMPLEMENTED (FeatureCache)                    │            │
│  │  - generate_signals()  → ✅ IMPLEMENTED (ONNX + fallback)                │            │
│  │  - retrain_models()    → ✅ IMPLEMENTED (cluster-based + ONNX)           │            │
│  │                                                                              │            │
│  │  NEW (March 2026):                                                          │            │
│  │  - FeatureCache: pickle cache in ./data/features/                          │            │
│  │  - SignalCache: JSON cache in ./data/signals/                             │            │
│  │  - load_cpp_tickers(): loads 70 tickers from C++                          │            │
│  │  - reload.signal file for C++ notification                                │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            │ MISSING: Data flow to/from C++                                     │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TRADING-CPP (C++)                                        │            │
│  │                                                                              │            │
│  │  ✓ Already has PythonSignalProvider (just added)                          │            │
│  │  ✓ Config: PYTHON_SERVICE_URL, USE_PYTHON_SERVICE                         │            │
│  │                                                                              │            │
│  │  MISSING:                                                                   │            │
│  │  - Actually USE PythonSignalProvider in LiveSignalsRunner                 │            │
│  │  - Train and export ONNX models in Trading-Python                         │            │
│  │  - Load cluster-based models                                               │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Missing Architectural Links

### Link 1: Telegram Listener → Python API

**Current State:**
```
telegram_listener.py                    Python API (FastAPI)
       │                                      │
       │  Reads live_signals.csv              │
       │  (C++ output file)                  │
       │          ✗                          │
       │                                     │
       │  Fetches Yahoo Finance directly      │
       │  (redundant with C++)               │
       └───────────────✗─────────────────────┘
```

**What's Missing:**
- Telegram listener should call Python API instead of reading CSV
- Should use `/api/signals/{ticker}` for real-time signals
- Should use `/api/sentiment/{ticker}` for sentiment data

**Code Change Needed:**
```python
# telegram_listener.py - what SHOULD happen:

def get_signals():
    """Get signals from Python API instead of CSV"""
    response = requests.get("http://localhost:8000/api/batch/signals",
                           params={"symbols": "NVDA,INTC,AMD"})
    return response.json()

def get_sentiment(symbol):
    """Get sentiment from Python API"""
    response = requests.get(f"http://localhost:8000/api/sentiment/{symbol}")
    return response.json()
```

---

### Link 2: Python API → Trading-Python Training

**Current State:**
```
Python API (FastAPI)              Trading-Python Scripts
       │                                  │
       │  _generate_mock_signal()         │
       │  (returns random data)           │
       │          ✗                       │
       │                                  │
       │  No connection to:               │
       │  - train_model.py                │
       │  - FeatureEngine                │
       │  - ONNXExporter                  │
       └───────────────✗─────────────────┘
```

**What's Missing:**
- API should call trained ONNX models for real predictions
- Scheduler jobs need implementation
- Feature cache needs to be built

**What Needs Implementation:**

| Scheduler Job | Status | What It Should Do |
|--------------|--------|-------------------|
| `scrape_news()` | ✅ Working | Fetches RSS, classifies, stores in DB |
| `update_features()` | ❌ Stub | Fetch data → Calculate 60+ indicators → Cache |
| `generate_signals()` | ❌ Mock | Load cached features → Run ONNX → Return signals |
| `retrain_models()` | ❌ Stub | Train cluster models → Export ONNX |

**Code Needed in scheduler.py:**
```python
def update_features(self):
    """Update cached features for all tickers"""
    from src.data import fetch_yahoo_data
    from src.features import FeatureEngine

    tickers = load_cpp_tickers()  # Load from Trading_cpp/tickers.csv

    for ticker in tickers:
        data = fetch_yahoo_data(ticker, "2024-01-01")  # Get last year
        engine = FeatureEngine()
        features = engine.add_all_indicators(data)

        # Cache to disk/Redis
        cache_features(ticker, features)

def generate_signals(self):
    """Generate signals from cached features + ONNX"""
    # Load cached features
    # Run ONNX inference
    # Store signals in database
    # Update API cache

def retrain_models(self):
    """Retrain cluster-based models"""
    # 1. Cluster tickers by correlation
    # 2. Train one model per cluster
    # 3. Export to ONNX
    # 4. Create reload signal for C++
```

---

### Link 3: Python API → C++

**Current State:**
```
Python API (FastAPI)              Trading-CPP
       │                                  │
       │  Added PythonSignalProvider     │
       │  (Providers.h/cpp)             │
       │          ✓                      │
       │                                  │
       │  NOT USED IN:                   │
       │  - LiveSignalsRunner            │
       │  - Signal generation            │
       └───────────────✗─────────────────┘
```

**What's Missing:**
- C++ doesn't actually call Python API for signals
- `PythonSignalProvider` exists but isn't wired into the signal flow

**What Needs Implementation:**

In `LiveSignals.cpp`, the signal generation should optionally use Python:
```cpp
// What SHOULD happen in LiveSignalsRunner::processSymbol()

if (Config::getInstance().getUsePythonService()) {
    // Option 1: Get signal from Python API
    auto signalResult = pythonProvider_->getSignal(symbol);
    if (signalResult.isOk()) {
        // Use Python signal
        return mapToLiveSignalRow(signalResult.value());
    }
    // Fallback to native if Python fails
}

// Default: Use native C++ signal generation (current behavior)
Signal sig = generateSignal(...);
```

---

### Link 4: Trading-Python Training → ONNX Models

**Current State:**
```
scripts/train_model.py           models/ (empty directory)
       │                                  │
       │  Can train single ticker:        │
       │  python train_model.py --ticker NVDA  │
       │          ✗                       │
       │                                  │
       │  MISSING:                        │
       │  - Cluster-based training        │
       │  - Batch training               │
       │  - Auto model selection          │
       │  - Daily retraining              │
       └───────────────✗─────────────────┘
```

**What's Missing:**

| Feature | Current | Needed |
|---------|---------|--------|
| Single ticker training | ✅ Works | - |
| Cluster-based training | ❌ Missing | Group tickers, train per cluster |
| Batch training | ❌ Missing | Train all clusters automatically |
| Daily retraining | ❌ Missing | Scheduler job to retrain |
| Model versioning | ❌ Missing | Keep last N versions |
| C++ model reload | ❌ Missing | Signal C++ to reload |

**What Needs Implementation:**

```python
# scripts/train_cluster_models.py (NEEDS TO BE CREATED)

def train_cluster_models():
    # 1. Load tickers from C++
    tickers = load_tickers("../Trading_cpp/tickers.csv")  # 71 tickers

    # 2. Dynamic clustering (not hardcoded)
    clusters = cluster_by_correlation(tickers, n_clusters=10)

    # 3. Train one model per cluster
    for cluster_name, cluster_tickers in clusters.items():
        data = fetch_yahoo_data(cluster_tickers, start="2022-01-01")
        features = FeatureEngine().add_all_indicators(data)
        trainer, onnx_path = train_and_export(features, ...)

    # 4. Save cluster metadata
    save_cluster_config(clusters)

    # 5. Signal C++ to reload
    with open("models/reload.signal", "w") as f:
        f.write(datetime.now().isoformat())
```

---

### Link 5: C++ Tickers → Python

**Current State:**
```
Trading_cpp/tickers.csv           Trading-Python
       │                                  │
       │  71 tickers: NVDA, INTC, ...    │
       │          ✗                       │
       │                                  │
       │  Hardcoded in signals.py:        │
       │  clusters = {"tech": [NVDA, ...]}│
       └───────────────✗─────────────────┘
```

**What's Missing:**
- Python doesn't read tickers from C++
- Hardcoded clusters instead of dynamic

**What Needs Implementation:**
```python
def load_cpp_tickers():
    """Load tickers from C++ project"""
    import csv
    tickers = []
    with open("../Trading_cpp/tickers.csv", "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            tickers.append(row["symbol"])
    return tickers
```

---

## Summary: All Missing Links [UPDATED - March 17, 2026]

| # | Link | From | To | Status |
|---|------|------|-----|--------|
| 1 | Telegram → API | telegram_listener.py | Python API | ✅ Connected (March 2026) |
| 2 | API → Training | scheduler.py | train_model.py | ✅ Implemented (March 2026) |
| 3 | API → ONNX | signals.py | ONNX models | ✅ Implemented (March 16, 2026) |
| 4 | API → C++ | PythonSignalProvider | LiveSignalsRunner | ❌ Not wired in |
| 5 | Training → Clusters | train_model.py | Cluster models | ✅ Implemented (March 2026) |
| 6 | C++ → Python | tickers.csv | Python | ✅ Implemented (March 2026) |
| 7 | Startup Training | telegram_listener | API/Models | ✅ Implemented (March 17, 2026) |

---

## Implementation Priority [UPDATED - March 16, 2026]

### ✅ COMPLETED: Priority 3 - Implement Scheduler Jobs (March 2026)
- Implemented in `src/service/scheduler.py`:
  - `update_features()`: Fetches data, calculates features, caches to pickle
  - `generate_signals()`: Runs ONNX inference with TA fallback
  - `retrain_models()`: Cluster-based training with ONNX export

### ✅ COMPLETED: Priority 4 - Cluster-Based Training (March 2026)
- Dynamic clustering using scipy correlation analysis
- Per-cluster Ridge models trained automatically
- `reload.signal` file created for C++ notification

### ✅ COMPLETED: API Real Signal Generation (March 16, 2026)
- Replaced mock signals with real inference in `src/api/routes/signals.py`
- Uses cached features + ONNX + technical analysis fallback
- Cluster endpoint now loads tickers dynamically from C++

### ⏳ REMAINING

#### Priority 1: Make Telegram Listener Use Python API

```python
# Quick win - just change telegram_listener.py
def get_signals_from_api():
    response = requests.get("http://localhost:8000/api/batch/signals?symbols=...")
    return response.json()
```

#### Priority 2: Wire C++ to Use Python Signals

```cpp
// In LiveSignalsRunner
if (Config::getInstance().getUsePythonService()) {
    auto result = pythonProvider->getSignal(symbol);
    // Use result
}
```
```

---

## File Changes Required

### New Files to Create

| File | Purpose |
|------|---------|
| `scripts/train_cluster_models.py` | Train ONNX models per cluster |
| `src/service/feature_cache.py` | Cache calculated features |
| `src/service/signal_generator.py` | Real signal generation via ONNX |

### Files to Modify

| File | Change |
|------|--------|
| `telegram_listener.py` | Call Python API instead of reading CSV |
| `scheduler.py` | Implement update_features, generate_signals, retrain_models |
| `signals.py` | Replace mock with real ONNX inference |
| `LiveSignals.cpp` | Use PythonSignalProvider when enabled |

### Files Created (This Session)

| File | Status |
|------|--------|
| `src/api/main.py` | ✅ Created |
| `src/api/routes/*.py` | ✅ Created |
| `src/service/scheduler.py` | ✅ Created (stubs) |
| `scripts/run_service.py` | ✅ Created |
| `Providers.h/cpp` (PythonSignalProvider) | ✅ Added |
| `Config.h` | ✅ Added PYTHON_SERVICE_URL |
| `src/service/feature_cache.py` | ✅ Created |
| `src/data/__init__.py` | ✅ Added load_cpp_tickers() |

---

## Running the System [UPDATED - March 17, 2026]

### Recommended: Start telegram_listener (Auto-starts Everything)
```bash
cd Trading_cpp
python telegram_listener.py

# This automatically:
# 1. Starts Python API using shared venv
# 2. Trains initial ONNX models for 7 tickers
# 3. Polls Telegram every 2.5 minutes for commands
#
# Telegram commands:
# /run - Run trading bot
# /signals - Show signal status
# /help - Show all commands
```

### Manual: Start Python API Separately
```bash
cd Trading_Python
..\venv\Scripts\python.exe scripts/run_service.py

# API runs on http://localhost:8000
# API docs at http://localhost:8000/docs
```

### Manual: Run C++ Trading Bot Directly
```bash
cd Trading_cpp
./TradingBot.exe
```

---

## Optional Enhancement: Qlib Integration

[Qlib](https://github.com/microsoft/qlib) is Microsoft's open-source AI-oriented quantitative investment platform. It offers institutional-grade features that could enhance the current system but is **not required** for basic functionality.

### Qlib Components

| Component | Qlib Provides | Current Implementation |
|-----------|--------------|----------------------|
| Data | Point-in-time database | yfinance + CSV |
| Features | Alpha158 (158 factors) | 20+ technical indicators |
| Models | LightGBM, Tabnet, TCN | Ridge, RF, GB + ONNX |
| Backtest | Cross-sectional portfolios | Single-stock |

### Recommended Integration (Optional)

**Priority 1: Alpha158 Factor Library**
- 158 pre-built alpha factors
- Easy to add as optional import
- Would enhance signal quality

**Priority 2: LightGBM (Optional)**
- Optimized gradient boosting for finance
- Would require custom ONNX export

### Critical: ONNX Export Must Remain Custom

**IMPORTANT**: The current ONNX export pipeline **MUST BE RETAINED** regardless of qlib integration:
- Qlib does NOT support ONNX export
- ONNX is required for C++ trading bot integration
- File: `src/models/__init__.py` - ONNXExporter class

### Files to Preserve

| File | Reason |
|------|--------|
| `src/models/__init__.py` | ONNXExporter - critical for C++ |
| `src/features/__init__.py` | TechnicalIndicators - matches C++ |
| `src/data/__init__.py` | load_cpp_tickers() - custom integration |

---

## 📝 EVOLUTION LOG: March 17, 2026

### UPDATE: Startup Model Training & /run Warning Fix

#### Problem Fixed
When running `/run` command, warnings appeared:
- "Failed to start python API, continuing anyway"
- "ONNX model not found - using fallback signals"

#### Solution Implemented
1. **telegram_listener.py startup logic**: Added automatic Python API start + model training on telegram_listener startup
2. **Shared venv**: Changed from system Python to `Trading_super/venv/Scripts/python.exe`
3. **Bug fixes**:
   - Fixed syntax error in `Trading_Python/src/api/routes/models.py`
   - Fixed ticker column bug in `Trading_Python/src/service/ticker_manager.py`

#### Current Status
- ✅ Python API starts on telegram_listener startup
- ✅ Models trained automatically (7 tickers: AAPL, MSFT, GOOGL, AMZN, META, NVDA, TSLA)
- ✅ Model copied to `Trading_cpp/models/stock_predictor.onnx`
- ✅ `/run` warnings resolved

#### Files Modified
| File | Change |
|------|--------|
| `Trading_cpp/telegram_listener.py` | Added startup API + training, use venv Python |
| `Trading_Python/src/api/routes/models.py` | Fixed syntax error |
| `Trading_Python/src/service/ticker_manager.py` | Fixed ticker column bug |

---

## 📝 EVOLUTION LOG: March 16, 2026

### NEW: Dynamic ONNX Model Management System

A new feature has been implemented to create a dynamic, focused model system where:
1. **Telegram Listener** uses a hybrid approach - tries Python API first, falls back to C++ live_signals.csv
2. **C++** dynamically selects up to 7 "hot" tickers based on volatility analysis and news
3. **Python** maintains ONNX models for only these 7 tickers
4. **C++** uses these models for predictions

#### Architecture Overview (March 2026)

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                         DYNAMIC ONNX MODEL SYSTEM (March 2026)                            │
├─────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                             │
│  ┌─────────────────────┐                      ┌─────────────────────┐                      │
│  │   TELEGRAM CHAT     │                      │   TELEGRAM CHAT     │                      │
│  └─────────┬───────────┘                      └─────────┬───────────┘                      │
│            │                                            │                                   │
│            │ Commands                                    │ Alerts                            │
│            ▼                                            ▼                                   │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    telegram_listener.py                                      │            │
│  │                                                                              │            │
│  │  ✅ IMPLEMENTED (March 2026):                                               │            │
│  │  - GET /api/signals/{ticker}  ← Python API                                │            │
│  │  - GET /api/sentiment/{ticker} ← Python API                               │            │
│  │  - Falls back to live_signals.csv if API unavailable                       │            │
│  │  - Hybrid approach: tries API first, CSV as fallback                       │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TRADING-PYTHON API (FastAPI)                             │            │
│  │                                                                              │            │
│  │  Endpoints (UPDATED March 2026):                                            │            │
│  │  GET /api/signals/{ticker}     → Returns signal (ONNX model)              │            │
│  │  GET /api/batch/signals        → Batch signals                             │            │
│  │  GET /api/sentiment/{ticker}   → Returns sentiment                         │            │
│  │  GET /api/health               → Health check                              │            │
│  │                                                                              │            │
│  │  NEW (March 2026):                                                            │            │
│  │  POST /api/models/select-tickers → C++ reports selected tickers           │            │
│  │  GET /api/models/status         → Get active model info                    │            │
│  │  GET /api/models/signal/{ticker} → Get signal from ONNX model             │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            │ C++ reports selected tickers                                       │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TickerManager (NEW - March 2026)                          │            │
│  │                                                                              │            │
│  │  - Tracks selected tickers from C++                                         │            │
│  │  - Maintains exactly 7 ONNX models maximum                                   │            │
│  │  - Trains/retrains models for selected tickers                              │            │
│  │  - Cleanup unused models                                                    │            │
│  │  - File: src/service/ticker_manager.py                                      │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│            │                                                                     │
│            │ Train models for selected tickers                                  │
│            ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐            │
│  │                    TRADING-CPP (C++)                                        │            │
│  │                                                                              │            │
│  │  ✅ IMPLEMENTED (March 2026):                                               │            │
│  │  - PythonSignalProvider: reports selected tickers to Python               │            │
│  │  - Dynamic ticker selection based on volatility (ATR) + sentiment         │            │
│  │  - Config: USE_PYTHON_SIGNALS, MAX_PYTHON_MODELS, PYTHON_SIGNALS_URL      │            │
│  │  - Falls back to native C++ signals if Python fails                        │            │
│  │                                                                              │            │
│  └─────────────────────────────────────────────────────────────────────────────┘            │
│                                                                                             │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

#### New Configuration Options (March 2026)

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `USE_PYTHON_SIGNALS` | false | Enable dynamic ONNX model system |
| `MAX_PYTHON_MODELS` | 7 | Maximum number of hot tickers |
| `PYTHON_SIGNALS_URL` | http://localhost:8000 | Python API URL |
| `PYTHON_API_URL` | http://localhost:8000 | Telegram API URL (for fallback) |

#### Files Created (March 2026)

| File | Purpose |
|------|---------|
| `Trading_Python/src/service/ticker_manager.py` | NEW - Manages 7 active ONNX models |
| `Trading_Python/src/api/routes/models.py` | NEW - Model management API endpoints |

#### Files Modified (March 2026)

| File | Changes |
|------|---------|
| `telegram_listener.py` | Added Python API calls, hybrid fallback |
| `LiveSignals.h` | Added PythonSignalProvider, ticker selection |
| `LiveSignals.cpp` | Added dynamic ticker selection, volatility analysis |
| `Config.h` | Added USE_PYTHON_SIGNALS, MAX_PYTHON_MODELS |
| `Providers.h` | Added reportSelectedTickers() |
| `Providers.cpp` | Implemented reportSelectedTickers() |
| `Trading_Python/src/api/main.py` | Added models router |
| `Trading_Python/src/service/scheduler.py` | Updated (already had ONNX support) |

#### New API Endpoints (March 2026)

```
POST /api/models/select-tickers
- Body: {"tickers": ["NVDA", "AAPL", "MSFT", ...]}
- Response: {"status": "ok", "tickers": [...], "trained": [...]}

GET /api/models/status
- Response: {"active_models": 7, "max_models": 7, "tickers": [...], "last_update": "..."}

GET /api/models/signal/{ticker}
- Response: {"ticker": "...", "signal": "buy/sell/hold", "confidence": 0.85, ...}
```

#### How It Works (March 2026)

1. **C++ Startup**: LiveSignalsRunner initializes and checks `USE_PYTHON_SIGNALS`
2. **Ticker Selection**: During warmup, C++ calculates volatility (ATR) and sentiment for all tickers
3. **Selection**: Top 7 tickers by volatility + sentiment score are selected
4. **Report to Python**: C++ calls `POST /api/models/select-tickers` to report selected tickers
5. **Model Training**: Python TickerManager trains ONNX models for new tickers
6. **Signal Generation**: C++ calls Python API for signals on selected tickers
7. **Fallback**: If Python fails, C++ uses native signal generation

---

### Previous Evolution Entries

#### February 2026: Initial System Integration

- Created Python FastAPI service
- Added signal and sentiment endpoints
- Added scheduler with background jobs
- Created feature cache system
- Added ONNX model training pipeline
