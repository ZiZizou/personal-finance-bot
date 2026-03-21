# Trading Bot v5.0 -- Codebase Capability Summary

> A C++ multi-strategy trading bot with machine learning, backtesting, portfolio optimization, and real-time market data integration. This document provides enough pseudocode and code snippets to fully reconstruct the system's behavior without access to the source.

---

## 1. Architecture Overview

```
                    +-----------------+
                    |   main.cpp      |   Reads tickers.csv, spawns thread pool
                    +--------+--------+
                             |
            +----------------+----------------+
            |                |                |
    +-------v-------+ +-----v------+ +-------v--------+
    | MarketData    | | NewsManager| | SentimentAnalyzer|
    | (Yahoo API)   | | (RSS/API)  | | (llama.cpp GGUF) |
    +-------+-------+ +-----+------+ +-------+--------+
            |                |                |
            +--------+-------+--------+-------+
                     |                |
             +-------v-------+ +-----v--------+
             | TradingStrategy| | MLPredictor  |
             | (Signal Gen)   | | (Ensemble)   |
             +-------+-------+ +-----+--------+
                     |                |
         +-----------+---------+------+
         |           |         |
   +-----v---+ +----v----+ +--v-----------+
   |Backtester| |WalkFwd  | |Portfolio     |
   |(Instance)| |Optimizer | |Optimizer     |
   +---------++ +---------+ +--------------+
              |
       +------v--------+
       |ReportGenerator |  CSV + HTML (Chart.js)
       +----------------+
```

**Language**: C++17
**Threading**: `std::async` with batch throttling (4 concurrent, 500ms between batches)
**JSON**: nlohmann/json
**HTTP**: libcurl (optional, compile with `-DENABLE_CURL`)
**ML/Sentiment**: llama.cpp for local GGUF model inference

---

## 2. Core Data Structures

### 2.1 Candle (OHLCV Bar)

```cpp
struct Candle {
    std::string date;
    float open, high, low, close;
    long long volume;
};
```

All market data, indicators, and backtests operate on `vector<Candle>`.

### 2.2 Signal (Legacy Action Output)

```cpp
struct Signal {
    std::string action;              // "buy" | "sell" | "hold"
    float entry, exit;               // entry price, exit/target price
    std::vector<float> targets;      // resistance/support targets
    float confidence;                // 0-100%
    std::string reason;              // human-readable explanation
    std::optional<OptionSignal> option;  // optional options recommendation
    float prospectiveBuy;            // (hold only) price to buy at
    float prospectiveSell;           // (hold only) price to sell at
    float mlForecast;                // ML predicted % change
};
```

### 2.3 StrategySignal (Strategy Interface Output)

```cpp
enum class SignalType { Buy, Sell, Hold };

struct StrategySignal {
    SignalType type = SignalType::Hold;
    float strength;          // -1.0 (strong sell) to +1.0 (strong buy)
    float stopLossPrice;     // suggested stop-loss (0 = unset)
    float takeProfitPrice;   // suggested take-profit (0 = unset)
    float confidence;        // 0..1
    std::string reason;

    // Factory methods
    static StrategySignal buy(float strength, string reason);
    static StrategySignal sell(float strength, string reason);
    static StrategySignal hold(string reason);
};
```

### 2.4 BacktestResult (40+ metrics)

```cpp
struct BacktestResult {
    // Core
    float totalReturn, sharpeRatio, maxDrawdown;
    int trades, wins;

    // Performance
    float annualizedReturn;     // CAGR
    float sortinoRatio;         // downside-only risk-adjusted
    float calmarRatio;          // return / maxDrawdown
    float winRate, avgWin, avgLoss, profitFactor, expectancy;

    // Costs
    float totalCommissions, totalSlippage, totalCosts, costImpact;

    // Risk
    float volatility, downsideDeviation, maxDrawdownDuration;
    float valueAtRisk95, cvar95;

    // Trade Stats
    int longTrades, shortTrades, winningLongTrades, winningShortTrades;
    float largestWin, largestLoss;
    int maxConsecutiveWins, maxConsecutiveLosses;

    // Curves
    vector<TradeRecord> tradeLog;
    vector<float> equityCurve, dailyReturns, drawdownCurve;
};
```

---

## 3. Market Data Pipeline

### 3.1 Price Data (`MarketData.cpp`)

```
PSEUDOCODE fetchCandles(symbol, type):
    url = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}?range=2y&interval=1d"
    response = HTTP_GET(url)
    json = parse(response)

    timestamps  = json["chart"]["result"][0]["timestamp"]
    opens       = json["chart"]["result"][0]["indicators"]["quote"][0]["open"]
    highs       = ...["high"]
    lows        = ...["low"]
    closes      = ...["close"]
    volumes     = ...["volume"]

    FOR i in 0..len(timestamps):
        candles.push(Candle{
            date  = format_date(timestamps[i]),
            open  = opens[i], high = highs[i],
            low   = lows[i],  close = closes[i],
            volume = volumes[i]
        })
    RETURN candles   // ~500 daily bars (2 years)
```

### 3.2 Fundamentals (`MarketData.cpp`)

```
PSEUDOCODE fetchFundamentals(symbol, type):
    url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols={symbol}"
    response = HTTP_GET(url)
    RETURN Fundamentals {
        pe_ratio      = json["trailingPE"],
        market_cap    = json["marketCap"],
        fifty_day_avg = json["fiftyDayAverage"]
    }
```

### 3.3 News & Sentiment

```
PSEUDOCODE NewsManager::fetchNews(symbol):
    headlines = []
    // Source 1: Yahoo RSS
    rss = HTTP_GET("https://feeds.finance.yahoo.com/rss/2.0/headline?s={symbol}")
    headlines += parse_rss_titles(rss)

    // Source 2: NewsAPI (if NEWS_KEY env var set)
    IF NEWS_KEY:
        api = HTTP_GET("https://newsapi.org/v2/everything?q={symbol}&apiKey={NEWS_KEY}")
        headlines += parse_api_titles(api)
    RETURN headlines

PSEUDOCODE SentimentAnalyzer::analyze(headlines):
    IF gguf_model_loaded:
        scores = llama_cpp_batch_inference(headlines)  // FinBERT-compatible
        RETURN average(scores)   // -1.0 to +1.0
    ELSE:
        // Keyword fallback
        FOR headline in headlines:
            IF contains(headline, ["surge", "rally", "beat"]): score += 0.3
            IF contains(headline, ["crash", "loss", "miss"]):  score -= 0.3
        RETURN clamp(score / len(headlines), -1, 1)
```

---

## 4. Technical Analysis Indicators

All implemented in `TechnicalAnalysis.cpp`. Each function takes a price vector or candle vector and returns scalar(s) or struct(s).

### 4.1 RSI (Wilder's Smoothing)

