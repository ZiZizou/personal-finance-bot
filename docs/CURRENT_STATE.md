# Trading-Python & Trading-CPP Documentation

## Current State of the Codebase

---

## 1. Trading_Python - Overview

### 1.1 Project Structure

```
Trading_Python/
├── src/
│   ├── api/                    # FastAPI service (NEW)
│   │   ├── main.py             # FastAPI app
│   │   └── routes/
│   │       ├── health.py       # Health check endpoints
│   │       ├── signals.py      # Signal API endpoints
│   │       ├── sentiment.py    # Sentiment API endpoints
│   │       └── models.py       # Model management endpoints [NEW: March 2026]
│   ├── service/                # Background workers
│   │   ├── scheduler.py        # APScheduler configuration [UPDATED]
│   │   ├── feature_cache.py    # Feature/Signal caching [NEW]
│   │   └── ticker_manager.py   # Dynamic ONNX model management [NEW: March 2026]
│   ├── data/                   # Data fetching & handling
│   │   └── __init__.py        # DataHandler, fetch_yahoo_data [UPDATED: load_cpp_tickers]
│   ├── features/              # Technical indicators
│   │   └── __init__.py        # FeatureEngine, 20+ indicators
│   ├── models/                # ML training & ONNX export
│   │   └── __init__.py        # ModelTrainer, ONNXExporter
│   ├── backtest/              # Backtesting engine
│   │   └── __init__.py        # Backtester, SignalStrategy
│   └── news/                  # News scraping & sentiment
│       ├── scraper.py          # NewsScraper (RSS feeds)
│       ├── classifier.py       # ArticleClassifier
│       ├── database.py         # NewsDatabase (SQLite)
│       ├── extractor.py        # CompanyExtractor
│       ├── ticker_resolver.py  # TickerResolver
│       ├── sources.py          # RSSSource
│       └── storage.py          # CSV export utilities
├── scripts/
│   ├── run_service.py          # Start API service
│   ├── train_model.py          # Train single ticker model
│   ├── train_rl.py             # Train RL agent
│   ├── scrape_news.py          # CLI for news scraping
│   ├── setup_qlib.py           # Qlib setup
│   └── export_compatible.py    # ONNX export utilities
├── data/
│   ├── features/               # Feature cache [NEW]
│   └── signals/                # Signal cache [NEW]
├── models/                     # Trained ONNX models
│   └── active/                # Active ONNX models for selected tickers [NEW: March 2026]
├── requirements.txt
└── venv/
```

---

## 2. Trading_Python - Working Features

### 2.1 Data Module (`src/data/__init__.py`)

**Status: WORKING**

| Function | Description | Usage |
|----------|-------------|-------|
| `fetch_yahoo_data()` | Download OHLCV from Yahoo Finance | `fetch_yahoo_data(["NVDA", "AAPL"], "2020-01-01")` |
| `load_csv_directory()` | Load CSV files from directory | `load_csv_directory("./data")` |
| `load_cpp_tickers()` | Load tickers from C++ CSV file | `load_cpp_tickers()` returns 70 tickers |
| `DataHandler` | Cache and manage data | `DataHandler(cache_dir).get("NVDA")` |
| `calculate_returns()` | Calculate price returns | `calculate_returns(df)` |
| `create_rolling_windows()` | Create ML training windows | `create_rolling_windows(df, window=20)` |

**Supported Intervals**: 1m, 5m, 15m, 30m, 60m, 1d, 1wk, 1mo

---

### 2.2 Features Module (`src/features/__init__.py`)

**Status: WORKING**

Technical indicators implemented:

| Category | Indicators |
|----------|------------|
| **Trend** | SMA, EMA, MACD, ADX, ROC, Momentum |
| **Volatility** | Bollinger Bands, ATR, ATR Ratio, Volatility |
| **Momentum** | RSI, Stochastic, Williams %R, CCI |
| **Volume** | OBV, MFI, VWAP, AD Oscillator |

**FeatureEngine Class**:
```python
from src.features import FeatureEngine

engine = FeatureEngine()
features = engine.add_all_indicators(df)        # Add all indicators
features = engine.add_lagged_features(df, lags=5)  # Add lagged features
features = engine.add_rolling_stats(df, windows=[5, 10, 20])  # Rolling stats
```

**Total Indicators**: 20+ technical indicators

---

### 2.3 Models Module (`src/models/__init__.py`)

**Status: WORKING** [UPDATED - March 16, 2026]

