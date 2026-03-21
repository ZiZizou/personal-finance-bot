# Incomplete Features & Partially Implemented Code

---

## ✅ IMPLEMENTED (March 17, 2026) - Startup Model Training & /run Warning Fix

### Problem
When running `/run` command in telegram_listener, two warning messages appeared:
1. **"Failed to start python API, continuing anyway"**
2. **"ONNX model not found - using fallback signals"**

### Root Cause
1. Python API was only started when `/run` was executed, not on telegram_listener startup
2. No ONNX model was trained on startup - users had to manually run `/train`

### Solution

#### 1. telegram_listener.py - Startup API + Training
Modified `main()` function in `Trading_cpp/telegram_listener.py`:
- Starts Python API automatically on telegram_listener startup
- Loads tickers from C++ tickers.csv
- Calls `/api/models/select-tickers` to train initial models
- Copies trained model to C++ models directory

#### 2. Use Shared venv
Changed from system Python to shared venv:
```python
PYTHON_VENV_DIR = "C:\\Users\\Atharva\\Documents\\Trading_super\\venv"
PYTHON_VENV_PYTHON = os.path.join(PYTHON_VENV_DIR, "Scripts", "python.exe")
```

#### 3. Fixed Syntax Error in models.py
Fixed missing closing parenthesis in `Trading_Python/src/api/routes/models.py` line 12

#### 4. Fixed Training Bug in ticker_manager.py
Fixed error: `could not convert string to float: 'AAPL'`
- Added code to drop 'ticker' column before training:
```python
if 'ticker' in features.columns:
    features = features.drop('ticker', axis=1)
```

### Files Modified
| File | Change |
|------|--------|
| `Trading_cpp/telegram_listener.py` | Added startup API + training |
| `Trading_Python/src/api/routes/models.py` | Fixed syntax error |
| `Trading_Python/src/service/ticker_manager.py` | Fixed ticker column bug |

### Status: ✅ COMPLETE (March 17, 2026)

---

## ✅ IMPLEMENTED (March 16, 2026) - Dynamic ONNX Model System

The following features have been implemented in this update:

### 8. Dynamic ONNX Model Management System
- **Files:**
  - `Trading_Python/src/service/ticker_manager.py` (NEW)
  - `Trading_Python/src/api/routes/models.py` (NEW)
  - `Trading_cpp/telegram_listener.py` (MODIFIED)
  - `Trading_cpp/LiveSignals.h` (MODIFIED)
  - `Trading_cpp/LiveSignals.cpp` (MODIFIED)
  - `Trading_cpp/Config.h` (MODIFIED)
  - `Trading_cpp/Providers.h` (MODIFIED)
  - `Trading_cpp/Providers.cpp` (MODIFIED)
  - `Trading_Python/src/api/main.py` (MODIFIED)

- **Functionality:**
  - Hybrid approach: Telegram tries Python API first, falls back to C++ CSV
  - C++ dynamically selects up to 7 "hot" tickers based on volatility (ATR) + sentiment
  - Python TickerManager maintains exactly 7 ONNX models
  - C++ reports selected tickers to Python via `/api/models/select-tickers`
  - Signal generation: C++ tries Python API first for selected tickers, falls back to native

- **New API Endpoints:**
  - `POST /api/models/select-tickers` - Receive selected tickers from C++
  - `GET /api/models/status` - Get active model info
  - `GET /api/models/signal/{ticker}` - Get signal from ONNX model

- **Configuration (Environment Variables):**
  - `USE_PYTHON_SIGNALS=true` - Enable dynamic model system
  - `MAX_PYTHON_MODELS=7` - Maximum hot tickers
  - `PYTHON_SIGNALS_URL=http://localhost:8000` - Python API URL
  - `PYTHON_API_URL=http://localhost:8000` - Telegram API URL

- **Status:** ✅ COMPLETE

---

## ✅ IMPLEMENTED (March 16, 2026)

The following features have been implemented in this update:

### 6. API Signal Generation - Real Inference
- **File:** `src/api/routes/signals.py`
- **Changed:** Replaced `_generate_mock_signal()` with `_get_real_signal()`
- **Functionality:**
  - First checks SignalCache for pre-computed signals from scheduler
  - Falls back to FeatureCache for cached features
  - Attempts ONNX model inference if available
  - Falls back to technical analysis (RSI/MACD) if no model
  - Final fallback to real-time Yahoo Finance data fetch
  - Includes `_calculate_rsi()` for real-time RSI computation
- **Also Updated:** Cluster endpoint now loads tickers dynamically from C++ tickers.csv
  - Supports sector clusters (tech, finance, healthcare, energy, consumer)
  - Supports numeric clusters (0-9) with cached correlation-based clustering
  - Special "all" cluster returns all tickers
- **Status:** ✅ COMPLETE

### 7. Model Auto-Selection
- **File:** `src/models/__init__.py`
- **Added:** `choose_model()` and `get_model_for_task()` functions
- **Functionality:**
  - Automatically selects best model (Ridge, RF, or GB) based on:
    - Data size (small → Ridge, large → GB)
    - Compute budget (low → Ridge, high → RF/GB)
    - Trading frequency (high freq → RF, low freq → Ridge)
  - Only lightweight models included (no RL as requested)
- **Usage:**
  ```python
  from src.models import choose_model, get_model_for_task

  # Auto-select based on data characteristics
  model_type = choose_model(data_size=10000, compute_budget=20, frequency="1d")

  # Get model for task
  model = get_model_for_task("regression", data_size=5000, compute_budget=10)
  ```
- **Status:** ✅ COMPLETE

---

## ✅ IMPLEMENTED (March 2026)

The following features have been implemented in the recent update:

### 1. Ticker Loader Function
- **File:** `src/data/__init__.py`
- **Added:** `load_cpp_tickers()` function
- **Functionality:** Loads tickers from `Trading_cpp/tickers.csv`
- **Status:** ✅ COMPLETE - Returns 70 tickers

### 2. Feature Cache Module (NEW)
- **File:** `src/service/feature_cache.py` (NEW FILE)
- **Classes:** `FeatureCache`, `SignalCache`
- **Functionality:**
  - Caches features as pickle files in `./data/features/`
  - TTL support (15 min default)
  - Signal caching as JSON in `./data/signals/`
- **Status:** ✅ COMPLETE

### 3. update_features() Implementation
- **File:** `src/service/scheduler.py`
- **Functionality:**
  - Loads tickers via `load_cpp_tickers()`
  - Fetches recent OHLCV via `fetch_yahoo_data()` (60 days)
  - Calculates indicators via `FeatureEngine`
  - Caches features using FeatureCache
- **Status:** ✅ COMPLETE

### 4. generate_signals() Implementation
- **File:** `src/service/scheduler.py`
- **Functionality:**
  - Loads cached features via FeatureCache
  - Attempts ONNX model inference (with onnxruntime)
  - Falls back to technical analysis (RSI/MACD) if no model
  - Stores signals via SignalCache
- **Status:** ✅ COMPLETE (with fallback)

### 5. retrain_models() Implementation
- **File:** `src/service/scheduler.py`
- **Functionality:**
  - Loads recent training data (last 2 years)
  - Performs correlation-based clustering (scipy)
  - Trains Ridge models per cluster
  - Exports to ONNX via `train_and_export()`
  - Creates `reload.signal` file for C++ notification
- **Status:** ✅ COMPLETE

---

## 1. Scheduler Service (`src/service/scheduler.py`) [UPDATED]

### Issue: Empty Job Implementations

The scheduler is configured but the actual job logic is not implemented:

```python
# Current state - scheduler.py lines 72-140

def scrape_news(self):
    """Background news scraping task"""
    logger.info("Starting scheduled news scraping...")
    try:
        from src.news import NewsScraper, NewsDatabase
        scraper = NewsScraper(...)
        articles = scraper.run(...)
        logger.info(f"News scraping completed. Fetched {len(articles)} articles")
    except Exception as e:
        logger.error(f"News scraping failed: {e}")
    # ✅ This one WORKS - uses existing NewsScraper

def update_features(self):
    """Background feature update task"""
    logger.info("Starting scheduled feature update...")
    try:
        # TODO: Implement actual training
        # 1. Load ticker list from C++
        # 2. Fetch latest price data
        # 3. Calculate technical indicators
        # 4. Cache features for fast access
        logger.info("Feature update completed")
    except Exception as e:
        logger.error(f"Feature update failed: {e}")
    # ❌ STUB ONLY - needs implementation

def generate_signals(self):
    """Background signal generation task"""
    logger.info("Starting scheduled signal generation...")
    try:
        # TODO: Implement actual training
        # 1. Load cached features
        # 2. Run ONNX model inference
        # 3. Cache signals for API access
        logger.info("Signal generation completed")
    except Exception as e:
        logger.error(f"Signal generation failed: {e}")
    # ❌ STUB ONLY - currently returns mock data in API

def retrain_models(self):
    """Background model retraining task"""
    logger.info("Starting scheduled model retraining...")
    try:
        # TODO: Implement actual training
        # 1. Load recent training data
        # 2. Retrain ONNX models
        # 3. Export new models
        # 4. Reload models for inference
        logger.info("Model retraining completed")
    except Exception as e:
        logger.error(f"Model retraining failed: {e}")
    # ❌ STUB ONLY - no training logic
```

### What Was Implemented

1. **`update_features()`** ✅:
   - Load ticker list from `Trading_cpp/tickers.csv` via `load_cpp_tickers()`
   - Fetch latest OHLCV for all tickers via `fetch_yahoo_data()`
   - Calculate technical indicators via `FeatureEngine`
   - Cache features to pickle files in `./data/features/`

2. **`generate_signals()`** ✅:
   - Load cached features via `FeatureCache`
   - Run ONNX inference (with onnxruntime) - falls back to RSI/MACD
   - Store signals as JSON in `./data/signals/`

3. **`retrain_models()`** ✅:
   - Load training data (last 2 years)
   - Perform dynamic clustering (correlation-based using scipy)
   - Train Ridge models per cluster
   - Export to ONNX via `train_and_export()`
   - Create `reload.signal` file for C++ notification

---

## 2. API Signal Generation (`src/api/routes/signals.py`)

### Issue: Mock Data

The signal endpoints currently return mock data:

```python
# signals.py - _generate_mock_signal()

def _generate_mock_signal(ticker: str) -> Signal:
    """Generate a mock signal for testing"""
    import random
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
        indicators={...},  # Random values
        source="python_api"
    )
```

### What Needs Implementation

Replace mock with real inference:
```python
def _generate_real_signal(ticker: str) -> Signal:
    # 1. Load cached features for ticker
    # 2. Run ONNX model
    # 3. Convert prediction to signal
    # 4. Return Signal object
```

---

## 3. Dynamic Clustering

### Issue: Hardcoded Clusters

Currently uses static cluster definitions:

```python
# signals.py lines 226-235

clusters = {
    "tech": ["NVDA", "INTC", "AMD", "MSFT", "AAPL", "GOOGL", "META", "TSM", "AVGO", "ORCL"],
    "finance": ["JPM", "BAC", "WFC", "GS", "MS", "C", "BLK", "AXP", "V", "MA"],
    "healthcare": ["JNJ", "UNH", "PFE", "ABBV", "MRK", "LLY", "TMO", "ABT", "DHR", "BMY"],
    "energy": ["XOM", "CVX", "COP", "SLB", "EOG", "PSX", "VLO", "MPC", "OXY", "HAL"],
    "consumer": ["AMZN", "WMT", "HD", "PG", "KO", "PEP", "COST", "NKE", "MCD", "SBUX"],
}
```

### What Needs Implementation

Dynamic clustering based on:
- **Price correlation** - tickers that move together
- **Sector** - same industry
- **Volatility profile** - similar risk
- **Market cap** - similar size

