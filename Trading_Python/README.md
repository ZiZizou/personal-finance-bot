# Trading Research Python Environment

Hybrid C++ + Python quantitative research setup for enhanced trading strategies.

## Overview

This Python research environment complements your existing C++ trading bot by providing:
- **Qlib Integration**: Microsoft's AI quantitative investment platform
- **Reinforcement Learning**: Train PPO/SAC agents with Stable-Baselines3
- **Feature Engineering**: 20+ technical indicators (ported from C++)
- **Model Export**: ONNX export compatible with your C++ `ONNXPredictor`
- **Backtesting**: Comprehensive strategy evaluation

## Architecture

```
┌─────────────────────────────────────────────────┐
│         PYTHON RESEARCH ENVIRONMENT             │
│                                                 │
│  ┌─────────┐  ┌─────────┐  ┌─────────────┐      │
│  │  Qlib   │  │ FinRL   │  │ Custom ML   │      │
│  │ (Data)  │  │ (RL)    │  │ (Features)  │      │
│  └────┬────┘  └────┬────┘  └──────┬──────┘      │
│       │             │               │            │
│       └─────────────┼───────────────┘            │
│                     ▼                            │
│              ┌───────────┐                       │
│              │ ONNX Models│                      │
│              └───────────┘                       │
└────────────────────┬─────────────────────────────┘
                     │ ONNX Export
                     ▼
┌─────────────────────────────────────────────────┐
│           C++ TRADING BOT                       │
│                                                 │
│  ┌──────────────┐  ┌──────────────────────┐     │
│  │ONNXPredictor│─▶│ Signal Generation     │     │
│  └──────────────┘  └──────────────────────┘     │
└─────────────────────────────────────────────────┘
```

## Setup

### Quick Start

```powershell
# Run setup script
.\setup_env.ps1

# Or manually:
uv venv .venv
.venv\Scripts\activate
uv pip install -r requirements.txt
```

### Install Qlib (Optional)

```powershell
.\setup_env.ps1 -InstallQlib
```

## Usage

### 1. Train ML Model

```powershell
python scripts/train_model.py --ticker AAPL --model ridge
```

Options:
- `--ticker`: Stock symbol
- `--model`: ridge, rf (Random Forest), or gb (Gradient Boosting)
- `--target`: return_1d or signal

### 2. Train RL Agent

```powershell
python scripts/train_rl.py --ticker AAPL --algo ppo --timesteps 50000
```

Options:
- `--algo`: ppo, sac, or a2c

### 3. Export Compatible Model

Creates ONNX model compatible with C++ `ONNXPredictor`:

```powershell
python scripts/export_compatible.py
```

## C++ Integration

### Using ONNX Models in C++

1. Train model in Python:
   ```powershell
   python scripts/train_model.py --ticker AAPL --model ridge
   ```

2. Copy model to C++ project:
   ```powershell
   cp models/AAPL_ridge.onnx ../Trading/models/
   ```

3. Load in C++:
   ```cpp
   #include "ONNXPredictor.h"

   ONNXPredictor predictor("models/AAPL_ridge.onnx");

   std::vector<double> features = {...}; // 15 features
   double prediction = predictor.predict(features);
   ```

### Feature Compatibility

The C++ `ONNXPredictor` expects 15 features in this order:

| Index | Feature | Description |
|-------|---------|-------------|
| 0 | RSI | Normalized 0-1 |
| 1 | MACD Hist | Normalized -1 to 1 |
| 2 | Sentiment | Normalized 0-1 |
| 3 | GARCH Vol | Scaled volatility |
| 4 | Cycle Phase | Cos of period |
| 5-9 | Lagged Returns | 1,2,3,5,10 day |
| 10-14 | Cross Features | Feature interactions |

Use `scripts/export_compatible.py` to create matching models.

## Project Structure

```
Trading-Python/
├── requirements.txt       # Dependencies
├── setup_env.ps1          # Setup script
├── src/
│   ├── __init__.py
│   ├── data/             # Data fetching & handling
│   ├── features/         # Technical indicators
│   ├── models/           # ML training & ONNX export
│   └── backtest/         # Strategy backtesting
├── scripts/
│   ├── train_model.py    # ML training
│   ├── train_rl.py      # RL training
│   ├── export_compatible.py  # C++ compatible export
│   └── setup_qlib.py    # Qlib setup
└── models/               # Trained models (generated)
```

## Key Classes

### FeatureEngine

Ports all indicators from your C++ `TechnicalAnalysis.h`:

```python
from src.features import FeatureEngine

engine = FeatureEngine()
features = engine.add_all_indicators(df)
# Adds: SMA, EMA, RSI, Bollinger Bands, MACD, ATR, ADX, etc.
```

### ModelTrainer

Train sklearn models with time-series validation:

```python
from src.models import ModelTrainer, ModelConfig

config = ModelConfig(model_type="ridge", lookback_window=20)
trainer = ModelTrainer(config)

X, y = trainer.prepare_data(features, "return_1d")
trainer.fit(X, y)
```