| Class | Description |
|-------|-------------|
| `ModelTrainer` | Train sklearn models (Ridge, RF, GB) |
| `ModelConfig` | Configuration for model training |
| `ONNXExporter` | Export models to ONNX format |
| `train_and_export()` | Convenience function |
| `choose_model()` | Auto-select model based on data (NEW) |
| `get_model_for_task()` | Get recommended model for task (NEW) |

**Supported Models**:
- Ridge Regression
- Random Forest
- Gradient Boosting

**Training Example**:
```python
from src.models import train_and_export

trainer, onnx_path = train_and_export(
    features,
    target_col="signal",
    model_type="ridge",
    output_dir="./models",
    model_name="NVDA_ridge"
)
```

**ONNX Export**: ✅ Full support with input/output validation

**Model Auto-Selection** [NEW - March 16, 2026]:
```python
from src.models import choose_model, get_model_for_task

# Auto-select based on data characteristics
model_type = choose_model(
    data_size=10000,      # Number of samples
    compute_budget=20,    # 1-100 scale
    frequency="1d"        # Trading frequency
)
# Returns: 'ridge', 'rf', or 'gb'

# Or get model for task
model = get_model_for_task(
    task="regression",
    data_size=5000,
    compute_budget=10,
    frequency="1d"
)
```

**Selection Logic**:
- Small datasets (<1000): Ridge (stability)
- Large datasets (>50000): Gradient Boosting (if compute allows)
- High frequency (1m/5m/15m): Random Forest
- Default: Ridge (fast, stable)

---

### 2.4 Backtest Module (`src/backtest/__init__.py`)

**Status: WORKING**

| Class | Description |
|-------|-------------|
| `Backtester` | Historical backtesting engine |
| `BacktestConfig` | Configuration (capital, fees, risk) |
| `BacktestResult` | Results (return, sharpe, drawdown) |
| `SignalStrategy` | Generate signals from predictions |
| `walk_forward_validation()` | Walk-forward validation |

**Configuration Options**:
- Initial capital
- Commission rate
- Slippage
- Stop-loss
- Take-profit

**Metrics Returned**:
- Total return
- Sharpe ratio
- Max drawdown
- Win rate
- Profit factor

---

### 2.5 News Module (`src/news/`)

**Status: WORKING**

| Component | Description |
|-----------|-------------|
| `NewsScraper` | Fetch RSS feeds from multiple sources |
| `ArticleClassifier` | Classify articles (promising/bad/neutral) |
| `CompanyExtractor` | Extract company names from text |
| `TickerResolver` | Map company names to stock tickers |
| `NewsDatabase` | SQLite storage for articles |

**News Sources** (30+ RSS feeds):
- Yahoo Finance
- MarketWatch
- Reuters
- CNBC
- Bloomberg
- Seeking Alpha
- Various industry-specific feeds

**Classification Categories**:
- `promising` - Positive sentiment
- `bad` - Negative sentiment
- `neutral` - Neutral

**Database Schema**:
```sql
articles (
    id, title, url, source, industry,
    published_date, classification, score,
    company_name, ticker, description
)
```

---

### 2.6 API Module (`src/api/`)

**Status: WORKING** [UPDATED - March 16, 2026]

**Endpoints**:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Root info |
| `/api/health` | GET | Health check |
| `/api/ping` | GET | Simple ping |
| `/api/signals/{ticker}` | GET | Get signal for ticker (real inference) |
| `/api/batch/signals` | GET | Get signals for multiple tickers |
| `/api/signals/cluster/{name}` | GET | Get signals by cluster |
| `/api/sentiment/{ticker}` | GET | Get sentiment for ticker |
| `/api/batch/sentiment` | GET | Get batch sentiment |

**Signal Generation** [UPDATED - March 16, 2026]:
- `/api/signals/{ticker}` now uses real inference:
  1. First checks SignalCache (from scheduler)
  2. Falls back to FeatureCache for cached features
  3. Attempts ONNX model inference if available
  4. Falls back to technical analysis (RSI/MACD)
  5. Final fallback to real-time Yahoo Finance data
- `/api/signals/cluster/{name}` now:
  - Loads tickers dynamically from C++ tickers.csv
  - Supports sector clusters: tech, finance, healthcare, energy, consumer
  - Supports numeric clusters (0-9) with cached correlation analysis
  - Special "all" cluster returns all tickers

**Running the Service**:
```bash
cd Trading_Python
python scripts/run_service.py
# Or with custom port:
python scripts/run_service.py --port 8080
```

---

### 2.7 Scheduler Service (`src/service/scheduler.py`) [UPDATED]

**Status: IMPLEMENTED** (March 2026)