```
PSEUDOCODE computeRSI(prices, period=14):
    gains, losses = [], []
    FOR i in 1..len(prices):
        delta = prices[i] - prices[i-1]
        IF delta > 0: gains.push(delta), losses.push(0)
        ELSE:         gains.push(0), losses.push(-delta)

    avg_gain = SMA(gains[0..period])
    avg_loss = SMA(losses[0..period])

    FOR i in period..len(gains):
        avg_gain = (avg_gain * (period-1) + gains[i]) / period   // Wilder smoothing
        avg_loss = (avg_loss * (period-1) + losses[i]) / period

    rs = avg_gain / avg_loss
    RETURN 100 - (100 / (1 + rs))
```

### 4.2 Adaptive RSI

```
PSEUDOCODE computeAdaptiveRSI(prices, basePeriod=14):
    targetCV = 0.01   // target coefficient of variation
    window = prices[-50:]
    currentCV = stddev(window) / mean(window)

    adaptivePeriod = basePeriod * (targetCV / currentCV)
    adaptivePeriod = clamp(adaptivePeriod, 7, 28)

    RETURN computeRSI(prices, round(adaptivePeriod))
```

### 4.3 MACD

```
PSEUDOCODE computeMACD(prices, fast=12, slow=26, signal=9):
    ema_fast   = EMA(prices, fast)
    ema_slow   = EMA(prices, slow)
    macd_line  = ema_fast - ema_slow
    signal_line = EMA(macd_line, signal)
    RETURN (macd_line, signal_line)
```

### 4.4 ATR (Average True Range)

```
PSEUDOCODE computeATR(candles, period=14):
    FOR each candle (from i=1):
        tr = max(high-low, abs(high-prev_close), abs(low-prev_close))
        true_ranges.push(tr)
    RETURN SMA(true_ranges[-period:])
```

### 4.5 Bollinger Bands

```
PSEUDOCODE computeBollingerBands(prices, period=20, multiplier=2.0):
    middle = SMA(prices[-period:])
    std    = stddev(prices[-period:])
    RETURN BollingerBands {
        upper  = middle + multiplier * std,
        middle = middle,
        lower  = middle - multiplier * std
    }
```

### 4.6 ADX (Average Directional Index)

```
PSEUDOCODE computeADX(candles, period=14):
    FOR each bar:
        +DM = max(0, high[i] - high[i-1])
        -DM = max(0, low[i-1] - low[i])
        IF +DM > -DM: -DM = 0  ELSE: +DM = 0

    smooth_+DM = Wilder_Smooth(+DM, period)
    smooth_-DM = Wilder_Smooth(-DM, period)
    smooth_TR  = Wilder_Smooth(TR,  period)

    +DI = 100 * smooth_+DM / smooth_TR
    -DI = 100 * smooth_-DM / smooth_TR
    DX  = 100 * abs(+DI - -DI) / (+DI + -DI)
    ADX = Wilder_Smooth(DX, period)

    RETURN ADXResult { adx, plusDI, minusDI }
```

### 4.7 Cycle Detection (DFT)

```
PSEUDOCODE detectCycle(prices):
    detrended = remove_linear_trend(prices[-120:])
    FOR period in 5..60:
        freq = 2*PI / period
        magnitude = |DFT_at_frequency(detrended, freq)|
        IF magnitude > best: best_period = period
    RETURN best_period
```

### 4.8 Price Forecasting

```
PSEUDOCODE forecastPrice(prices, horizon=30):
    // Linear regression on last 60 points
    x = [0, 1, ..., 59]
    y = prices[-60:]
    slope, intercept = linear_regression(x, y)
    RETURN intercept + slope * (59 + horizon)

PSEUDOCODE forecastPricePoly(prices, horizon=30, degree=2):
    // Polynomial regression (degree 2+) on last 60 points
    coeffs = polynomial_fit(x, y, degree)
    RETURN evaluate_polynomial(coeffs, 59 + horizon)
```

### 4.9 GARCH Volatility (Quick)

```
PSEUDOCODE computeGARCHVolatility(returns):
    alpha=0.05, beta=0.90
    omega = variance(returns) * (1 - alpha - beta)
    h = variance(returns)

    FOR t in 1..len(returns):
        h = omega + alpha * returns[t-1]^2 + beta * h

    RETURN sqrt(h)   // next-day volatility estimate
```

### 4.10 Support/Resistance & Candlestick Patterns

```
PSEUDOCODE identifyLevels(prices, lookback=60):
    window = prices[-lookback:]
    support    = min(window)     // simplified; actual uses local minima clustering
    resistance = max(window)
    RETURN SupportResistance { support, resistance }

PSEUDOCODE detectCandlestickPattern(candles):
    last3 = candles[-3:]
    IF is_hammer(last3[-1]):        RETURN PatternResult{"Hammer",     +0.3}
    IF is_engulfing_bull(last3):    RETURN PatternResult{"BullEngulf", +0.4}
    IF is_engulfing_bear(last3):    RETURN PatternResult{"BearEngulf", -0.4}
    IF is_doji(last3[-1]):          RETURN PatternResult{"Doji",        0.0}
    RETURN PatternResult{"None", 0.0}

PSEUDOCODE checkVolatilitySqueeze(prices):
    bb = computeBollingerBands(prices)
    bandwidth = (bb.upper - bb.lower) / bb.middle
    avg_bandwidth = average of recent bandwidths
    RETURN bandwidth < avg_bandwidth * 0.5   // squeeze = narrow bands
```

---

## 5. Signal Generation Engine (`TradingStrategy.cpp`)

The core `generateSignal()` function fuses all data sources into a single buy/sell/hold decision.

### 5.1 Regime Detection

```cpp
// Actual code from TradingStrategy.cpp:14-40
std::string detectMarketRegime(const std::vector<float>& prices) {
    if (prices.size() < 50) return "Unknown";

    float sma50 = /* SMA of last 50 */, sma200 = /* SMA of last 200 */;
    float current = prices.back();
    float vol = computeGARCHVolatility(calculateLogReturns(prices));

    if (vol > 0.03f) return "HighVol";    // Crisis mode
    if (current > sma50 && sma50 > sma200) return "Bull";
    if (current < sma50 && sma50 < sma200) return "Bear";
    return "Sideways";
}
```

### 5.2 Sentiment Blending (Adaptive Weighting)

```cpp
// Actual code from TradingStrategy.cpp:42-52
float blendSentiment(float stat_score, float sentiment, const std::string& regime) {
    float sentimentWeight = 0.25f;
    if (regime == "HighVol") sentimentWeight = 0.50f;       // news drives panic
    else if (std::abs(sentiment) >= 0.8f) sentimentWeight = 0.40f;  // extreme sentiment

    float technicalWeight = 1.0f - sentimentWeight;
    return (stat_score * technicalWeight) + (sentiment * sentimentWeight);
}
```

### 5.3 Full Signal Generation Pipeline