```python
# Proposed implementation:
from sklearn.cluster import KMeans
import numpy as np

def cluster_tickers(tickers: List[str], n_clusters: int = 5) -> Dict[str, List[str]]:
    # 1. Fetch price data for all tickers
    prices = fetch_yahoo_data(tickers, start="1y")

    # 2. Calculate returns correlation matrix
    returns = prices.pct_change().dropna()
    corr_matrix = returns.corr()

    # 3. Convert to distance matrix
    distance = 1 - corr_matrix.abs()

    # 4. Hierarchical clustering
    from scipy.cluster.hierarchy import linkage, fcluster
    Z = linkage(distance, method='ward')
    clusters = fcluster(Z, n_clusters, criterion='maxclust')

    # 5. Return dict mapping cluster_id -> tickers
```

---

## 4. Batch Training Pipeline

### Issue: No Cluster-Based Training

Currently can only train single-ticker models:

```bash
# Current - train one ticker at a time
python scripts/train_model.py --ticker NVDA --model ridge
python scripts/train_model.py --ticker INTC --model ridge
# ... repeat for 71 tickers
```

### What Needs Implementation

Batch training per cluster:

```python
# Proposed: train_cluster_models.py
def train_cluster_models():
    # 1. Get ticker list
    tickers = load_tickers("Trading_cpp/tickers.csv")

    # 2. Perform dynamic clustering
    clusters = cluster_tickers(tickers, n_clusters=10)

    # 3. Train one model per cluster
    for cluster_name, cluster_tickers in clusters.items():
        print(f"Training cluster: {cluster_name}")

        # Combine data from all tickers in cluster
        data = fetch_yahoo_data(cluster_tickers, "2020-01-01")

        # Add features
        engine = FeatureEngine()
        features = engine.add_all_indicators(data)

        # Create cluster-specific target
        features['signal'] = ...

        # Train model
        trainer, onnx_path = train_and_export(
            features,
            target_col="signal",
            model_type="ridge",
            output_dir=f"./models/clusters/{cluster_name}",
            model_name=f"cluster_{cluster_name}"
        )

        # Save cluster metadata
        save_cluster_config(cluster_name, cluster_tickers, onnx_path)

    print("Cluster training complete!")
```

---

## 5. Model Auto-Selection

### Issue: Manual Model Choice

Currently must specify model manually:
```bash
python scripts/train_model.py --model ridge  # or rf, gb
python scripts/train_rl.py --algo PPO       # RL is separate
```

### What Needs Implementation

Auto-select based on:
- Data size
- Compute budget
- Trading frequency

```python
def choose_model(
    data_size: int,
    compute_budget: float,
    frequency: str
) -> str:
    """
    Choose best model type automatically.

    Returns: 'ridge', 'rf', 'gb', 'ppo', or 'sac'
    """

    # High frequency trading → RL
    if frequency in ["1m", "5m", "15m"]:
        if compute_budget > 100:
            return "ppo"  # RL for high frequency
        else:
            return "gb"   # Fallback to gradient boosting

    # Large datasets → RL
    if data_size > 50000:
        if compute_budget > 100:
            return "sac"  # RL for big data
        else:
            return "gb"

    # Small/medium data → Simple models
    return "ridge"  # Default to simplest
```

---

## 6. Ticker List Integration [IMPLEMENTED ✅]

### Previously: C++ Tickers Not Loaded

The Python service didn't load tickers from C++:

```python
# Old state: Hardcoded in signals.py
clusters = {
    "tech": ["NVDA", "INTC", "AMD", ...],  # Static list
}
```

### Now Implemented

```python
# In src/data/__init__.py
def load_cpp_tickers(tickers_path: Optional[str] = None) -> List[str]:
    """Load ticker list from C++ project's tickers.csv"""
    import csv

    if tickers_path is None:
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        tickers_path = os.path.join(project_root, "Trading_cpp", "tickers.csv")

    tickers = []
    with open(tickers_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            ticker = row.get('symbol') or row.get('ticker')
            if ticker:
                tickers.append(ticker.strip())

    return tickers

# Usage in scheduler:
tickers = load_cpp_tickers()  # Get 70 tickers from C++
```