### Backtester

Comprehensive backtesting:

```python
from src.backtest import Backtester, BacktestConfig

config = BacktestConfig(initial_capital=100000, stop_loss=0.05)
backtester = Backtester(config)
result = backtester.run(data, predictions=predictions)

print(f"Sharpe: {result.sharpe_ratio:.2f}")
print(f"Max DD: {result.max_drawdown:.2%}")
```

## Qlib Integration

Qlib provides institutional-grade data management:

```python
# Setup
from qlib.data import D

# Load data
data = D.features(
    ["AAPL"],
    ["$close", "$volume"],
    start_date="2020-01-01",
    end_date="2024-01-01"
)
```

See `scripts/setup_qlib.py` for detailed setup.

## Dependencies

### Core
- numpy, pandas, scipy, scikit-learn

### ML/ONNX
- onnx, onnxruntime, skl2onnx

### RL
- stable-baselines3, gymnasium

### Optional
- qlib (Microsoft's quantitative platform)
- torch (for deep learning)

## Troubleshooting

### ta-lib installation

If ta-lib fails to install:
1. Download from: https://github.com/ta-lib/ta-lib/releases
2. Extract to `C:\ta-lib`
3. Build: `python setup.py build`
4. Then: `pip install TA-Lib`

### ONNX Runtime issues

For GPU support:
```bash
uv pip install onnxruntime-gpu
```

## Next Steps

1. **Run quick test**:
   ```powershell
   python -c "from src.data import fetch_yahoo_data; print(fetch_yahoo_data('AAPL', '2023-01-01').shape)"
   ```

2. **Train first model**:
   ```powershell
   python scripts/train_model.py --ticker MSFT
   ```

3. **Integrate with C++**:
   - Copy `.onnx` file to C++ project
   - Update `ONNXPredictor` initialization

## References

- [Qlib Documentation](https://qlib.readthedocs.io/)
- [Stable-Baselines3](https://stable-baselines3.readthedocs.io/)
- [ONNX Runtime](https://onnxruntime.ai/)

---

## Additional Features (Added March 2026)

### Sentiment API Service

> **Date Added**: March 2026

Start the API service with scheduler:

```powershell
python scripts/run_service.py
```

This starts:
- FastAPI server on port 8000
- Background scheduler for news scraping and model training

**API Endpoints:**

```powershell
# Get sentiment for a ticker
curl http://localhost:8000/api/sentiment/NVDA

# Get sentiment for multiple tickers
curl "http://localhost:8000/api/batch/sentiment?symbols=NVDA,AMD,TSLA"

# Health check
curl http://localhost:8000/health
```

**Sentiment Response:**
```json
{
  "ticker": "NVDA",
  "timestamp": "2026-03-17T10:30:00",
  "sentiment_score": 0.65,
  "confidence": 0.75,
  "article_count": 12,
  "headline": "NVDA reports strong earnings...",
  "source": "news_db"
}
```

### Background Scheduler

> **Date Added**: March 2026

The scheduler runs these tasks automatically:

| Task | Frequency | Description |
|------|-----------|-------------|
| `scrape_news` | Every hour | Fetch RSS news, classify, store in news.db |
| `update_features` | Every 15 min | Update cached technical indicators |
| `generate_signals` | Every 5 min | Generate BUY/SELL/HOLD signals |
| `retrain_models` | Daily 4 PM | Retrain ONNX models |

**Market Hours:**
- Tasks only run during extended market hours (8 AM - 6 PM ET, weekdays)
- Outside market hours, the scheduler stays idle

### Intelligent Ticker Selection

> **Date Added**: March 2026

When `retrain_models` runs, it selects the top 7 tickers based on actionability:

**Heuristics:**
- Volatility (25%) - High movement stocks
- Volume Spike (15%) - Unusual volume activity
- Price Momentum (15%) - Recent trending
- Gap Analysis (10%) - Price gaps indicate movement
- News Recency (15%) - Recent news coverage
- News Severity (10%) - Strong sentiment
- News Frequency (10%) - Many articles

**Manual Override:**
You can override via Telegram (see Trading_cpp telegram_listener.py):
- `/set NVDA,AMD,TSLA` - Set manual tickers
- `/swap NVDA AMD` - Replace a ticker
- `/auto` - Revert to intelligent selection

### News Database

> **Date Added**: March 2026

Location: `./data/news.db`

Schema:
```sql
CREATE TABLE articles (
    id INTEGER PRIMARY KEY,
    title TEXT,
    url TEXT,
    source TEXT,
    industry TEXT,
    published_date TEXT,
    classification TEXT,  -- promising, bad, neutral
    score REAL,           -- -30 to +30
    company_name TEXT,
    ticker TEXT,
    description TEXT
);
```

### Running Without Full Setup

For testing or minimal operation:

```powershell
# Just start the API (no scheduler)
python scripts/run_service.py --no-scheduler

# Or run individual scripts
python scripts/scrape_news.py
python scripts/train_model.py --ticker NVDA
```