```
PSEUDOCODE generateSignal(symbol, candles, sentiment, fundamentals, onChain, levels, mlModel):

    closes = candles.map(c => c.close)
    currentPrice = closes.last()

    // Step 0: Compute indicators
    regime       = detectMarketRegime(closes)
    returns      = log_returns(closes)
    garchVol     = computeGARCHVolatility(returns)
    cyclePeriod  = detectCycle(closes)
    macd         = computeMACD(closes)         // (line, signal)
    rsi          = computeAdaptiveRSI(closes, 14)
    adx          = computeADX(candles)
    bb           = computeBollingerBands(closes)
    atr          = computeATR(candles, 14)

    // Step 1: ML Prediction
    features = mlModel.extractFeatures(rsi, macd.histogram, sentiment, garchVol, cyclePeriod)
    mlPredPct = mlModel.predict(features)    // predicted % return

    // Step 2: Technical Score (-1 to +1)
    stat_score = 0.0

    // Forecast component
    forecast = regime == "Sideways" ? forecastPricePoly(closes, 30) : forecastPrice(closes, 30)
    stat_score += clamp((forecast - currentPrice) / currentPrice * 5.0, -0.5, 0.5)

    // Bollinger Band + RSI (regime-aware)
    IF regime == "Bull":
        IF currentPrice < bb.lower: stat_score += 0.4   // buy the dip
        IF rsi < 40:                stat_score += 0.3
    ELIF regime == "Bear":
        IF currentPrice > bb.upper: stat_score -= 0.4   // sell the rip
        IF rsi > 60:                stat_score -= 0.3
    ELSE:  // Sideways - mean reversion
        IF currentPrice < bb.lower: stat_score += 0.3
        IF currentPrice > bb.upper: stat_score -= 0.3

    // MACD histogram
    stat_score += (macd.histogram > 0) ? 0.2 : -0.2

    // Step 3: Fundamental & On-Chain modifiers
    IF fund.pe_ratio < 15:  stat_score += 0.2
    IF fund.pe_ratio > 50:  stat_score -= 0.1
    IF onChain.net_inflow > 0: stat_score += 0.15
    IF onChain.net_inflow < 0: stat_score -= 0.15

    // Step 4: ML modifier
    IF mlPredPct >  0.5%:  stat_score += 0.15
    IF mlPredPct < -0.5%:  stat_score -= 0.15

    // Step 5: Blend with sentiment
    stat_score = clamp(stat_score, -1, 1)
    blended = blendSentiment(stat_score, sentiment, regime)

    // Step 6: Additional filters for ambiguous signals
    IF abs(blended) < 0.4:
        pattern = detectCandlestickPattern(candles)
        // Boost if pattern aligns near support/resistance
        squeeze = checkVolatilitySqueeze(closes)   // breakout imminent

    // Step 7: GARCH risk veto
    IF garchVol > 3% AND blended > 0:
        blended *= 0.5    // halve bullish signal in high-vol

    // Step 8: Support veto
    IF blended < -0.2 AND currentPrice <= support * 1.02:
        RETURN Signal { action="hold", reason="Bearish but at support" }

    // Step 9: Decision
    IF blended > 0.25:
        action = "buy"
        targets = nearest resistance levels above price
        confidence = 50% + (blended - 0.25) / 0.75 * 50%

        // Kelly position sizing
        winProb = 0.55 + blended * 0.20
        riskReward = (target - entry) / (entry - support)
        kelly = (winProb * (riskReward+1) - 1) / riskReward

    ELIF blended < -0.25:
        action = "sell"
        targets = nearest support levels below price
        confidence = 50% + (abs(blended) - 0.25) / 0.75 * 50%

    ELSE:
        action = "hold"
        prospectiveBuy  = nearest support
        prospectiveSell = nearest resistance

    // Step 10: Options recommendation (for strong signals)
    IF abs(blended) > 0.4:
        annualizedVol = garchVol * sqrt(252)
        IF blended > 0:
            strike = currentPrice * 1.05   // 5% OTM call
            premium = BlackScholes::callPrice(S=currentPrice, K=strike, T=45/365, r=4%, sigma=annualizedVol)
        ELSE:
            strike = currentPrice * 0.95   // 5% OTM put
            premium = BlackScholes::putPrice(...)

    RETURN Signal { action, entry, targets, confidence, reason, option }
```

---

## 6. Strategy Framework (`IStrategy.h`)

Extensible interface for pluggable strategies.

```cpp
// Abstract interface - actual code from IStrategy.h:77-118
class IStrategy {
public:
    virtual StrategySignal generateSignal(const vector<Candle>& history, size_t idx) = 0;
    virtual string getName() const = 0;
    virtual int getWarmupPeriod() const = 0;
    virtual vector<StrategyParams> getParameters() const;  // for optimization
    virtual void setParameters(const vector<StrategyParams>& params);
    virtual void reset();                                   // for walk-forward
    virtual void onTradeExecuted(float entry, float exit, bool isWin);  // for learning
    virtual unique_ptr<IStrategy> clone() const = 0;        // for parallel optimization
};
```

### 6.1 MeanReversionStrategy

```
PSEUDOCODE MeanReversionStrategy::generateSignal(history, idx):
    closes = history.map(c => c.close)
    bb  = computeBollingerBands(closes, bbPeriod=20, multiplier=2.0)
    rsi = computeRSI(closes, period=14)   // or adaptive
    atr = computeATR(history, 14)
    current = closes.last()

    // Buy: oversold
    IF rsi < 30 OR current < bb.lower:
        strength = 0.5
        IF (rsi < 30 AND current < bb.lower): strength = 0.8
        IF rsi < 20: strength = 1.0
        RETURN buy(strength, stopLoss = current - 2*ATR, takeProfit = bb.middle)

    // Sell: overbought
    IF rsi > 70 OR current > bb.upper:
        strength = 0.5
        IF (rsi > 70 AND current > bb.upper): strength = 0.8
        IF rsi > 80: strength = 1.0
        RETURN sell(strength, stopLoss = current + 2*ATR, takeProfit = bb.middle)

    RETURN hold()

    // Optimizable parameters (with ranges for walk-forward):
    // rsiPeriod:        7-28 step 1
    // rsiBuyThreshold:  20-40 step 5
    // rsiSellThreshold: 60-80 step 5
    // bbPeriod:         10-30 step 5
    // bbMultiplier:     1.5-3.0 step 0.25
```

### 6.2 TrendFollowingStrategy

```
PSEUDOCODE TrendFollowingStrategy::generateSignal(history, idx):
    closes = history.map(c => c.close)
    fastMA = SMA(closes, period=10)
    slowMA = SMA(closes, period=30)
    prevFastMA = SMA(closes[:-1], 10)
    prevSlowMA = SMA(closes[:-1], 30)
    adx = computeADX(history, 14)
    atr = computeATR(history, 14)

    bullishCross = (prevFastMA <= prevSlowMA) AND (fastMA > slowMA)
    bearishCross = (prevFastMA >= prevSlowMA) AND (fastMA < slowMA)

    IF bullishCross AND adx.adx >= 25:
        strength = min(1.0, (adx.adx - 25) / 25 + 0.5)
        IF adx.plusDI > adx.minusDI: strength += 0.2
        RETURN buy(strength, stopLoss = current - 2.5*ATR, takeProfit = 0)  // let trend run

    IF bearishCross AND adx.adx >= 25:
        ...analogous sell...

    IF adx.adx < 25 * 0.7:  // trend died
        RETURN weak exit signal (strength 0.3)

    RETURN hold()
```

### 6.3 TripleMAStrategy