---

## 7. Model Versioning & Reload

### Issue: No Model Management

When models are retrained, C++ doesn't know to reload them.

### What Needs Implementation

1. **Versioning**:
```python
# Save model with timestamp
onnx_path = f"./models/clusters/{cluster}/v{timestamp}.onnx"

# Keep last N versions
cleanup_old_versions("./models", keep=5)
```

2. **C++ Notification**:
```python
# Option 1: Polling - C++ checks file timestamp
# Option 2: Signal file
with open("./models/reload.signal", "w") as f:
    f.write(timestamp)

# Option 3: WebSocket (more complex)
```

---

## 8. Feature Caching [IMPLEMENTED ✅]

### Previously: No Feature Cache

Features were recalculated on every request.

### Now Implemented (NEW FILE: src/service/feature_cache.py)

```python
import pickle
import time
import json
from pathlib import Path
import pandas as pd

class FeatureCache:
    DEFAULT_TTL = 900  # 15 minutes

    def __init__(self, cache_dir: str = "./data/features", ttl: int = DEFAULT_TTL):
        self.cache_dir = Path(cache_dir)
        self.ttl = ttl
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def get(self, ticker: str) -> Optional[pd.DataFrame]:
        cache_file = self.cache_dir / f"{ticker.upper()}.pkl"
        if cache_file.exists():
            age = time.time() - cache_file.stat().st_mtime
            if age < self.ttl:  # Check TTL
                return pickle.load(open(cache_file, "rb"))
        return None

    def set(self, ticker: str, features: pd.DataFrame) -> bool:
        cache_file = self.cache_dir / f"{ticker.upper()}.pkl"
        try:
            pickle.dump(features, open(cache_file, "wb"))
            return True
        except Exception as e:
            return False

    def get_cache_info(self) -> dict:
        """Get cache statistics"""
        # Returns: total_files, fresh_files, expired_files, total_size_bytes

class SignalCache:
    """Separate cache for trading signals as JSON"""
    def __init__(self, cache_dir: str = "./data/signals"):
        self.cache_dir = Path(cache_dir)
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def get_signal(self, ticker: str) -> Optional[dict]:
        # Load JSON signal file
        pass

    def set_signal(self, ticker: str, signal: dict) -> bool:
        # Save JSON signal file
        pass
```

---

## 9. Sentiment History Bug

### Issue: Wrong Variable Name

In `src/api/routes/sentiment.py`:

```python
# Line 135 - bug
signals=sentiments,  # Should be: sentiments=sentiments

# This causes: NameError: name 'signals' is not defined
```

### Fix Applied ✅

```python
# Corrected to:
sentiments=sentiments,
```

---

## 10. Qlib Integration [OPTIONAL FUTURE ENHANCEMENT]