| Task | Frequency | Status |
|------|-----------|--------|
| News Scraping | Every 1 hour | ✅ Scraping works |
| Feature Updates | Every 15 min | ✅ Implemented |
| Signal Generation | Every 5 min | ✅ Implemented (with ONNX/fallback) |
| Model Retraining | Daily 16:00 | ✅ Implemented |

**Implementation Details:**

- `update_features()`: Loads 70 tickers from C++, fetches 60 days of OHLCV, calculates indicators via FeatureEngine, caches to pickle files
- `generate_signals()`: Uses FeatureCache + SignalCache, attempts ONNX inference, falls back to RSI/MACD
- `retrain_models()`: Fetches 2 years of data, performs correlation-based clustering, trains Ridge per cluster, exports to ONNX, creates reload.signal

### 2.8 Feature Cache Module (`src/service/feature_cache.py`) [NEW]

**Status: WORKING**

| Class | Description |
|-------|-------------|
| `FeatureCache` | Cache features as pickle files with TTL |
| `SignalCache` | Cache trading signals as JSON |

**Cache Directories:**
- `./data/features/` - Pickle files (one per ticker)
- `./data/signals/` - JSON files (one per ticker)

**Usage:**
```python
from src.service.feature_cache import FeatureCache, SignalCache

# Feature cache
cache = FeatureCache(ttl=900)  # 15 min TTL
cache.set("NVDA", features_df)
features = cache.get("NVDA")

# Signal cache
signal_cache = SignalCache()
signal_cache.set_signal("NVDA", {"signal": "buy", "confidence": 0.7})
signal = signal_cache.get_signal("NVDA")
```

---

## 3. Trading_CPP - Overview

### 3.1 Project Structure

```
Trading_cpp/
├── Providers.h/cpp           # Data providers (Yahoo, Python API)
├── MarketData.h/cpp           # Candle data structures
├── Config.h                   # Configuration (env vars, .env)
├── NetworkUtils.h/cpp         # HTTP/curl utilities
├── MLPredictor.h/cpp         # ML inference wrapper
├── ONNXPredictor.h/cpp       # ONNX Runtime integration
├── SentimentAnalyzer.h/cpp   # News sentiment analysis
├── SentimentService.h/cpp    # Sentiment data fetching
├── TechnicalAnalysis.h/cpp   # Technical indicators (C++)
├── TradingStrategy.h/cpp     # Trading logic
├── RiskManagement.h/cpp     # Position sizing, risk limits
├── Backtester.h/cpp          # Historical backtesting
├── OrderManager.h/cpp        # Order execution
├── Broker.h/cpp               # Broker integration
├── LiveSignals.h/cpp         # Live signal generation
├── NewsManager.h/cpp          # News fetching
├── TelegramNotifier.h/cpp    # Telegram notifications
├── main.cpp                   # Main entry point
├── Config.h                   # Config + Environment class
├── .env                       # API keys & settings
└── vendors/eigen/            # Linear algebra library
```

---

### 3.2 Trading_CPP - Working Features

#### Data Providers
| Provider | Status | Description |
|----------|--------|-------------|
| Yahoo Finance | ✅ Working | OHLCV data via API |
| Python API | ✅ New | Fetch signals from Python service |

#### Market Data
| Feature | Status |
|---------|--------|
| Candle structures | ✅ |
| Historical data fetching | ✅ |
| Real-time bars | ✅ |
| Fundamentals | ✅ |

#### ML/AI
| Feature | Status |
|---------|--------|
| ONNX Runtime inference | ✅ |
| GGUF sentiment model | ✅ |
| Technical indicators | ✅ |
| Regime detection | ✅ |

#### Trading
| Feature | Status |
|---------|--------|
| Backtesting | ✅ |
| Live signals | ✅ |
| Risk management | ✅ |
| Order management | ✅ |
| Telegram alerts | ✅ |

---

## 4. Integration Points

### 4.1 Python → C++ Integration

**REST API** (New):
- Python exposes signals via FastAPI
- C++ fetches via `PythonSignalProvider`
- Config: `PYTHON_SERVICE_URL`, `USE_PYTHON_SERVICE`

**Environment Variables**:
```bash
# .env file
PYTHON_SERVICE_URL=http://localhost:8000
USE_PYTHON_SERVICE=true
```

### 4.2 C++ Tickers

Currently 71 tickers in `tickers.csv` - used for:
- Batch processing
- Signal generation
- Model training (when implemented)

---

## 📝 UPDATE: March 17, 2026 - Startup Model Training & /run Warning Fix

### Problem Fixed
When running `/run` command in telegram_listener, two warning messages appeared:
1. **"Failed to start python API, continuing anyway"**
2. **"ONNX model not found - using fallback signals"**