```
PSEUDOCODE: Buy when fast(5) > medium(20) > slow(50) AND ADX >= 20
            Sell when fast < medium < slow AND ADX >= 20
```

### 6.4 EnhancedMeanReversionStrategy

```
PSEUDOCODE: Same as MeanReversion PLUS:
    - Volume filter: only act if current volume > 120% of 20-day average
    - Trend filter: reduce signal strength by 50% if going against the 50-SMA trend
```

---

## 7. Machine Learning (`MLPredictor.h/cpp`)

### 7.1 Feature Engineering (15-dimensional)

```cpp
// Actual feature vector layout from MLPredictor.h:28-36
// [0]  RSI normalized to 0..1          = rsi / 100
// [1]  MACD histogram (tanh-squashed)  = tanh(macdHist * 10)
// [2]  Sentiment score                 = raw -1..1
// [3]  GARCH vol normalized            = tanh(garchVol * 50)
// [4]  Cycle phase                     = cos(2*PI * dayIndex / cyclePeriod)
// [5]  Lagged return t-1
// [6]  Lagged return t-2
// [7]  Lagged return t-3
// [8]  Lagged return t-5
// [9]  Lagged return t-10
// [10] RSI * Sentiment                 (cross-product)
// [11] MACD * Volatility
// [12] RSI * Volatility
// [13] Sentiment * CyclePhase
// [14] MACD * CyclePhase
```

### 7.2 Model Architecture

```
PSEUDOCODE MLPredictor (Online Linear Regressor):
    weights[15], bias  // L2-regularized SGD
    train(features, target):
        prediction = dot(weights, features) + bias
        error = prediction - target
        FOR i in 0..15:
            weights[i] -= lr * (error * features[i] + lambda * weights[i])
        bias -= lr * error

PSEUDOCODE NeuralNetPredictor:
    Architecture: 15 -> 8 (tanh) -> 1 (linear)
    Training: backpropagation with momentum (0.9) and L2 regularization
    Activation: tanh for hidden, linear for output

PSEUDOCODE RidgeRegression:
    Closed-form: w = (X'X + lambda*I)^{-1} * X'y
    Fit once on batch data, then predict

PSEUDOCODE EnsemblePredictor:
    prediction = 0.4 * linear.predict(f)
               + 0.3 * neural.predict(f)
               + 0.3 * ridge.predict(f)
```

---

## 8. Backtesting Engine (`Backtester.h/cpp`)

### 8.1 Configuration

```cpp
// Actual code from BacktestConfig.h:106-136
struct BacktestConfig {
    float initialCapital = 10000.0f;
    TransactionCostModel costs;     // commission 0.1%, slippage 0.05%, min $1
    RiskManagement risk;            // stop-loss, take-profit, trailing stop, max-DD breaker
    PositionSizing sizing;          // FixedFraction(10%), Kelly(half), ATRBased, EqualWeight
    int warmupPeriod = 60;
    float riskFreeRate = 0.04f;     // 4% annual
    bool allowShort = false;
    bool reinvestProfits = true;

    // Presets
    static BacktestConfig realisticConfig();   // 0.1% commission, 2% stop, 4% TP
    static BacktestConfig aggressiveConfig();  // Kelly sizing, 5% stop, 50% max position
};
```

### 8.2 Position Sizing

```cpp
// Actual code from BacktestConfig.h:67-102
float PositionSizing::calculateFraction(float winRate, float avgWinLossRatio, float atr, float price) {
    switch (method) {
        case FixedFraction: return fixedFraction;       // default 10%
        case Kelly: {
            float kelly = (avgWinLossRatio * winRate - (1-winRate)) / avgWinLossRatio;
            return max(0, kelly * kellyFraction);       // half-Kelly default
        }
        case ATRBased:
            return (atrRiskPerTrade * price) / atr;     // size so 1 ATR = 1% capital risk
        case EqualWeight:
            return fixedFraction;
    }
    return min(fraction, maxPositionSize);              // cap at 25%
}
```

### 8.3 Backtest Loop

```
PSEUDOCODE Backtester::run(candles, strategy):
    capital = config.initialCapital
    position = Position(closed)
    equityCurve = []
    tradeLog = []

    FOR i in warmupPeriod..len(candles):
        candle = candles[i]
        history = candles[0..i+1]

        // 1. Check risk management on open positions
        IF position.isOpen:
            updateTrailingStop(position, candle)
            IF checkStopLoss(position, candle, exitPrice):
                trade = closePosition(position, candle, "stop_loss")
                tradeLog.push(trade)
            ELIF checkTakeProfit(position, candle, exitPrice):
                trade = closePosition(position, candle, "take_profit")
                tradeLog.push(trade)

        // 2. Generate strategy signal
        signal = strategy.generateSignal(history, i)

        // 3. Execute signal
        IF signal.type == Buy AND NOT position.isOpen:
            openPosition(position, candle, i, capital, signal, isLong=true)
        ELIF signal.type == Sell AND position.isOpen AND position.isLong:
            trade = closePosition(position, candle, "signal")
            tradeLog.push(trade)

        // 4. Track equity
        equity = capital + (position.isOpen ? unrealized_pnl(position, candle) : 0)
        equityCurve.push(equity)

    // 5. Force close at end of data
    IF position.isOpen:
        tradeLog.push(closePosition(position, candles.last(), "end_of_data"))

    // 6. Calculate comprehensive metrics
    calculateMetrics(result, dailyReturns, len(candles))
    RETURN result
```

### 8.4 Transaction Cost Model

```cpp
// Actual code from BacktestConfig.h:11-24
float TransactionCostModel::calculateCost(float tradeValue) {
    float commission = tradeValue * commissionPercent;
    return std::max(minCommission, commission);   // min $1
}

float TransactionCostModel::applySlippage(float price, bool isBuy) {
    if (isBuy) return price * (1.0f + slippagePercent);   // pay more
    else       return price * (1.0f - slippagePercent);    // receive less
}
```

### 8.5 Risk Metrics Calculation

```
PSEUDOCODE calculateMetrics(result, dailyReturns, totalBars):
    // Sharpe = (mean_return - rf/252) / stddev * sqrt(252)
    // Sortino = (mean_return - rf/252) / downside_stddev * sqrt(252)
    // Calmar = annualized_return / max_drawdown
    // VaR(95%) = sorted_returns[5th percentile]
    // CVaR(95%) = mean(returns below VaR)
    // ProfitFactor = sum(winning_trades) / sum(losing_trades)
    // Expectancy = winRate * avgWin - lossRate * avgLoss
```

---

## 9. Walk-Forward Optimization (`WalkForward.h`)

Validates that a strategy works on unseen data, not just curve-fitted.

