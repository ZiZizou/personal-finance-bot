# Personal Finance Bot

A comprehensive multi-strategy algorithmic trading system with machine learning, backtesting, and real-time signal generation. The system uses **Python as the brain** for portfolio management, ML training, and scheduling, and **C++ for high-performance ONNX inference** when called by Python.

> **Disclaimer**: This is for educational and research purposes only. Not financial advice. Trading involves risk of loss.

---

## Table of Contents

- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Trading-Python (Brain)](#trading-python-brain)
- [Trading-Cpp (Analysis Engine)](#trading-cpp-analysis-engine)
- [Scheduler](#scheduler)
- [API Reference](#api-reference)
- [Telegram Commands](#telegram-commands)
- [Configuration](#configuration)

---

## Architecture

### Design Principle

**Trading-Python** is the brain. **Trading-Cpp** is the analysis engine that performs ONNX inference when invoked.

```
┌─────────────────────────────────────────────────────────────────────┐
│                      TRADING-PYTHON (Brain)                          │
│                                                                       │
│  • Manages portfolio.json (holdings, cash)                           │
│  • Selects top 7 promising tickers via intelligent scoring           │
│  • Trains ONNX models for selected tickers                          │
│  • Scrapes news and computes sentiment                               │
│  • Runs hourly scheduler for signal generation                       │
│  • Pushes notifications to Telegram via pending_notifications.json     │
│  • Invokes C++ for ONNX inference with specific tickers             │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Selected tickers for inference
                              │ Signal results written to files
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    TRADING-CPP (Analysis Engine)                     │
│                                                                       │
│  • ONLY runs when called by Python with specific tickers            │
│  • Performs ONNX inference on provided tickers                      │
│  • Technical analysis (RSI, MACD, Bollinger, etc.)                 │
│  • NO portfolio management (deprecated in Broker.h/cpp)             │
│  • NO independent ticker selection (deprecated)                     │
│  • NO tickers.csv dependency (deprecated)                          │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Notifications written to pending_notifications.json
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     TELEGRAM LISTENER (Relay)                        │
│                                                                       │
│  • Polls pending_notifications.json for signal changes             │
│  • Sends Telegram notifications when signals change                  │
│  • Commands routed to Python API (NOT independent management)       │
│  • NO watchlist management (deprecated)                             │
└─────────────────────────────────────────────────────────────────────┘
```

### Data Flow

```
1. Startup: Python selects top 7 tickers → saves to selected_tickers.txt
2. Hourly (:30): Python scrapes news, computes sentiment
3. Hourly (:00): Python generates signals ONLY for:
   - Portfolio holdings (from portfolio.json)
   - Selected tickers (from selected_tickers.txt)
4. Signal Change? → Python writes to pending_notifications.json
5. Telegram polls → reads pending_notifications.json → sends alerts
```

### Technology Stack

| Component | Language | Key Libraries | Role |
|-----------|----------|---------------|------|
| Portfolio/Scheduler | Python 3.10+ | FastAPI, APScheduler | Brain |
| ML Training | Python 3.10+ | scikit-learn, onnxruntime, pandas | Brain |
| News Scraping | Python 3.10+ | feedparser, requests | Brain |
| ONNX Inference | C++17 | Eigen, ONNX Runtime | Analysis Engine |
| Technical Analysis | C++17 | Eigen | Analysis Engine |
| Notifications | Python | python-telegram-bot | Relay |

---

## Quick Start

### 1. Start Telegram Listener (Recommended)

```bash
cd Trading_cpp
python telegram_listener.py

# Telegram listener will:
# - Start Python API automatically
# - Trigger initial model training
# - Poll for notifications and send Telegram alerts
```

### 2. Monitor via Telegram

```
/portfolio     # View holdings
/selected      # View promising tickers
/signals       # View current signals
/discover      # Sector heatmap
```

### 3. Development

```bash
# Start Python API only
cd Trading_Python
../venv/Scripts/python.exe scripts/run_service.py

# API docs at http://localhost:8000/docs
```

---

## Project Structure

```
Trading_super/
├── Trading_Python/                 # Brain - Python ML Service
│   ├── src/
│   │   ├── api/
│   │   │   ├── main.py            # FastAPI application
│   │   │   └── routes/
│   │   │       ├── signals.py    # Signal endpoints
│   │   │       ├── sentiment.py  # Sentiment endpoints
│   │   │       ├── portfolio.py  # Portfolio management
│   │   │       └── models.py     # Model management
│   │   ├── service/
│   │   │   ├── scheduler.py      # Hourly signal generation
│   │   │   ├── feature_cache.py  # Feature caching
│   │   │   └── ticker_manager.py  # ONNX model selection
│   │   ├── data/                  # Data fetching
│   │   ├── features/             # Technical indicators
│   │   ├── models/               # ML training + ONNX export
│   │   └── news/                 # News scraping + classification
│   ├── data/
│   │   ├── portfolio.json        # Holdings (source of truth)
│   │   └── selected_tickers.txt   # Top 7 promising tickers
│   └── scripts/
│       ├── run_service.py         # Start API server
│       └── train_model.py         # Train single ticker
│
├── Trading_cpp/                    # Analysis Engine - C++ Core
│   ├── main.cpp                   # Entry point (deprecated modes)
│   ├── TradingStrategy.cpp         # Signal generation
│   ├── TechnicalAnalysis.cpp       # 20+ technical indicators
│   ├── ONNXPredictor.cpp          # ONNX Runtime inference
│   ├── LiveSignals.cpp            # Live mode (deprecated)
│   ├── Broker.h/cpp               # DEPRECATED - portfolio mgmt
│   ├── telegram_listener.py       # Telegram relay (deprecated watchlist)
│   └── models/                    # ONNX models (deployed by Python)
│
└── data/
    ├── pending_notifications.json # Signal change notifications
    └── news.db                     # SQLite news database
```

---

## Trading-Python (Brain)

### Portfolio Management

Portfolio is stored in `portfolio.json` and managed via REST API:

```bash
GET  /api/portfolio              # Get holdings
POST /api/portfolio/position      # Add/update position
DELETE /api/portfolio/position/{ticker}  # Remove position
POST /api/portfolio/execute/{trade_id}   # Execute trade
```

### Intelligent Ticker Selection

At startup, Python selects top 7 promising tickers based on:

| Factor | Weight | Description |
|--------|--------|-------------|
| Volatility | 25% | Higher volatility = more opportunity |
| Volume Spike | 15% | Unusual volume activity |
| Price Momentum | 15% | Recent returns |
| Gap Analysis | 10% | Price gaps (often fill) |
| News Recency | 15% | Recent news articles |
| News Severity | 10% | Sentiment strength |
| News Frequency | 10% | Number of articles |

Selected tickers are saved to `selected_tickers.txt` for C++ to use.

### Signal Generation

Signals generated ONLY for:
1. **Portfolio holdings** - Rebalance notifications
2. **Selected tickers** - New opportunity notifications

Signal includes:
- Technical indicators (from cached features)
- ML prediction (from ONNX inference)
- Sentiment score (from news analysis)
- Confidence adjusted by sentiment alignment

---

## Trading-Cpp (Analysis Engine)

### Role

Trading-C++ is **ONLY** an analysis engine. It:
- Performs ONNX inference when called by Python
- Runs technical analysis on provided tickers
- Writes results to files for Python to read

### Deprecated Features

The following are **DEPRECATED** and will be removed:

| Feature | File | Reason |
|---------|------|--------|
| `live_signals` mode | main.cpp | Reads tickers.csv directly |
| `scheduled` mode | main.cpp | Should be Python scheduler |
| Portfolio management | Broker.h/cpp | Now handled by Python API |
| Watchlist management | telegram_listener.py | Now uses portfolio.json |
| tickers.csv loading | LiveSignals.cpp | Now uses selected_tickers.txt |

### C++ Modes (Deprecated)

| Mode | Status | Notes |
|------|--------|-------|
| `backtest` (default) | Deprecated | Use Python backtesting |
| `live_signals` | **Deprecated** | Use Python scheduler |
| `scheduled` | **Deprecated** | Use Python scheduler |

---

## Scheduler

Scheduler runs inside Python API during market hours (8 AM - 6 PM ET, weekdays):

| Task | Time | Description |
|------|------|-------------|
| `scrape_news` | :30 hourly | Fetch RSS feeds, classify, score |
| `update_features` | :00, :15, :30, :45 | Calculate technical indicators |
| `generate_signals` | :00 hourly | Generate signals for portfolio + selected |
| `retrain_models` | 4:00 PM daily | Retrain ONNX models |

**Key**: Signals are only generated for portfolio holdings + selected tickers (NOT all tickers).

---

## API Reference

### Portfolio Endpoints

```
GET    /api/portfolio                    # Get portfolio
GET    /api/portfolio/position/{ticker}   # Get specific position
POST   /api/portfolio/position            # Add/update position
DELETE /api/portfolio/position/{ticker}   # Remove position
GET    /api/portfolio/summary             # Portfolio summary
POST   /api/portfolio/cash               # Update cash balance
```

### Signal Endpoints

```
GET    /api/signals/{ticker}            # Get signal for ticker
GET    /api/batch/signals?symbols=AAPL,NVDA  # Batch signals
```

### Sentiment Endpoints

```
GET    /api/sentiment/{ticker}           # Get sentiment
GET    /api/batch/sentiment?symbols=...  # Batch sentiment
GET    /api/discover                      # Sector heatmap
```

### Model Endpoints

```
POST   /api/models/select-tickers         # Select tickers for training
POST   /api/models/train                  # Trigger training
```

---

## Telegram Commands

### Portfolio Commands

| Command | Description |
|---------|-------------|
| `/portfolio` | Show current holdings |
| `/add_pos T SHARES PRICE` | Add/update position |
| `/remove_pos TICKER` | Remove position |

### Signal Commands

| Command | Description |
|---------|-------------|
| `/signals` | Show signals for portfolio + selected |
| `/analyze SYMBOL` | Get signal for specific ticker |
| `/discover` | Sector heatmap with laggard plays |

### System Commands

| Command | Description |
|---------|-------------|
| `/scrape` | Trigger news scrape |
| `/selected` | Show promising tickers |
| `/help` | Show all commands |

### Deprecated Commands

| Command | Status | Use Instead |
|---------|--------|------------|
| `/add SYMBOL` | **Deprecated** | `/add_pos` |
| `/remove SYMBOL` | **Deprecated** | `/remove_pos` |
| `/list` | **Deprecated** | `/portfolio` |

---

## Configuration

### Environment Variables

```bash
# Trading-Python
PYTHON_API_URL=http://localhost:8000

# Trading-Cpp (deprecated)
STOCK_TELEGRAM_BOT_TOKEN=your_token
STOCK_TELEGRAM_CHAT_ID=your_chat_id
USE_ONNX_MODEL=true
ONNX_MODEL_PATH=models/stock_predictor.onnx
```

### Key Files

| File | Location | Purpose |
|------|----------|---------|
| portfolio.json | Trading_Python/data/ | Holdings source of truth |
| selected_tickers.txt | Trading_Python/models/ | Top 7 promising tickers |
| pending_notifications.json | Trading_Python/data/ | Signal change notifications |
| news.db | Trading_Python/data/ | SQLite news database |
| stock_predictor.onnx | Trading_cpp/models/ | Deployed ONNX model |

### Market Hours

```
Extended Hours:  Mon-Fri, 8:00 AM - 6:00 PM ET
Regular Session: Mon-Fri, 9:30 AM - 4:00 PM ET
Weekends:       Closed

Scheduler tasks only run during extended hours.
```

---

## License

For educational and research purposes only.