### Root Cause Analysis
1. **Python API Not Started on Startup**: The `start_python_api_service()` in telegram_listener.py was only called when `/run` was executed, not on startup.
2. **ONNX Model Not Found**: No model was trained on startup - users had to manually run `/train` first.

### Solution Implemented

#### 1. telegram_listener.py - Startup Model Training
Modified `main()` function to:
- Start Python API automatically on telegram_listener startup
- Trigger initial model training via API
- Copy trained model to C++ directory for ONNX inference

**Key Changes**:
```python
# Added to main():
# ===== STARTUP: Start Python API and train initial models =====
print("Starting Python API service...")
if start_python_api_service():
    # Trigger initial model training
    tickers = load_tickers()
    ticker_symbols = [t[0] for t in tickers[:7]]

    url = f"{PYTHON_API_URL}/api/models/select-tickers"
    response = requests.post(url, json={"tickers": selected_tickers})
```

#### 2. Use Shared venv Python
Fixed to use the shared venv at `Trading_super/venv/Scripts/python.exe` instead of system Python:
```python
PYTHON_VENV_DIR = "C:\\Users\\Atharva\\Documents\\Trading_super\\venv"
PYTHON_VENV_PYTHON = os.path.join(PYTHON_VENV_DIR, "Scripts", "python.exe")
```

#### 3. Fixed Bug in models.py (Syntax Error)
Fixed missing closing parenthesis in `Trading_Python/src/api/routes/models.py`:
```python
# Before (syntax error):
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# After (fixed):
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
```

#### 4. Fixed Bug in ticker_manager.py (Training Error)
Fixed error: `could not convert string to float: 'AAPL'`
- The `fetch_yahoo_data()` adds a 'ticker' column to the dataframe
- This column was being passed to the model training causing conversion errors
- Fixed by dropping the 'ticker' column before training:

```python
# Add features
engine = FeatureEngine()
features = engine.add_all_indicators(data)

# Drop ticker column if present (added by fetch_yahoo_data)
if 'ticker' in features.columns:
    features = features.drop('ticker', axis=1)
```

### Files Modified

| File | Change |
|------|--------|
| `Trading_cpp/telegram_listener.py` | Added startup API + training, use venv Python |
| `Trading_Python/src/api/routes/models.py` | Fixed syntax error |
| `Trading_Python/src/service/ticker_manager.py` | Fixed ticker column bug |

### Current Status

- ✅ Python API starts automatically on telegram_listener startup
- ✅ Initial models train on startup (7 tickers: AAPL, MSFT, GOOGL, AMZN, META, NVDA, TSLA)
- ✅ Model copied to `Trading_cpp/models/stock_predictor.onnx`
- ✅ `/run` command no longer shows warnings

### Models Trained (March 17, 2026)
```
Trading_Python/models/active/
├── AAPL.onnx      (487 bytes)
├── AMZN.onnx      (487 bytes)
├── GOOGL.onnx     (487 bytes)
├── META.onnx      (487 bytes)
├── MSFT.onnx      (487 bytes)
├── NVDA.onnx      (487 bytes)
└── TSLA.onnx      (487 bytes)

Trading_cpp/models/
└── stock_predictor.onnx  (copied from AAPL.onnx)
```

---

## 5. Summary Table [UPDATED - March 17, 2026]

| Component | Status | Notes |
|-----------|--------|-------|
| Data Fetching | ✅ Working | Yahoo Finance, CSV |
| Feature Engineering | ✅ Working | 20+ indicators |
| Model Training | ✅ Working | Ridge, RF, GB |
| ONNX Export | ✅ Working | Full validation |
| Backtesting | ✅ Working | Full metrics |
| News Scraping | ✅ Working | RSS feeds, classification |
| Sentiment Analysis | ✅ Working | Article classification |
| API Service | ✅ Working | FastAPI endpoints |
| Scheduler | ✅ Working | All jobs implemented |
| Feature Caching | ✅ Working | Pickle + JSON cache |
| Signal Generation | ✅ Working | ONNX + TA + real-time fallback |
| Dynamic Clustering | ✅ Working | Correlation-based (scipy) |
| Cluster Training | ✅ Working | Per-cluster Ridge models |
| Model Auto-Selection | ✅ Working | Auto (March 16, 2026) |
| API Real Signals | ✅ Working | Real inference (March 16, 2026) |
| C++ → Python Integration | ✅ Working | REST API + signals |
| Startup Model Training | ✅ Working | Auto-train on startup (March 17, 2026) |
| Qlib Integration | ❌ Optional | Not integrated |