```
PSEUDOCODE WalkForwardOptimizer::run(strategy, data, paramGrids):
    windows = generateWindows(data.size())
    // e.g., 5 rolling windows, each 70% train / 30% test

    FOR each window:
        trainData = data[window.trainStart .. window.trainEnd]
        testData  = data[window.testStart  .. window.testEnd]

        // Grid search optimization on training data
        bestParams = NULL, bestObjective = -inf
        FOR each combination of paramGrids:
            clone = strategy.clone()
            clone.setParameters(combination)
            result = backtester.run(trainData, clone)
            IF getObjective(result) > bestObjective:
                bestParams = combination

        // Test on out-of-sample data
        strategy.setParameters(bestParams)
        trainResult = backtester.run(trainData, strategy)
        strategy.reset()
        testResult  = backtester.run(testData, strategy)

        efficiency = testResult.sharpe / trainResult.sharpe

    // Aggregate
    walkForwardEfficiency = mean(efficiencies)
    robustnessScore = 1 / (1 + coefficientOfVariation(testReturns))

    isRobust = efficiency > 0.5 AND robustnessScore > 0.6
```

### Monte Carlo Validation

```
PSEUDOCODE MonteCarloValidator::validate(strategy, data, config, numSims=100):
    FOR sim in 0..numSims:
        noisyData = addNoise(data, level=0.001)  // +/-0.1% noise to OHLC
        result = backtester.run(noisyData, strategy)
        returns.push(result.totalReturn)

    RETURN {
        meanReturn     = mean(returns),
        stdReturn      = stddev(returns),
        probability95  = sorted(returns)[5th percentile]
    }
```

---

## 10. Volatility Models (`VolatilityModels.h`)

### 10.1 GARCH(1,1) -- MLE Fitting

```
PSEUDOCODE GARCHModel::fit(returns):
    // sigma^2_t = omega + alpha * epsilon^2_{t-1} + beta * sigma^2_{t-1}
    residuals = returns - mean(returns)
    variance  = var(residuals)
    alpha=0.05, beta=0.90, omega = variance * (1 - alpha - beta)

    FOR iter in 0..100:
        // Forward pass: compute conditional variances
        h[0] = variance
        logLik = 0
        FOR t in 1..N:
            h[t] = omega + alpha * residuals[t-1]^2 + beta * h[t-1]
            logLik -= 0.5 * (log(h[t]) + residuals[t]^2 / h[t])

        // Convergence check
        IF abs(logLik - prevLogLik) < 1e-6: BREAK

        // Numerical gradient + update (gradient ascent on log-likelihood)
        alpha += lr * dLogLik/dAlpha
        beta  += lr * dLogLik/dBeta
        omega += lr * dLogLik/dOmega

        // Constraints: alpha in [0.0001, 0.5], alpha+beta < 1

    // Forecast: h_{t+k} = omega + (alpha+beta) * h_{t+k-1}  -> converges to longRunVariance
```

### 10.2 EGARCH (Asymmetric / Leverage Effect)

```
PSEUDOCODE:
    // log(sigma^2_t) = omega + alpha*(|z_{t-1}| - sqrt(2/pi)) + gamma*z_{t-1} + beta*log(sigma^2_{t-1})
    // gamma < 0 means negative shocks increase volatility more (leverage effect)
    // Fitted via grid search over alpha=[0.05,0.30], gamma=[-0.20,0.10], beta=[0.70,0.95]
```

### 10.3 Realized Volatility Estimators

```
PSEUDOCODE:
    Historical:    sqrt(var(returns[-period:]))
    Parkinson:     sqrt(sum(log(H/L)^2) / (4*ln(2)*N))        // high-low range
    Garman-Klass:  sqrt(sum(0.5*(log(H/L))^2 - (2*ln2-1)*(log(C/O))^2) / N)   // OHLC
    Annualize:     dailyVol * sqrt(252)
```

---

## 11. Portfolio Optimization (`PortfolioOptimizer.h`)

### 11.1 Mean-Variance Optimizer

```
PSEUDOCODE PortfolioOptimizer:
    // Input: expected returns vector, covariance matrix

    minimumVariance():
        weights = [1/N, ..., 1/N]   // equal start
        FOR 1000 iterations:
            gradient[i] = 2 * sum_j(cov[i][j] * weights[j])
            weights[i] -= lr * gradient[i]
            weights = clamp(weights, minWeight, maxWeight)
            weights = normalize(weights)    // sum to 1
        RETURN OptimizedPortfolio { weights, return, volatility, sharpe }

    maxSharpe():
        // Numerical gradient ascent on Sharpe = (ret - rf) / vol
        FOR 2000 iterations:
            FOR each asset i:
                sharpeHigh = sharpe(weights + h*e_i)
                sharpeLow  = sharpe(weights - h*e_i)
                gradient[i] = (sharpeHigh - sharpeLow) / (2*h)
            weights += lr * gradient
            weights = normalize(clamp(weights))

    riskParity():
        // Start with inverse-volatility weights
        weights[i] = (1/vol_i) / sum(1/vol_j)
        // Iteratively equalize risk contributions
        FOR 500 iterations:
            marginalRisk[i] = sum_j(cov[i][j] * weights[j]) / portfolioVol
            riskContrib[i] = weights[i] * marginalRisk[i]
            targetRisk = portfolioVol / N
            weights[i] += 0.1 * (targetRisk - riskContrib[i]) / marginalRisk[i]
            normalize(weights)

    blackLitterman(marketWeights, views, tau=0.05, confidence=0.5):
        // Implied equilibrium returns: pi = 2.5 * Cov * marketWeights
        // Blend: adjustedReturns[i] = (1-conf)*pi[i] + conf*viewReturn[i]
        // Then run maxSharpe() on adjusted returns

    efficientFrontier(numPoints=20):
        // For each target return from min to max:
        //   find minimum variance portfolio with that return constraint
```

### 11.2 Portfolio Manager (Multi-Asset Backtesting)

```
PSEUDOCODE PortfolioManager:
    // State: cash balance, map<symbol, AssetHolding>
    // Holdings track: quantity, avgCostBasis, currentPrice, unrealizedPnL

    rebalance(targetAllocation, prices, barIndex):
        // 1. Sell overweight positions first (free up cash)
        FOR each holding where currentWeight > targetWeight:
            sell (currentValue - targetValue) worth of shares
            collect proceeds minus transaction costs

        // 2. Buy underweight positions
        FOR each target where targetWeight > currentWeight:
            buy up to (targetValue - currentValue) limited by available cash
            deduct cost + transaction fees

    needsRebalance(target, barIndex):
        MATCH config.rebalanceFreq:
            Daily:     RETURN true
            Weekly:    RETURN barIndex % 5 == 0
            Monthly:   RETURN barIndex % 21 == 0
            Quarterly: RETURN barIndex % 63 == 0
            Threshold: RETURN any |currentWeight - targetWeight| > 5%
```

---

## 12. Black-Scholes Options Pricing (`BlackScholes.h/cpp`)

