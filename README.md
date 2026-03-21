# Personal Finance Bot

A comprehensive multi-strategy algorithmic trading system with machine learning, backtesting, and real-time signal generation. The system combines C++ for high-performance trading logic with Python for ML model training and sentiment analysis.

> **Disclaimer**: This is for educational and research purposes only. Not financial advice. Trading involves risk of loss.

---

## Table of Contents

- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Trading_cpp (C++ Core)](#trading_cpp-c-core)
- [Trading_Python (ML Service)](#trading_python-ml-service)
- [Trading Strategies](#trading-strategies)
- [Backtesting](#backtesting)
- [Machine Learning](#machine-learning)
- [API Reference](#api-reference)
- [Telegram Commands](#telegram-commands)
- [Configuration](#configuration)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         TELEGRAM USER                                │
│                    /run, /signals, /help                            │
└────────────────────────────┬────────────────────────────────────────┘
                             │
                             v
┌─────────────────────────────────────────────────────────────────────┐
│                   telegram_listener.py (Python)                     │
│         Command parsing, /run pipeline, Python API startup           │
└────────────────────────────┬────────────────────────────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          v                  v                  v
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────────────┐
│  Python API      │ │  C++ Trading     │ │  Scheduler Service       │
│  (FastAPI)       │ │  Bot (C++)       │ │  (APScheduler)            │
│                  │ │                  │ │                          │
│  /api/signals    │ │  Multi-strategy  │ │  - News scraping (hourly)│
│  /api/sentiment  │ │  Backtesting     │ │  - Feature updates (15m)  │
│  /api/health     │ │  ONNX inference  │ │  - Signal gen (5m)       │
│                  │ │                  │ │  - Model retrain (daily) │
└────────┬─────────┘ └────────┬─────────┘ └────────────┬─────────────┘
         │                    │                        │
         v                    v                        v
┌──────────────────────────────────────────────────────────────────────┐
│                         Data Layer                                   │
│  news.db (SQLite) │ feature cache │ ONNX models │ tickers.csv      │
└──────────────────────────────────────────────────────────────────────┘
```

### Technology Stack

| Component | Language | Key Libraries |
|-----------|----------|---------------|
| Trading Engine | C++17 | Eigen, ONNX Runtime, libcurl |
| ML Training | Python 3.10+ | scikit-learn, onnxruntime, pandas |
| API Service | Python 3.10+ | FastAPI, uvicorn |
| Scheduler | Python 3.10+ | APScheduler |
| News Scraping | Python 3.10+ | feedparser, requests |
| Notifications | Python 3.10+ | python-telegram-bot |

---

## Quick Start

### Option 1: Telegram Interface (Recommended)

```bash
# Start telegram listener (auto-starts Python API and trains models)
cd Trading_cpp
python telegram_listener.py

# Then use Telegram commands:
/run          # Run full pipeline
/signals      # Show current signals
/help         # Show all commands
```

### Option 2: Direct C++ Execution

```bash
cd Trading_cpp

# Build (MSVC)
build_msvc.bat

# Run in different modes
./TradingBot.exe                  # Backtest mode (default)
./TradingBot.exe live_signals     # Live signal generation
./TradingBot.exe scheduled        # Single run for task scheduler
```

### Option 3: Python API Only

```bash
cd Trading_Python
../venv/Scripts/python.exe scripts/run_service.py

# API available at http://localhost:8000
# Docs at http://localhost:8000/docs
```

---

## Project Structure

```
Trading_super/
├── Trading_cpp/                    # C++ trading engine
│   ├── main.cpp                    # Entry point, execution modes
│   ├── Config.h                    # Environment configuration
│   ├── MarketData.cpp              # Yahoo Finance data fetching
│   ├── TradingStrategy.cpp         # Signal generation engine
│   ├── TechnicalAnalysis.cpp       # 20+ technical indicators
│   ├── Backtester.cpp              # Backtesting engine
│   ├── MLPredictor.cpp             # ML prediction wrapper
│   ├── ONNXPredictor.cpp           # ONNX Runtime inference
│   ├── Strategies/                  # Trading strategies
│   │   ├── IStrategy.h             # Strategy interface
│   │   ├── MeanReversionStrategy.h
│   │   ├── TrendFollowingStrategy.h
│   │   ├── MLStrategy.h
│   │   └── EnsembleStrategy.h
│   ├── VolatilityModels.h          # GARCH, EGARCH, realized vol
│   ├── PortfolioOptimizer.h        # Mean-variance, risk-parity
│   ├── BlackScholes.cpp            # Options pricing
│   ├── LiveSignals.cpp             # Live signal generation
│   ├── TelegramNotifier.cpp        # Telegram alerts
│   ├── telegram_listener.py        # Telegram bot interface
│   ├── telegram_listener.py        # Telegram bot interface
│   ├── TradingBot.vcxproj         # MSVC project
│   └── models/                     # ONNX models (deployed)
│
├── Trading_Python/                 # Python ML service
│   ├── src/
│   │   ├── api/                    # FastAPI endpoints
│   │   │   ├── main.py             # FastAPI app
│   │   │   └── routes/
│   │   │       ├── signals.py     # Signal endpoints
│   │   │       ├── sentiment.py    # Sentiment endpoints
│   │   │       └── models.py       # Model management
│   │   ├── service/
│   │   │   ├── scheduler.py        # Background task scheduler
│   │   │   ├── feature_cache.py   # Feature caching
│   │   │   └── ticker_manager.py  # ONNX model management
│   │   ├── data/                   # Data fetching
│   │   ├── features/               # Technical indicators
│   │   ├── models/                 # ML training + ONNX export
│   │   ├── backtest/               # Python backtesting
│   │   └── news/                   # News scraping + classification
│   ├── scripts/
│   │   ├── run_service.py          # Start API
│   │   ├── train_model.py          # Train single ticker
│   │   └── scrape_news.py          # News scraper CLI
│   └── models/                     # Trained ONNX models
│
├── docs/                           # Documentation
│   ├── CURRENT_STATE.md            # Feature status
│   ├── ARCHITECTURE_GAPS.md        # Known gaps
│   └── INCOMPLETE_FEATURES.md      # Work in progress
│
├── requirements.txt                # Python dependencies
└── .gitignore                     # Git ignore rules
```

---

## Trading_cpp (C++ Core)

### Execution Modes

| Mode | Description |
|------|-------------|
| `backtest` (default) | Historical backtesting on tickers.csv |
| `live_signals` | Real-time signal generation (blocks) |
| `scheduled` | Single run for Windows Task Scheduler |

### Core Components

#### Market Data (`MarketData.cpp`)
- Fetches OHLCV data from Yahoo Finance (2-year history)
- Supports stocks, ETFs, crypto, futures
- Thread-safe with rate limiting (60 req/min per domain)
- 300-second cache TTL

#### Signal Generation (`TradingStrategy.cpp`)

Multi-factor signal generation:

1. **Market Regime Detection** - Bull, Bear, Sideways, HighVol
2. **Technical Analysis** - RSI, MACD, Bollinger Bands, ADX, ATR
3. **ML Prediction** - ONNX or native MLPredictor
4. **Sentiment Blending** - Adaptive weighting based on regime
5. **GARCH Volatility** - Risk-adjusted signals
6. **Support/Resistance** - Price levels identification

```
Signal Score = clamp(technicalScore * technicalWeight + sentimentScore * sentimentWeight)

Buy:  signal > 0.25
Sell: signal < -0.25
Hold: otherwise
```

#### Technical Indicators (`TechnicalAnalysis.cpp`)

| Category | Indicators |
|----------|------------|
| **Trend** | SMA, EMA (multiple periods), MACD, ADX, ROC |
| **Momentum** | RSI (standard + adaptive), Stochastic, Williams %R, CCI |
| **Volatility** | Bollinger Bands, ATR, GARCH(1,1), Realized Vol |
| **Volume** | OBV, MFI, VWAP |
| **Custom** | Cycle detection (DFT), Price forecasting (linear/poly) |

#### Risk Management (`RiskManagement.cpp`, `BacktestConfig.h`)

- **Stop Loss**: Configurable % (default 2%)
- **Take Profit**: Configurable % (default 4%)
- **Trailing Stop**: Dynamic stop that follows price
- **Position Sizing**: Fixed Fraction, Kelly Criterion, ATR-based
- **Max Drawdown Breaker**: Auto-exit if drawdown exceeds threshold

---

## Trading_Python (ML Service)

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | Health check |
| `/api/ping` | GET | Simple ping |
| `/api/signals/{ticker}` | GET | Get signal for ticker |
| `/api/batch/signals` | GET | Batch signals |
| `/api/signals/cluster/{name}` | GET | Cluster-based signals |
| `/api/sentiment/{ticker}` | GET | Get sentiment score |
| `/api/batch/sentiment` | GET | Batch sentiment |
| `/api/models/train` | POST | Trigger model training |
| `/api/models/select-tickers` | POST | Select tickers for training |

### Scheduler Tasks

| Task | Frequency | Description |
|------|-----------|-------------|
| `scrape_news` | Every 1 hour | Fetch RSS feeds, classify articles |
| `update_features` | Every 15 min | Calculate technical indicators |
| `generate_signals` | Every 5 min | Generate trading signals |
| `retrain_models` | Daily 4PM | Retrain ONNX models |

**Scheduler runs only during market hours (8 AM - 6 PM ET, weekdays)**

### News Pipeline

1. Fetch from 30+ RSS sources (Yahoo Finance, MarketWatch, Reuters, CNBC, etc.)
2. Extract company names using NLP
3. Resolve to stock tickers
4. Classify: `promising` | `bad` | `neutral`
5. Score: -30 to +30 based on keywords
6. Store in SQLite (`news.db`)

---

## Trading Strategies

### Strategy Interface (`IStrategy.h`)

All strategies implement:

```cpp
class IStrategy {
    virtual StrategySignal generateSignal(
        const vector<Candle>& history,
        size_t idx
    ) = 0;

    virtual string getName() const = 0;
    virtual int getWarmupPeriod() const = 0;
    virtual unique_ptr<IStrategy> clone() const = 0;
};
```

### Mean Reversion Strategy

**Logic**: Buy oversold, sell overbought

```
Buy Signal:
  - RSI < 30 OR price < lower Bollinger Band
  - Strength increases if both conditions met
  - Stop loss: current - 2*ATR
  - Take profit: middle Bollinger Band

Sell Signal:
  - RSI > 70 OR price > upper Bollinger Band
  - Strength increases if both conditions met
  - Stop loss: current + 2*ATR
```

### Trend Following Strategy

**Logic**: Follow momentum, cut losses quickly

```
Buy Signal:
  - Fast MA (10) crosses above Slow MA (30)
  - ADX >= 25 (strong trend required)
  - Strength based on ADX value
  - Stop loss: current - 2.5*ATR

Sell Signal:
  - Fast MA crosses below Slow MA
  - ADX >= 25
```

### Ensemble Strategy

Combines multiple strategies with configurable weights:

```cpp
EnsembleStrategy strategy({
    make_unique<MeanReversionStrategy>(),
    make_unique<TrendFollowingStrategy>()
}, 0.5);  // 50% weight each
```

---

## Backtesting

### BacktestConfig

```cpp
BacktestConfig config;
config.initialCapital = 10000.0f;       // Starting capital
config.costs.commissionPercent = 0.001f; // 0.1%
config.costs.slippagePercent = 0.0005f;  // 0.05%
config.costs.minCommission = 1.0f;        // $1 minimum
config.risk.stopLossPercent = 0.02f;      // 2%
config.risk.takeProfitPercent = 0.04f;    // 4%
config.risk.enableTrailingStop = true;
config.risk.trailingStopPercent = 0.015f;
config.sizing.method = PositionSizing::FixedFraction;
config.sizing.fixedFraction = 0.10f;     // 10% per trade
```

### Backtest Metrics (40+)

| Category | Metrics |
|----------|---------|
| **Core** | Total Return, Sharpe Ratio, Max Drawdown |
| **Performance** | CAGR, Sortino Ratio, Calmar Ratio |
| **Trade Stats** | Win Rate, Avg Win, Avg Loss, Profit Factor, Expectancy |
| **Risk** | Volatility, Downside Deviation, VaR(95%), CVaR(95%) |
| **Costs** | Total Commissions, Total Slippage, Cost Impact |
| **Sequences** | Max Consecutive Wins/Losses |

### Walk-Forward Optimization

Validates strategy robustness on out-of-sample data:

```
1. Split data into rolling windows (70% train / 30% test)
2. Grid search optimal parameters on training data
3. Test optimized parameters on unseen test data
4. Calculate Walk-Forward Efficiency = testSharpe / trainSharpe
5. Robust if efficiency > 0.5 AND robustnessScore > 0.6
```

---

## Machine Learning

### Feature Engineering (15 dimensions)

```cpp
// C++ MLPredictor features
[0]  RSI normalized (0-1)
[1]  MACD histogram (tanh-squashed)
[2]  Sentiment score (-1 to 1)
[3]  GARCH volatility (tanh-squashed)
[4]  Cycle phase (cosine)
[5-9]  Lagged returns (t-1, t-2, t-3, t-5, t-10)
[10-14]  Cross-products (RSI*Sentiment, MACD*Vol, etc.)
```

### Python Model Training

```bash
# Train single ticker
python scripts/train_model.py --ticker NVDA --start 2020-01-01

# Train with specific model type
python scripts/train_model.py --ticker AAPL --model ridge

# Automatic model selection (based on data characteristics)
# - Small data (<1000): Ridge (stability)
# - Large data (>50000): Gradient Boosting
# - High frequency: Random Forest
```

### ONNX Deployment

```
Training (Python):
  sklearn model → ONNXExporter → .onnx file

Deployment (C++):
  ONNX Runtime → ONNXPredictor → float prediction
```

---

## API Reference

### Signal Response

```json
GET /api/signals/NVDA

{
    "ticker": "NVDA",
    "signal": "buy",
    "confidence": 0.72,
    "price": 875.50,
    "action": "buy",
    "entry": 875.50,
    "target": 910.00,
    "stop_loss": 857.00,
    "reason": "RSI oversold, MACD bullish cross",
    "ml_forecast": 0.034,
    "sentiment": 0.65,
    "regime": "Bull",
    "timestamp": "2026-03-21T10:30:00"
}
```

### Sentiment Response

```json
GET /api/sentiment/NVDA?days=7

{
    "ticker": "NVDA",
    "sentiment_score": 0.65,
    "confidence": 0.75,
    "article_count": 12,
    "headline": "NVDA reports strong earnings...",
    "source": "news_db",
    "timestamp": "2026-03-21T10:30:00"
}
```

---

## Telegram Commands

### Core Commands

| Command | Description |
|---------|-------------|
| `/help` | Show all commands |
| `/analyze SYMBOL` | Get last analysis for symbol |
| `/signals` | Show all current signals |
| `/fundamentals SYMBOL` | Get Yahoo Finance fundamentals |
| `/sentiment` | Get market sentiment |
| `/news` | Get latest stock news |

### Trading Commands

| Command | Description |
|---------|-------------|
| `/run` | Run full pipeline: start API → scrape → train → run bot |
| `/scrape` | Scrape latest news |
| `/train` | Train ONNX models |

### Ticker Selection

| Command | Description |
|---------|-------------|
| `/selected` | Show current ticker selection |
| `/swap OLD NEW` | Replace a ticker |
| `/set AAPL,GOOGL` | Set manual tickers (max 7) |
| `/addticker META` | Add ticker |
| `/removeticker TSLA` | Remove ticker |
| `/auto` | Revert to intelligent selection |

---

## Configuration

### Environment Variables (.env)

```bash
# Trading_cpp/.env

# Execution
MODE=backtest                    # backtest, live_signals, scheduled

# Data
BAR_SIZE_MINUTES=60              # Candle interval
WARMUP_DAYS=60                   # Warmup period for indicators

# Output
LIVE_OUTPUT_CSV=live_signals.csv # Live signals output file
LIVE_INCLUDE_NEWS=true           # Include news in live mode
LIVE_INCLUDE_SENTIMENT=true      # Include sentiment in live mode

# Parallelism
NUM_WORKERS=4                   # Concurrent ticker processing
SETTLE_DELAY_SECONDS=3           # Delay between batches

# Python Integration
USE_PYTHON_SERVICE=true
PYTHON_SERVICE_URL=http://localhost:8000

# ONNX Model
USE_ONNX_MODEL=true
ONNX_MODEL_PATH=models/stock_predictor.onnx

# API Keys (optional)
NEWSAPI_KEY=your_key_here
ALPHAVANTAGE_KEY=your_key_here
STOCK_TELEGRAM_BOT_TOKEN=your_token
STOCK_TELEGRAM_CHAT_ID=your_chat_id
```

### Market Hours

```
Regular Session:  Mon-Fri, 9:30 AM - 4:00 PM ET
Extended Hours:    Mon-Fri, 8:00 AM - 6:00 PM ET
Weekends:          Closed

Scheduled tasks only run during extended hours.
```

---

## Key Constants

| Parameter | Default | Description |
|-----------|---------|-------------|
| Initial Capital | $10,000 | Backtest starting capital |
| Commission | 0.1% (min $1) | Per-trade commission |
| Slippage | 0.05% | Price impact |
| RSI Period | 14 (adaptive 7-28) | Technical indicator |
| MACD | (12, 26, 9) | Fast, slow, signal |
| Bollinger | 20-period, 2.0 std | Price bands |
| ADX Threshold | 25 | Trend strength |
| GARCH | alpha=0.05, beta=0.90 | Volatility model |
| Kelly Fraction | 0.50 | Half-Kelly sizing |
| Cache TTL | 300s | HTTP cache duration |
| Rate Limit | 60 req/min | Per domain |

---

## Known Limitations

See [docs/INCOMPLETE_FEATURES.md](docs/INCOMPLETE_FEATURES.md) for work in progress.

---

## License

For educational and research purposes only.