---

## 5.1 Qlib Integration Opportunities [FUTURE ENHANCEMENT]

[Qlib](https://github.com/microsoft/qlib) is Microsoft's open-source AI-oriented quantitative investment platform. While the current codebase implements custom solutions, qlib offers several features that could enhance the system.

### What is Qlib?

Qlib is an AI-oriented quantitative investment platform that provides:
- **Data Management**: Point-in-time database, efficient caching
- **Alpha Factors**: Alpha158 library (158 pre-built factors)
- **Model Training**: LightGBM, Tabnet, TCN, and more
- **Backtesting**: Cross-sectional portfolio backtesting, IC analysis
- **Online Serving**: Automatic model rolling

### Integration Analysis

| Module | Current Implementation | Qlib Enhancement | Recommendation |
|--------|----------------------|------------------|----------------|
| **Data** | yfinance, CSV loading | PIT database, operators | Optional - keep custom for simplicity |
| **Features** | 20+ technical indicators | Alpha158 (158 factors) | **HIGH PRIORITY** - Add as optional |
| **Models** | Ridge, RF, GB + ONNX | LightGBM, Tabnet | Medium - keep ONNX export |
| **Backtest** | Single-stock strategy | Cross-sectional portfolios | Optional |

### Recommended Integration Points

**1. Alpha158 Factor Library (Recommended First Step)**

Qlib's Alpha158 provides 158 alpha factors. Current coverage:

| Category | Qlib Factors | Current Coverage |
|----------|-------------|------------------|
| Price Volume | 58 | Partial (returns, SMA, EMA) |
| Trend | 14 | Partial (ADX, momentum) |
| Volatility | 6 | Partial |
| Money Flow | 10 | Partial (MFI) |
| Loopback | 10 | None |
| Others | 60 | Limited |

**Implementation:**
```python
# Could be added to src/features/__init__.py
from qlib.contrib.data.feature import FeatureAlpha158

def get_qlib_factors(ticker: str, start: str, end: str) -> pd.DataFrame:
    """Get Alpha158 factors for a ticker"""
    features = FeatureAlpha158(inst_index=ticker, freq="day").to_array()
    return features
```

**2. LightGBM Models (Optional)**

Qlib's LightGBM is optimized for financial data:

```python
# Could replace sklearn GradientBoosting
from qlib.contrib.model.gbdt import LGBModel
```

**IMPORTANT**: The current ONNX export pipeline **MUST BE RETAINED** - qlib doesn't support ONNX export, which is critical for C++ integration.

**3. Data Operators (Optional)**

Qlib's feature operators:
```python
from qlib.data.ops import Operators
returns = Operators.Return("$close", 1)
ranked = Operators.Rank(returns)
```

### Files to Preserve Regardless

- `src/models/__init__.py` - ONNXExporter class (lines 227-389)
- `src/features/__init__.py` - TechnicalIndicators class
- `src/data/__init__.py` - load_cpp_tickers()

### Future Enhancement Ideas

1. **Add optional qlib data layer** - Use qlib's data format for efficient storage
2. **Integrate Alpha158** - Add as optional factor library
3. **LightGBM training** - Add as alternative to sklearn GB
4. **Cross-sectional backtesting** - If portfolio strategies needed

---

## 6. Running the System [UPDATED - March 17, 2026]

### Quick Start (Recommended)
```bash
# Just start telegram_listener - it will automatically:
# 1. Start the Python API
# 2. Train initial ONNX models
# 3. Begin polling for commands

cd Trading_cpp
python telegram_listener.py

# Then use Telegram commands:
# /run - Run trading bot
# /signals - Show signal status
# /help - Show all commands
```

### Manual Start (Individual Components)

#### Start Python API Only
```bash
# Uses shared venv at Trading_super/venv
cd Trading_Python
..\venv\Scripts\python.exe scripts/run_service.py

# Or with custom port:
..\venv\Scripts\python.exe scripts/run_service.py --port 8080
```

#### Start Python Service (Old Way - Not Recommended)
```bash
cd Trading_Python
python scripts/run_service.py
```

### Run C++ with Python Signals
```bash
# Ensure .env has:
# PYTHON_SERVICE_URL=http://localhost:8000
# USE_PYTHON_SERVICE=true

cd Trading_cpp
./TradingBot.exe
```

### Train a Model
```bash
cd Trading_Python
python scripts/train_model.py --ticker NVDA --start 2020-01-01
```

### Scrape News
```bash
cd Trading_Python
python scripts/scrape_news.py --update
```