```
PSEUDOCODE BlackScholes:
    callPrice(S, K, T, r, sigma):
        d1 = (ln(S/K) + (r + sigma^2/2)*T) / (sigma*sqrt(T))
        d2 = d1 - sigma*sqrt(T)
        RETURN S*N(d1) - K*exp(-r*T)*N(d2)

    putPrice(S, K, T, r, sigma):
        RETURN callPrice - S + K*exp(-r*T)    // put-call parity

    calculateGreeks(S, K, T, r, sigma, isCall):
        delta = isCall ? N(d1) : N(d1) - 1
        gamma = pdf(d1) / (S * sigma * sqrt(T))
        theta = -(S*pdf(d1)*sigma)/(2*sqrt(T)) - r*K*exp(-r*T)*N(d2)
        vega  = S * pdf(d1) * sqrt(T)
        RETURN Greeks { delta, gamma, theta, vega }

    impliedVolatility(marketPrice, S, K, T, r, isCall):
        // Newton-Raphson: iterate sigma until BS_price(sigma) = marketPrice
```

---

## 13. Error Handling (`Result.h`) -- Rust-style

```cpp
// Actual code from Result.h:72-198
template<typename T>
class Result {
    std::variant<T, Error> data_;

    bool isOk() const;
    T& value();                          // throws if error
    T valueOr(const T& defaultValue);
    const Error& error() const;

    // Monadic operations
    auto map(F&& f)     -> Result<U>;    // transform value
    auto andThen(F&& f) -> Result<U>;    // chain Result-returning ops
    Result<T> mapError(F f);             // transform error
    Result<T> orElse(F f);               // fallback on error
    auto match(okFn, errFn);             // pattern matching

    std::optional<T> toOptional();       // discard error info
};

// Error codes: Network=1000, Parse=2000, Validation=3000,
//              NotFound=4000, Timeout=5000, RateLimit=6000, Auth=7000, Internal=8000

// Helper: combineResults(vector<Result<T>>) -> Result<vector<T>>
// Helper: tryExecute(lambda) -> wraps exceptions into Result
```

---

## 14. Caching & Rate Limiting (`Cache.h`)

### 14.1 Thread-Safe LRU Cache

```cpp
// Actual code from Cache.h:13-221
template<typename Key, typename Value>
class LRUCache {
    // Internal: doubly-linked list (LRU order) + map to (list iterator, value+expiry)
    size_t capacity_;              // default 100
    std::chrono::seconds ttl_;     // default 300s

    optional<Value> get(Key);      // returns nullopt if missing/expired, promotes to MRU
    void put(Key, Value);          // inserts or updates, evicts expired then LRU if full
    void put(Key, Value, customTTL);
    Value getOrCompute(Key, lambda);  // cache-aside pattern
    void cleanup();                // manually evict expired entries
};
```

### 14.2 Rate Limiter

```cpp
// Actual code from Cache.h:237-343
class RateLimiter {
    // Per-domain: { lastRequest, requestCount, windowStart }
    int maxRequestsPerWindow_ = 60;          // 60 requests
    chrono::seconds windowDuration_ = 60s;   // per 60 seconds
    chrono::milliseconds minInterval_ = 100ms;

    bool allowRequest(string domain);        // check + record
    void waitForAllowance(string domain);    // blocking wait
    chrono::milliseconds getWaitTime(string domain);
};
```

---

## 15. Main Execution Flow (`main.cpp`)

```cpp
// Actual code from main.cpp:189-263
int main() {
    // 1. Initialize config from environment variables
    Config::getInstance().initialize();
    NetworkUtils::setApiKey("NEWSAPI", Config::getInstance().getApiKey("NEWSAPI"));
    SentimentAnalyzer::getInstance().init("sentiment.gguf");

    // 2. Read tickers from CSV
    vector<Ticker> tickers = readTickers("tickers.csv");  // {symbol, type}
    MLPredictor mlModel;

    // 3. Parallel analysis (batches of 4, 500ms between batches)
    for (size_t i = 0; i < tickers.size(); i += 4) {
        vector<future<void>> batch;
        for (j = i; j < i+4 && j < tickers.size(); ++j)
            batch.push_back(async(launch::async, processTicker, tickers[j], mlModel));
        for (auto& f : batch) f.wait();
        sleep(500ms);
    }

    // 4. Enhanced backtest on first ticker with >200 bars
    for (auto& result : globalResults)
        if (result.history.size() > 200) {
            runEnhancedBacktest(result.symbol, result.history);
            break;
        }

    // 5. Generate reports
    ReportGenerator::generateCSV(globalResults, "report.csv");
    ReportGenerator::generateHTML(globalResults, "report.html");   // Chart.js interactive

    // 6. Print summary
    for (auto& r : globalResults)
        print(r.symbol, r.backtest.totalReturn, r.backtest.sharpeRatio, ...);
}
```

### Thread-safe result collection

```cpp
// Actual code from main.cpp:57-59
std::mutex resultsMutex;
std::vector<AnalysisResult> globalResults;

// In processTicker():
{
    std::lock_guard<std::mutex> lock(resultsMutex);
    globalResults.push_back(res);
}
```

---

## 16. Configuration System (`Config.h`)

```
PSEUDOCODE Config (Singleton):
    initialize():
        // Reads API keys ONLY from environment variables (no hardcoded defaults)
        apiKeys["NEWSAPI"]      = getenv("NEWS_KEY")
        apiKeys["ALPHAVANTAGE"] = getenv("ALPHA_VANTAGE_KEY")
        logLevel                = getenv("LOG_LEVEL") or "INFO"
        cacheDir                = getenv("CACHE_DIR") or ".cache"

    getApiKey(name):
        RETURN apiKeys[name]   // empty string if not set

    // Keys are masked in output: "ab****yz" (first 2 + last 2 chars visible)
    // Thread-safe via mutex
```

---

## 17. Network Layer (`NetworkUtils.h/cpp`)

```
PSEUDOCODE NetworkUtils::httpGet(url):
    // Rate limiter check (60 req/min per domain)
    domain = extract_domain(url)
    IF NOT rateLimiter.allowRequest(domain):
        wait or return cached

    // Cache check
    cacheKey = hash(url)
    IF cache.contains(cacheKey):
        RETURN cache.get(cacheKey)

    // HTTP request (libcurl)
    curl_easy_setopt(CURLOPT_URL, url)
    curl_easy_setopt(CURLOPT_USERAGENT, "Mozilla/5.0 Chrome/120.0.0.0")
    curl_easy_setopt(CURLOPT_TIMEOUT, 15)
    curl_easy_setopt(CURLOPT_SSL_VERIFYPEER, true)
    curl_easy_setopt(CURLOPT_FOLLOWLOCATION, true)
    response = curl_easy_perform()

    cache.put(cacheKey, response)   // TTL = 300s
    RETURN response
```

---

## 18. Key Constants & Defaults

| Constant | Value | Where Used |
|----------|-------|------------|
| Initial Capital | $10,000 | BacktestConfig |
| Commission | 0.1% (min $1) | TransactionCostModel |
| Slippage | 0.05% | TransactionCostModel |
| Risk-Free Rate | 4% annual | Sharpe/Sortino |
| RSI Period | 14 (adaptive 7-28) | TechnicalAnalysis |
| MACD | (12, 26, 9) | TechnicalAnalysis |
| Bollinger Bands | 20-period, 2.0 std | TechnicalAnalysis |
| ADX Period | 14, threshold 25 | TrendFollowing |
| GARCH | alpha=0.05, beta=0.90 | VolatilityModels |
| Kelly Fraction | 0.50 (half-Kelly) | PositionSizing |
| Cache TTL | 300 seconds | LRUCache |
| Cache Capacity | 100 entries | LRUCache |
| Rate Limit | 60 req / 60s / domain | RateLimiter |
| Batch Size | 4 tickers concurrent | main.cpp |
| ML Features | 15 dimensions | MLPredictor |
| Warmup Bars | 60 | BacktestConfig |
| Forecast Horizon | 30 days | TradingStrategy |
| Options Expiry | 45 days | TradingStrategy |
| Walk-Forward Windows | 5 | WalkForward |
| Train/Test Split | 70/30 | WalkForward |