[Qlib](https://github.com/microsoft/qlib) is Microsoft's open-source AI-oriented quantitative investment platform. It offers institutional-grade features that could enhance the current system.

### Overview

| Component | Qlib Provides | Current Code |
|-----------|--------------|--------------|
| Data | Point-in-time database, operators | yfinance, custom CSV |
| Features | Alpha158 (158 factors) | 20+ technical indicators |
| Models | LightGBM, Tabnet, TCN | Ridge, RF, GB |
| Backtest | Cross-sectional portfolios | Single-stock |

### What Could Be Added

1. **Alpha158 Factor Library** (Recommended)
   - 158 pre-built alpha factors
   - Categories: Price Volume, Trend, Volatility, Money Flow, etc.
   - Easy to add as optional import

2. **LightGBM Models**
   - Optimized gradient boosting for finance
   - However, ONNX export must remain custom

3. **Point-in-Time Data**
   - Prevents lookahead bias
   - More complex setup

### Important Notes

- **ONNX Export MUST REMAIN CUSTOM** - Qlib doesn't support ONNX export, which is required for C++ integration
- **Technical Indicators** - Current 20+ indicators are well-implemented and match C++ code
- **Optional Enhancement** - Not required for basic functionality

### Example: Adding Alpha158

```python
# Could be added to src/features/__init__.py
from qlib.contrib.data.feature import FeatureAlpha158

def get_qlib_alpha_factors(ticker: str, start: str, end: str) -> pd.DataFrame:
    """Get Alpha158 factors"""
    fa = FeatureAlpha158(inst_index=ticker, freq="day")
    return fa.to_array()
```

---

## 10. Summary: What To Implement

### Completed ✅ (March 16, 2026)

| Priority | Feature | Effort | Impact | Status |
|----------|---------|--------|--------|--------|
| 6 | Model auto-selection | Low | Medium | ✅ DONE (March 16, 2026) |
| 8 | Real signal generation (API) | Medium | High | ✅ DONE (March 16, 2026) |

### Completed ✅ (March 2026)

| Priority | Feature | Effort | Impact | Status |
|----------|---------|--------|--------|--------|
| 1 | Fix scheduler job logic | Medium | High | ✅ DONE |
| 2 | Load C++ tickers | Low | High | ✅ DONE |
| 3 | Feature caching | Low | Medium | ✅ DONE |
| 7 | Model versioning/reload | Medium | Medium | ✅ DONE |

### Remaining

| Priority | Feature | Effort | Impact | Status |
|----------|---------|--------|--------|--------|
| 4 | Dynamic clustering | Medium | High | 🔄 Partially done (in retrain_models) |
| 5 | Batch cluster training | Medium | High | 🔄 Partially done (in retrain_models) |

---

## 11. Example: Complete Training Pipeline

Here's what the full implementation would look like:

```python
# scripts/train_cluster_models.py

import os
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from datetime import datetime
from src.data import fetch_yahoo_data
from src.features import FeatureEngine
from src.models import train_and_export
from sklearn.cluster import KMeans
import numpy as np
import pandas as pd

def load_tickers():
    """Load tickers from C++ project"""
    tickers = []
    with open("../Trading_cpp/tickers.csv", "r") as f:
        import csv
        reader = csv.DictReader(f)
        for row in reader:
            tickers.append(row["ticker"])
    return tickers

def cluster_tickers(tickers, n_clusters=10):
    """Cluster tickers by correlation"""
    print(f"Fetching data for {len(tickers)} tickers...")
    data = fetch_yahoo_data(tickers, "2023-01-01", interval="1d")

    # Calculate correlation
    returns = data.pct_change().dropna()
    corr = returns.corr()

    # Simple clustering by sector (placeholder)
    # In production: use KMeans on returns
    clusters = {}
    for i, ticker in enumerate(tickers):
        cluster = i % n_clusters
        if cluster not in clusters:
            clusters[cluster] = []
        clusters[cluster].append(ticker)

    return clusters

def main():
    print("=== Cluster Model Training ===")

    # Load tickers
    tickers = load_tickers()
    print(f"Loaded {len(tickers)} tickers")

    # Cluster
    clusters = cluster_tickers(tickers, n_clusters=10)

    # Train per cluster
    engine = FeatureEngine()
    os.makedirs("./models/clusters", exist_ok=True)

    for cluster_id, cluster_tickers in clusters.items():
        print(f"\n--- Cluster {cluster_id}: {len(cluster_tickers)} tickers ---")

        # Fetch data
        data = fetch_yahoo_data(cluster_tickers, "2022-01-01")
        if data.empty:
            continue

        # Features
        features = engine.add_all_indicators(data)
        features['signal'] = np.where(features['Close'].pct_change() > 0, 1, -1)
        features = features.dropna()

        # Train
        output_dir = f"./models/clusters/cluster_{cluster_id}"
        os.makedirs(output_dir, exist_ok=True)

        trainer, onnx_path = train_and_export(
            features,
            target_col="signal",
            model_type="ridge",
            output_dir=output_dir,
            model_name=f"cluster_{cluster_id}_ridge"
        )

        print(f"  Saved: {onnx_path}")

    print("\n=== Training Complete ===")

if __name__ == "__main__":
    main()
```