---

## 19. File Dependency Graph

```
main.cpp
  |-- MarketData.h/cpp        (Yahoo Finance API, fetchCandles, fetchFundamentals)
  |-- TradingStrategy.h/cpp   (generateSignal, detectMarketRegime, blendSentiment)
  |     |-- TechnicalAnalysis.h/cpp   (RSI, MACD, ATR, BB, ADX, GARCH, cycles, forecasts)
  |     |-- MLPredictor.h/cpp         (online linear, neural net, ridge, ensemble)
  |     |-- BlackScholes.h/cpp        (options pricing, Greeks)
  |-- NewsManager.h/cpp        (Yahoo RSS, NewsAPI)
  |-- SentimentAnalyzer.h/cpp  (llama.cpp GGUF, keyword fallback)
  |-- Backtester.h/cpp         (position management, risk controls, 40+ metrics)
  |     |-- BacktestConfig.h   (costs, risk mgmt, position sizing, presets)
  |     |-- IStrategy.h        (abstract interface, StrategyBase)
  |-- IStrategy.h
  |     |-- Strategies/MeanReversionStrategy.h    (RSI + BB mean reversion)
  |     |-- Strategies/TrendFollowingStrategy.h   (MA crossover + ADX)
  |     |-- Strategies/MLStrategy.h               (ML-based with online learning)
  |-- WalkForward.h            (rolling/anchored windows, grid search, Monte Carlo)
  |-- VolatilityModels.h       (GARCH, EGARCH, realized vol, term structure)
  |-- PortfolioOptimizer.h     (min-var, max-sharpe, risk-parity, Black-Litterman)
  |-- Portfolio.h              (PortfolioManager, rebalancing, multi-asset backtest)
  |-- Config.h                 (env vars, singleton)
  |-- Cache.h                  (LRU cache, rate limiter)
  |-- NetworkUtils.h/cpp       (HTTP with caching + rate limiting)
  |-- Logger.h                 (thread-safe file + console)
  |-- ReportGenerator.h        (CSV + HTML with Chart.js)
  |-- Result.h                 (Rust-style Result<T>, monadic ops)
  |-- json.hpp                 (nlohmann/json)
```

---

## 20. How to Extend

### Add a new strategy:

```cpp
class MyStrategy : public StrategyBase {
public:
    MyStrategy() : StrategyBase("MyStrategy", 60) {
        addParam("threshold", 0.5f, 0.1f, 1.0f, 0.1f);  // optimizable
    }

    StrategySignal generateSignal(const vector<Candle>& history, size_t idx) override {
        // your logic here using history[0..idx]
        return StrategySignal::buy(0.8f, "my reason");
    }

    unique_ptr<IStrategy> clone() const override {
        return make_unique<MyStrategy>(*this);
    }
};
```

### Add a new data source:

Implement a fetch function returning `vector<Candle>` or a struct, call it in `processTicker()`.

### Add a new indicator:

Add function to `TechnicalAnalysis.h/cpp`, then reference it in `generateSignal()` or a strategy's `generateSignal()`.

---

## 21. Python Integration Layer (Added March 2026)

> **Date Added**: March 2026
> **Purpose**: Connect C++ trading bot to Python ML models, news sentiment, and scheduled training

### 21.1 Architecture Overview

```
Telegram User
     |
     v
telegram_listener.py (Python)
     |
     +-- /run --------> Start Python API (FastAPI)
     |                      |
     |                      v
     |                 scheduler.py (Background)
     |                      |
     |                      +-- scrape_news() - every hour
     |                      +-- update_features() - every 15 min
     |                      +-- generate_signals() - every 5 min
     |                      +-- retrain_models() - daily 4PM
     |
     +-- Run C++ trading_bot.exe (with USE_ONNX_MODEL=true)
                            |
                            v
                       ONNX models (from Python)
                       Sentiment API (from news.db)
```

### 21.2 Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `telegram_listener.py` | Trading_cpp/ | Telegram bot commands |
| `scheduler.py` | Trading_Python/src/service/ | Background task scheduler |
| `market_hours.py` | Trading_Python/src/service/ | Market hours detection |
| `sentiment.py` | Trading_Python/src/api/routes/ | Sentiment API endpoints |
| `train_model.py` | Trading_Python/scripts/ | ONNX model training |
| News scraper | Trading_Python/src/news/ | RSS news fetching & classification |

### 21.3 Market Hours System

```python
# market_hours.py
def is_market_open() -> bool:
    # Regular session: Mon-Fri, 9:30 AM - 4:00 PM ET
    return weekday < 5 and time(9, 30) <= now < time(16, 0)

def is_extended_hours() -> bool:
    # Extended: Mon-Fri, 8:00 AM - 6:00 PM ET
    return weekday < 5 and time(8, 0) <= now < time(18, 0)

def should_run_scheduled_task() -> bool:
    # Tasks only run during extended hours
    return is_extended_hours()
```

**Scheduler Behavior**:
- Tasks run ONLY during extended market hours (8 AM - 6 PM ET, weekdays)
- Outside hours: scheduler stays idle, saves resources
- C++ bot also checks market hours before running

---

## 22. Sentiment API Integration (Added March 2026)

> **Date Added**: March 2026
> **Purpose**: Replace mock sentiment with real news-driven sentiment from Python news database

### 22.1 Sentiment Data Flow

```
news.db (SQLite)
     |
     v
sentiment.py API
     |
     +-- _get_real_sentiment() - Query database
     +-- _fetch_fresh_news_sentiment() - Fallback: scrape fresh news
     +-- _generate_mock_sentiment() - Final fallback
     |
     v
C++ trading_bot (via HTTP API)
```

### 22.2 API Endpoints

```python
# GET /api/sentiment/{ticker}?days=7
{
    "ticker": "NVDA",
    "timestamp": "2026-03-17T10:30:00",
    "sentiment_score": 0.65,      # -1.0 to 1.0
    "confidence": 0.75,            # Based on article count
    "article_count": 12,
    "headline": "NVDA reports strong earnings...",
    "source": "news_db"           # or "fresh_news" or "mock_fallback"
}
```

### 22.3 Fallback Chain

1. **Check cache** - In-memory cache (1 hour TTL)
2. **Query news.db** - Real sentiment from stored articles
3. **Fresh news scrape** - Fetch from RSS if no DB data
4. **Mock fallback** - Return random sentiment (marked as `source: mock_fallback`)

### 22.4 Database Schema

```sql
CREATE TABLE articles (
    id INTEGER PRIMARY KEY,
    title TEXT,
    url TEXT UNIQUE,
    source TEXT,
    industry TEXT,
    published_date TEXT,
    classification TEXT,  -- promising, bad, neutral
    score REAL,            -- -30 to +30 (keyword-based)
    company_name TEXT,
    ticker TEXT,
    description TEXT
);
```

---

## 23. Intelligent Ticker Selection (Added March 2026)

> **Date Added**: March 2026
> **Purpose**: Select top 7 tickers for ONNX model training based on actionable criteria

### 23.1 Selection Heuristics

| Heuristic | Weight | What It Measures |
|-----------|--------|------------------|
| Volatility | 25% | Annualized std dev of returns |
| Volume Spike | 15% | Today's volume vs 20-day average |
| Price Momentum | 15% | Last 5 days return |
| Gap Analysis | 10% | Price gaps >2% |
| News Recency | 15% | Hours since last news |
| News Severity | 10% | Sentiment strength |
| News Frequency | 10% | Number of articles |

### 23.2 Selection Process

```python
# scheduler.py - _select_top_tickers()
def _select_top_tickers(tickers, max_tickers=7):
    # 1. Check for manual override
    manual = _get_manual_tickers()
    if manual:
        return manual[:7]  # User-specified tickers

    # 2. Calculate actionability scores
    scores = _calculate_ticker_actionability(tickers, days=30)

    # 3. Sort and select top 7
    sorted_tickers = sorted(scores.items(), key=lambda x: x[1], reverse=True)
    return [t for t, _ in sorted_tickers[:7]]
```

### 23.3 Manual Override

Users can override intelligent selection via Telegram:

```
/selected          - Show current selection
/swap NVDA AMD    - Replace NVDA with AMD
/set AAPL,GOOGL   - Set manual tickers (max 7)
/addticker META   - Add META to selection
/removeticker TSLA - Remove TSLA
/auto             - Revert to intelligent selection
```

Files:
- Manual override: `Trading_Python/data/manual_tickers.txt`
- Intelligent selection: `Trading_Python/models/selected_tickers.txt`

---

## 24. Telegram Bot Commands (Added March 2026)

> **Date Added**: March 2026
> **Purpose**: Control trading system via Telegram

### 24.1 Core Commands

| Command | Description |
|---------|-------------|
| `/add SYMBOL` | Add ticker to C++ watchlist |
| `/remove SYMBOL` | Remove from watchlist |
| `/analyze SYMBOL` | Get last analysis for symbol |
| `/fundamentals SYMBOL` | Get Yahoo Finance fundamentals |
| `/list` | Show all watchlist tickers |
| `/signals` | Show all current signals |
| `/sentiment` | Get market sentiment (from scraper) |
| `/news` | Get latest stock news |
| `/pairs` | Show pairs trading opportunities |

### 24.2 Trading Commands

| Command | Description |
|---------|-------------|
| `/run` | Run full pipeline: start Python API → scrape news → train models → run C++ bot |
| `/scrape` | Scrape latest news (populate sentiment DB) |
| `/train` | Train ONNX models on selected tickers |

### 24.3 Ticker Selection Commands

| Command | Description |
|---------|-------------|
| `/selected` | Show current ticker selection |
| `/swap OLD NEW` | Replace a ticker in selection |
| `/set T1,T2,...` | Set manual tickers (max 7) |
| `/addticker T` | Add ticker to selection |
| `/removeticker T` | Remove ticker from selection |
| `/auto` | Clear manual, use intelligent selection |

### 24.4 /run Command Flow

```python
def run_trading_bot_async():
    # Step 1: Start Python API if not running
    if not is_python_api_running():
        start_python_api_service()

    # Step 2: Check ONNX model
    if check_onnx_model():
        print("ONNX model found - ML enabled")

    # Step 3: Run C++ with ONNX config
    env = {
        "USE_ONNX_MODEL": "true",
        "ONNX_MODEL_PATH": ".../stock_predictor.onnx",
        "PYTHON_API_URL": "http://localhost:8000"
    }
    subprocess.run([TRADING_BOT_PATH, "scheduled"], env=env)

    # Step 4: Fetch and display results
    signals = analyze_signals_status()
    send_message(signals)
```

---

## 25. ONNX Model Training & Deployment (Added March 2026)

> **Date Added**: March 2026
> **Purpose**: Train ML models in Python, deploy to C++ for inference

### 25.1 Training Pipeline

```python
# scheduler.py - retrain_models()

def retrain_models():
    # 1. Select top 7 tickers (intelligent or manual)
    tickers = _select_top_tickers(load_cpp_tickers(), max_tickers=7)

    # 2. Fetch 2 years of data
    data = fetch_yahoo_data(tickers, start_date="2 years ago")

    # 3. Add features
    features = engine.add_all_indicators(data)

    # 4. Train model
    trainer, onnx_path = train_and_export(
        features,
        target_col="signal",
        model_type="ridge"
    )

    # 5. Copy to C++ directory
    _copy_model_to_cpp()

    # 6. Create reload signal
    _create_reload_signal()
```

### 25.2 Model Files

| File | Location | Purpose |
|------|----------|---------|
| `trading_model.onnx` | Trading_Python/models/ | General model |
| `cluster_*.onnx` | Trading_Python/models/clusters/ | Cluster-specific |
| `stock_predictor.onnx` | Trading_cpp/models/ | Deployed model |
| `reload.signal` | Trading_cpp/models/ | Signal to reload |

### 25.3 C++ ONNX Integration

```cpp
// Config.h
USE_ONNX_MODEL = true
ONNX_MODEL_PATH = "models/stock_predictor.onnx"

// main.cpp
#ifdef USE_ONNXRUNTIME
    ONNXPredictor onnxPredictor(onnxPath);
    if (onnxPredictor.isLoaded()) {
        // Use ONNX for predictions
        prediction = onnxPredictor.predict(features);
    }
#endif
```

---

## 26. Data Flow Summary (March 2026)

```
┌─────────────────────────────────────────────────────────────────┐
│                    TELEGRAM USER                                 │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           v
┌──────────────────────────────────────────────────────────────────┐
│                 telegram_listener.py                              │
│  /run → starts Python API → runs C++ bot → displays results    │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
           ┌───────────────┼───────────────┐
           v               v               v
    ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
    │ Python API   │ │ C++ Bot      │ │ Files        │
    │ (FastAPI)    │ │ (C++)        │ │              │
    └──────┬───────┘ └──────┬───────┘ └──────────────┘
           │                 │
           v                 v
    ┌──────────────┐ ┌──────────────────────────────────┐
    │ scheduler.py │ │ ONNX models                     │
    │               │ │ (Python → C++)                  │
    └──────┬───────┘ └──────────────────────────────────┘
           │
     ┌─────┼─────┬──────────┐
     v     v     v          v
  news  features signals  models
  .db   cache   cache     .onnx

Legend:
  • Scheduler runs 8AM-6PM ET (market hours)
  • Sentiment from news.db (real) or fallback
  • Ticker selection: intelligent (7 heuristics) or manual
  • C++ uses Python-trained ONNX models
```
