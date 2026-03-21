## Plan for Claude Code (Live Trading, Equities, Medium-Frequency Intraday)

Goal: turn the current “analysis + backtest” codebase into a **robust live trading service** with (1) correct time handling, (2) reliable data + broker integration, (3) deterministic bar-close execution, (4) thread-safe ML/sentiment, (5) risk controls + kill switch, and (6) persistence across restarts.

I’m going to be explicit about *what to change*, *where*, and *exact acceptance criteria*. Use the snippets as anchors—**do not generate entire files**.

---

# Phase 0 — Ground rules (do these before coding logic)

### 0.1 Switch core numeric types to `double`
**Why:** `float` is too lossy for returns, vol, and sizing.

**Claude Code tasks**
- Replace price/indicator/risk/metrics types from `float` to `double` in:
  - `Candle`, `Signal`, `StrategySignal`, `BacktestResult`, indicator outputs
- Leave volumes as `int64_t`.

**Snippet (example target)**
```cpp
struct Candle {
    std::chrono::sys_seconds ts; // replace string date
    double open, high, low, close;
    int64_t volume;
};
```

**Done when**
- Compiles, and all indicator computations build with `double` inputs/outputs.

---

# Phase 1 — Time + bars: make “bar-close” execution correct

### 1.1 Replace `Candle.date` string with `std::chrono` timestamp
**Why:** intraday trading needs timezone correctness and ordering.

**Claude Code tasks**
- Create a small utility header (don’t overbuild) like `TimeUtils.h`:
  - parse/format ISO8601
  - convert epoch seconds to `sys_seconds`
- Update Yahoo parsing to fill `ts` not `date`.
- Keep formatting only in report layer.

**Snippet**
```cpp
// TimeUtils.h (just signatures + tiny helpers)
using TimePoint = std::chrono::sys_seconds;

inline TimePoint fromUnixSeconds(int64_t s) {
    return TimePoint{std::chrono::seconds{s}};
}
```

**Done when**
- Candles are stored in ascending timestamp order and you can print human time in reports.

### 1.2 Add `MarketClock` (NYSE schedule aware, at least weekdays + hours)
**Why:** live loop must not trade outside regular session.

**Claude Code tasks**
- Add `MarketClock` with:
  - `bool isMarketOpen(TimePoint nowET)`
  - `TimePoint nextBarClose(TimePoint nowET, std::chrono::minutes barSize)`
- Keep it simple first: Mon–Fri, 09:30–16:00 America/New_York. (Later you can add holidays/early closes.)

**Pseudocode**
```
isMarketOpen(nowET):
  if weekend return false
  if time < 09:30 or time >= 16:00 return false
  return true

nextBarClose(nowET):
  if not open -> return next open + barSize aligned
  compute minutes since 09:30
  round up to next multiple of barSize
```

**Done when**
- Live loop can sleep until the next bar close and does not run orders after hours.

---

# Phase 2 — Split “historical research” from “live trading” data providers

### 2.1 Introduce provider interfaces (minimal, injectable)
**Why:** Yahoo is not appropriate as a live trading feed; you need broker/data-feed provider.

**Claude Code tasks**
- Add `IPriceProvider`, `INewsProvider`, `IFundamentalsProvider` interfaces.
- Update `processTicker()` to accept providers instead of calling Yahoo directly.

**Snippet (header-only interface excerpt)**
```cpp
class IPriceProvider {
public:
    virtual ~IPriceProvider() = default;

    // Historical bars for warmup
    virtual Result<std::vector<Candle>> getHistory(
        const std::string& symbol,
        std::chrono::minutes barSize,
        TimePoint start,
        TimePoint end
    ) = 0;

    // Latest completed bar (bar-close)
    virtual Result<Candle> getLastCompletedBar(
        const std::string& symbol,
        std::chrono::minutes barSize
    ) = 0;
};
```

**Done when**
- You can run the old analysis by using a `YahooPriceProvider` implementation, and later swap to a real feed.

### 2.2 Add a `BarSeries` container with “append only if newer”
**Why:** live bars arrive over time; you must avoid duplicates/out-of-order.

**Claude Code tasks**
- Create a small helper class:
  - stores vector of candles
  - `bool tryAppend(const Candle&)` checks `ts > last.ts`

**Snippet**
```cpp
class BarSeries {
public:
    bool tryAppend(const Candle& c) {
        if (!bars_.empty() && c.ts <= bars_.back().ts) return false;
        bars_.push_back(c);
        return true;
    }
    const std::vector<Candle>& bars() const { return bars_; }
private:
    std::vector<Candle> bars_;
};
```

**Done when**
- Live loop doesn’t accidentally “re-run” the same bar.

---

# Phase 3 — Replace `std::async` batching with a bounded thread pool

### 3.1 Add a fixed thread pool + work queue
**Why:** `std::async` is not a guarantee of bounded concurrency and complicates shutdown.

**Claude Code tasks**
- Implement `ThreadPool` with:
  - constructor `ThreadPool(size_t n)`
  - `submit(fn) -> future`
  - destructor joins threads
- Replace batching logic in `main.cpp` with pool usage.

**Snippet (usage only)**
```cpp
ThreadPool pool(/*nThreads=*/4);

auto fut = pool.submit([&] {
    return analyzeTicker(ticker, *priceProvider, *newsProvider, ml);
});
```

**Done when**
- Maximum concurrency is bounded and CPU usage is stable.

---

# Phase 4 — Live trading core: broker + order management + persistence

## 4.1 Create `IBroker` interface (REST-based implementation later)
**Why:** you need an execution layer with order status, partial fills, and account state.

**Claude Code tasks**
- Add an `IBroker` with:
  - `getAccount()`, `getPositions()`
  - `placeOrder(OrderRequest) -> Result<OrderId>`
  - `getOrderStatus(OrderId)`
  - `cancelOrder(OrderId)`

**Snippet**
```cpp
enum class OrderSide { Buy, Sell };
enum class OrderType { Market, Limit };

struct OrderRequest {
    std::string symbol;
    OrderSide side;
    OrderType type;
    int64_t qty;
    double limitPrice = 0.0;
    bool allowExtendedHours = false;
    std::string clientOrderId; // idempotency key
};

class IBroker {
public:
    virtual Result<std::string> placeOrder(const OrderRequest&) = 0;
    virtual Result<void> cancelOrder(const std::string& orderId) = 0;
    // ...positions/account/order status...
};
```

## 4.2 Add `OrderManager` with idempotency + retry/backoff
**Why:** network failures must not create duplicate orders.

**Claude Code tasks**
- `OrderManager::submitOnce(key, req)`:
  - if key exists in local persistent store -> do not re-place
  - else place, then persist mapping key→orderId
- Add exponential backoff retry for transient failures (`Timeout`, `RateLimit`, `Network`).

**Pseudocode**
```
submitOnce(key, req):
  if store.has(key): return store.get(key)
  for attempt in 1..max:
    res = broker.placeOrder(req with clientOrderId=key)
    if ok: store.put(key, orderId); return ok
    if non-retryable: return err
    sleep(backoff(attempt))
```

## 4.3 Add persistence (SQLite preferred, JSON acceptable initially)
**Why:** after restart, you must know what you already ordered.

**Claude Code tasks**
- Create `StateStore` with minimal functions:
  - `putOrderKey(key, orderId)`
  - `getOrderIdByKey(key)`
  - `putLastBarTs(symbol, ts)`
- Start with a simple SQLite table OR a JSON file with atomic write (write temp then rename).

**Done when**
- Restarting the process does not duplicate orders.

---

# Phase 5 — Convert signal generation into orders (live-safe)

### 5.1 Enforce “signal computed only on completed bars”
**Why:** don’t trade on a bar still forming.

**Claude Code tasks**
- In live loop, call `getLastCompletedBar()`, append to series, then run strategies on that series.
- Pass `idx = bars.size() - 1`.

### 5.2 Unify `Signal` and `StrategySignal` in the live path
**Why:** string actions are fragile; live must be type-safe.

**Claude Code tasks**
- For live execution, accept only `StrategySignal`.
- Keep legacy `Signal` only for reporting/back-compat if needed.

### 5.3 Add a `TradeDecision` layer (signal → order intent)
**Why:** you need risk checks, liquidity filters, and position awareness before sending an order.

**Claude Code tasks**
- Implement:
  - `DecisionEngine::evaluate(symbol, series, signal, account, positions) -> optional<OrderRequest>`
- Add checks:
  - do not trade if already in same-direction position (unless scaling is explicitly supported)
  - minimum liquidity: e.g., `avg20dDollarVolume > X`
  - max open positions, max sector exposure (optional but recommended)

**Pseudocode**
```
if signal.Buy and no position:
  qty = sizing(...)
  if qty <= 0: return null
  return market/limit buy request
if signal.Sell and have position:
  return market sell for position qty
else null
```

---

# Phase 6 — Risk management for live equities (must-have controls)

### 6.1 Replace Kelly sizing in live with risk-per-trade
**Why:** Kelly is unstable with noisy winRate estimates.

**Claude Code tasks**
- Add sizing method: risk a fixed % of equity per trade (e.g., 0.5%–1%).
- Use ATR-based stop distance from the strategy signal:
  - risk dollars = equity * riskPct
  - per-share risk = entry - stop
  - qty = floor(riskDollars / perShareRisk)

**Snippet**
```cpp
int64_t qtyFromRisk(
    double equity,
    double riskPct,
    double entry,
    double stop
) {
    const double riskDollars = equity * riskPct;
    const double perShare = std::max(0.01, entry - stop);
    return static_cast<int64_t>(std::floor(riskDollars / perShare));
}
```

### 6.2 Add kill switches
**Claude Code tasks**
- Implement `RiskGuard` with:
  - daily max loss (realized + unrealized) threshold
  - max consecutive errors threshold
  - max order rate per symbol
- If tripped: stop placing new orders; optionally flatten positions (config flag).

**Done when**
- You can force a kill switch in config and verify no new orders are sent.

---

# Phase 7 — Sentiment/ML: make it thread-safe and latency-bounded

### 7.1 Sentiment inference must be async + time-budgeted
**Why:** llama.cpp inference can stall; live trading must not miss bar-close.

**Claude Code tasks**
- Create `SentimentService`:
  - takes headlines
  - returns `Result<double>` with a timeout (e.g., 200–500 ms)
  - if timeout: return cached last sentiment or 0

**Pseudocode**
```
analyzeWithTimeout(headlines, timeout):
  submit to a single-thread inference queue
  wait timeout
  if not ready -> return cached/neutral
```

### 7.2 Freeze ML weights in live trading
**Why:** online training introduces nondeterminism and can overfit intraday noise.

**Claude Code tasks**
- Add `MLPredictor::predict(features)` const-safe.
- Disable `train()` in live mode (guard with config).
- If you still want adaptation: train only end-of-day on stored data, then reload.

**Done when**
- No shared mutable ML state is accessed across ticker tasks.

---

# Phase 8 — Live main loop (bar-close scheduler)

### 8.1 Add `--mode=live` execution path (don’t rewrite the existing main)
**Claude Code tasks**
- Add a mode flag parsing (very small).
- Implement `runLive()` that:
  1) initializes providers + broker + store
  2) loads initial history for each symbol (warmup)
  3) loops forever:
     - sleep until next bar close
     - for each symbol: fetch last completed bar, append, generate signal, evaluate decision, place order if any
     - record last processed bar ts in store

**Pseudocode**
```
runLive():
  load symbols
  for each symbol: series = provider.getHistory(...)
  while true:
    now = clock.nowET()
    if !marketOpen: sleep(60s); continue
    wakeAt = clock.nextBarClose(now, barSize)
    sleepUntil(wakeAt + smallDelay)
    parallel for symbols:
      bar = price.getLastCompletedBar(symbol)
      if series.tryAppend(bar):
        signal = strategy.generateSignal(series.bars(), series.size()-1)
        orderReqOpt = decisionEngine.evaluate(...)
        if orderReqOpt: orderManager.submitOnce(orderKey(symbol, bar.ts), req)
```

**Important detail for Claude Code**
- Add a small delay after bar close (e.g., 1–3 seconds) to ensure data-feed finalizes the bar.

**Done when**
- Bot trades exactly once per symbol per bar (no duplicates), and sleeps between bars.

---

# Phase 9 — Minimal test harness (do not skip)

### 9.1 Add a `FakeBroker` + `FakePriceProvider` for integration tests
**Claude Code tasks**
- Implement in-memory fakes:
  - FakePriceProvider returns a deterministic bar sequence
  - FakeBroker records placed orders and “fills” them at requested price
- Write 3 integration tests (using whatever test framework you already have or a minimal assert-based harness):
  1) “no duplicate order on restart” (state store works)
  2) “does not trade outside market hours”
  3) “risk sizing never exceeds max position cap”

**Snippet (assert-style)**
```cpp
assert(fakeBroker.orders().size() == 1);
assert(fakeBroker.orders()[0].symbol == "AAPL");
```

---

## Implementation order (Claude Code execution checklist)

1) `double` migration (core structs + indicators)
2) `Candle.ts` + `TimeUtils` + report formatting adjustments
3) `MarketClock` + bar-close scheduler helpers
4) Provider interfaces + adapt current Yahoo implementation to `IPriceProvider`
5) ThreadPool + replace `std::async` batching
6) `IBroker` + `OrderManager` + `StateStore`
7) `DecisionEngine` + `RiskGuard` + ATR risk sizing
8) SentimentService timeout + ML frozen weights in live
9) `runLive()` mode + integration tests with fakes

---

## Questions Claude Code should NOT guess (pick defaults now)
Reply with your choices (one line each), and I’ll tailor the order request/clock/provider scaffolding:

1) Bar size: `5m | 15m | 60m | 1d`
2) Broker: `Alpaca | Interactive Brokers (IBKR) | other`
3) Data feed: `broker feed | Polygon | IEX | other`
4) Order style: `market` only vs `limit at mid/last` vs bracket (entry + stop)

----------------------------------------------------------------------------------------------------
## Claude Code Plan: “Live Signals” (60‑minute bars, equities, free data, limit-order ideas)
You are **not executing trades**. You are running a **bar-close intraday signal service** that outputs: buy/sell/hold, entry/exit targets, reasoning, and a **limit order suggestion**.

Below are concrete implementation steps with pseudocode + code snippets. Do **not** generate full files; apply focused edits.

---

# 1) Add a new runtime mode: `live_signals` (don’t break backtesting)

### 1.1 Config keys (environment variables)
Update `Config` to read:
- `MODE` default `"backtest"` (existing behavior)
- `BAR_SIZE_MINUTES` default `"60"`
- `LIVE_OUTPUT_CSV` default `"live_signals.csv"`
- `YAHOO_RANGE_DAYS` default `"60"` (for 60m bars warmup)
- `LIVE_INCLUDE_NEWS` default `"1"`
- `LIVE_INCLUDE_SENTIMENT` default `"1"`

**Acceptance**
- Running with `MODE=live_signals` enters the live loop; otherwise old behavior remains.

**Snippet (Config getters, minimal)**
```cpp
int getInt(const std::string& key, int def);
bool getBool(const std::string& key, bool def);
std::string getString(const std::string& key, const std::string& def);
```

---

# 2) Time handling: represent bars with timestamps, not strings

### 2.1 Change Candle to carry a timestamp
Replace `std::string date` with `std::chrono::sys_seconds ts`.

**Edit**
```cpp
struct Candle {
    std::chrono::sys_seconds ts;
    double open, high, low, close;
    int64_t volume;
};
```

### 2.2 Provide a formatting helper for output only
Add a tiny helper (new file or existing utils):
```cpp
std::string formatTimeET(std::chrono::sys_seconds ts);
```

**Acceptance**
- All indicator and strategy code compiles with `Candle.ts` and no longer depends on parsing strings.

---

# 3) Market clock + scheduler (NYSE regular session, 60m bars)

You do **not** want to invent your own bar boundaries if the feed already gives bars. But you **do** need:
- “Only run during regular session”
- “Wake shortly after each bar close”
- “Avoid outputting signals for incomplete bars”

### 3.1 Add `MarketClock` (simple weekday + hours)
Implement:
- `isRegularSessionOpen(nowET)`
- `sleepUntilNextBarClose(nowET, barSizeMinutes)`

**Pseudocode**
```
if weekend: sleep 30 minutes; continue
if time < 09:30: sleep until 09:30
if time >= 16:00: sleep until next weekday 09:30
else:
  nextClose = ceil_to_next_multiple(nowET, barSize, anchor=09:30)
  sleep_until(nextClose + 2 seconds)  // allow data to settle
```

**Acceptance**
- Process sleeps overnight and does not spam Yahoo outside market hours.

---

# 4) Intraday price provider (Yahoo 60m) + “completed bar” logic

Yahoo’s chart endpoint supports intraday intervals but limited range. For 60m, use e.g. `range=60d`.

### 4.1 Update MarketData to support interval + rangeDays
Add a new method (don’t delete the daily one; keep backtesting intact):
```cpp
Result<std::vector<Candle>> fetchCandlesYahoo(
    const std::string& symbol,
    const std::string& interval,   // "60m"
    const std::string& range       // "60d"
);
```

**Yahoo URL**
- `https://query1.finance.yahoo.com/v8/finance/chart/{symbol}?range=60d&interval=60m&includePrePost=false`

### 4.2 Parse carefully: skip nulls, ensure ordering
Yahoo arrays sometimes contain `null`. Skip any bar where open/high/low/close/volume is missing.

**Snippet (parsing pattern)**
```cpp
auto& res = json["chart"]["result"][0];
auto tsArr = res["timestamp"];
auto quote = res["indicators"]["quote"][0];

for (size_t i = 0; i < tsArr.size(); ++i) {
    if (quote["open"][i].is_null() || quote["close"][i].is_null() ||
        quote["high"][i].is_null() || quote["low"][i].is_null() ||
        quote["volume"][i].is_null()) {
        continue;
    }

    Candle c;
    c.ts = std::chrono::sys_seconds{std::chrono::seconds{
        tsArr[i].get<int64_t>()
    }};
    c.open = quote["open"][i].get<double>();
    c.high = quote["high"][i].get<double>();
    c.low  = quote["low"][i].get<double>();
    c.close = quote["close"][i].get<double>();
    c.volume = quote["volume"][i].get<int64_t>();
    candles.push_back(c);
}
std::sort(candles.begin(), candles.end(),
          [](auto& a, auto& b){ return a.ts < b.ts; });
```

### 4.3 “Last completed bar” selection
When you fetch recent bars (e.g., `range=5d`), the most recent candle may be **in-progress**. Define:
- bar is complete if `c.ts + barSize <= nowET - settleDelay`

**Snippet**
```cpp
std::optional<Candle> lastCompleted(
    const std::vector<Candle>& bars,
    std::chrono::sys_seconds now,
    std::chrono::minutes barSize
) {
    for (int i = (int)bars.size() - 1; i >= 0; --i) {
        if (bars[i].ts + barSize <= now - std::chrono::seconds(2)) {
            return bars[i];
        }
    }
    return std::nullopt;
}
```

**Acceptance**
- Live loop never generates a signal for a candle still forming.

---

# 5) Maintain rolling bar series per symbol (no duplicates)

### 5.1 Add `BarSeries` with append-if-newer
Use the minimal class:

```cpp
class BarSeries {
public:
    bool tryAppend(const Candle& c) {
        if (!bars_.empty() && c.ts <= bars_.back().ts) return false;
        bars_.push_back(c);
        return true;
    }
    const std::vector<Candle>& bars() const { return bars_; }
    size_t size() const { return bars_.size(); }
private:
    std::vector<Candle> bars_;
};
```

**Acceptance**
- Re-fetching recent Yahoo bars does not cause reprocessing of the same timestamp.

---

# 6) Concurrency: remove `std::async` for live loop (bounded thread pool)

You only need modest parallelism (e.g., 4 workers), but it must be bounded.

### 6.1 Implement/introduce `ThreadPool`
Do not over-engineer—just:
- fixed worker threads
- queue of packaged tasks
- `submit()` returns `future`

**Acceptance**
- At most `N` symbols processed concurrently each bar.

---

# 7) Sentiment/news in live mode (cache + time budget)

Your goal is **signals on bar close**. Don’t let sentiment inference block the bar.

### 7.1 Cache headlines and sentiment with TTL
- News TTL: 15 minutes
- Sentiment TTL: 30 minutes
- If sentiment not ready: use cached value or 0.0

**Pseudocode**
```
if LIVE_INCLUDE_NEWS:
  headlines = cache.getOrCompute(symbol, ttl=15m, fetchNews)
if LIVE_INCLUDE_SENTIMENT:
  sentiment = cache.getOrCompute(symbol, ttl=30m, analyze(headlines with timeout 300ms))
else sentiment = 0
```

### 7.2 Add sentiment timeout wrapper
Do it with a single-thread executor for llama.cpp to avoid concurrent inference contention.

**Snippet (pattern)**
```cpp
auto fut = sentimentExecutor.submit([&]{
    return analyzer.analyze(headlines); // returns double [-1,1]
});
if (fut.wait_for(std::chrono::milliseconds(300)) == std::future_status::ready) {
    sentiment = fut.get();
} else {
    sentiment = cachedOrNeutral;
}
```

**Acceptance**
- A slow model does not delay signal output past bar close.

---

# 8) Limit-order “idea” output (since you’re not trading)

You want the system to output limit order suggestions. Add a new struct **only for reporting**.

### 8.1 Add `OrderIdea` struct
```cpp
enum class OrderIdeaSide { Buy, Sell };

struct OrderIdea {
    OrderIdeaSide side;
    double limitPrice;     // suggested limit
    double stopLossPrice;  // suggested stop
    double takeProfitPrice; // suggested TP
    std::string timeInForce; // "DAY" (default)
};
```

### 8.2 Generate limit price using ATR + support/resistance
Do this in one place: e.g., `TradingStrategy::buildOrderIdea(...)` so strategies don’t duplicate logic.

**Rules (equities, 60m):**
- For **Buy**: place limit slightly below last close (improve fill) but not crazy:
  - `limit = close - 0.25 * ATR`
  - If support exists: `limit = min(limit, support * 1.005)` (0.5% above support)
- Stop-loss:
  - If strategy set stop: use it
  - else `stop = limit - 1.5 * ATR`
- Take-profit:
  - If strategy set TP: use it
  - else `tp = limit + 2.0 * ATR` or next resistance if available
- Round to $0.01.

**Snippet**
```cpp
double roundToCent(double x) {
    return std::round(x * 100.0) / 100.0;
}

OrderIdea buildBuyIdea(double close, double atr, double support,
                       double stopFromSignal, double tpFromSignal,
                       double resistance) {
    double limit = close - 0.25 * atr;
    if (support > 0) limit = std::min(limit, support * 1.005);
    limit = roundToCent(limit);

    double stop = stopFromSignal > 0 ? stopFromSignal : (limit - 1.5 * atr);
    double tp = tpFromSignal > 0 ? tpFromSignal : (limit + 2.0 * atr);
    if (resistance > 0) tp = std::min(tp, resistance); // don’t promise beyond resistance

    return OrderIdea{OrderIdeaSide::Buy, roundToCent(limit),
                     roundToCent(stop), roundToCent(tp), "DAY"};
}
```

**Sell signals (equities, no shorting)**
- Treat Sell as **“exit-only”**:
  - If user is not tracking positions, output “SELL (exit-only)” with a limit near resistance:
    - `limit = close + 0.10 * ATR` (or `resistance * 0.995`)
- Do **not** output “open short” limit orders unless you explicitly add an `ALLOW_SHORT=1` mode.

**Acceptance**
- Every BUY/SELL output includes a limit price and stop/take-profit suggestion.

---

# 9) Live loop: hourly bar-close signal runner

### 9.1 Create `runLiveSignals()`
Keep it separate from `main`’s existing analysis/backtest.

**High-level pseudocode**
```
runLiveSignals():
  barSize = 60 minutes
  load tickers.csv

  init ThreadPool(4)
  init price provider (Yahoo)
  init news manager + sentiment analyzer (optional)
  init per-symbol BarSeries map

  // Warmup history per symbol: range=60d interval=60m
  for each symbol:
    history = fetchCandlesYahoo(symbol, "60m", "60d")
    keep only regular-session bars (optional filter)
    store in BarSeries

  while true:
    nowET = clock.nowET()
    MarketClock.sleepUntilNextBarClose(nowET, 60m)

    // after wake: process each symbol (bounded parallel)
    for each symbol in tickers:
      submit task:
        recent = fetchCandlesYahoo(symbol, "60m", "5d")
        c = lastCompleted(recent, nowET, 60m)
        if c and series.tryAppend(*c):
           compute news/sentiment (cached)
           generate signal (existing TradingStrategy/strategies)
           compute ATR/support/resistance
           build OrderIdea (limit order suggestion)
           append row to live_signals.csv + log to console
```

### 9.2 CSV output row (add these columns)
At minimum:
- timestamp (ET)
- symbol
- lastClose
- regime
- action (BUY/SELL/HOLD)
- strength/confidence
- limitPrice, stopLoss, takeProfit
- targets (stringified)
- sentiment
- reason

**Acceptance**
- Every hour during session, you get one row per symbol **at most** (no duplicates), and HOLD rows include prospective buy/sell (existing fields) plus optional limit suggestions if you want.

---

# 10) Yahoo reliability + “free fallback” (optional but recommended)

Yahoo can throttle/break. Add a fallback provider that’s still free-tier:
- **Alpha Vantage** intraday 60min (requires free API key; still “free”)
- If no key, fallback is disabled.

### 10.1 Implement `CompositePriceProvider`
**Pseudocode**
```
try provider A (Yahoo)
if error Network/RateLimit/Parse:
  try provider B (AlphaVantage if key set)
```

**Acceptance**
- If Yahoo fails temporarily, system continues for symbols where fallback works.

---

# 11) IMPORTANT edits to reduce wrong/inconsistent signals (do these while you’re there)

### 11.1 Fix regime detection warmup
In `detectMarketRegime`, require `>= 200` when using SMA-200. For intraday 60m, you likely won’t have 200 bars initially; degrade:
- if < 200: use SMA-50 only and return “Bull/Bear/Sideways” based on price vs SMA-50 + vol threshold.

**Acceptance**
- No out-of-range reads; regime works even with ~60–150 bars.

### 11.2 Freeze ML state across threads
If `MLPredictor` mutates state, do NOT share one instance across worker tasks.
- In live mode: only call `predict()` on an immutable model OR clone per task.

**Acceptance**
- Thread sanitizer (if you run it) shows no races in live mode.

---

## Concrete “Claude Code” execution order
1) Candle timestamp migration (`Candle.ts`, parsing, formatting for reports)
2) Yahoo 60m fetch + last-completed-bar detection + null skipping
3) `BarSeries` per symbol
4) `MarketClock` scheduler (RTH only) + `runLiveSignals()`
5) ThreadPool integration for per-bar symbol processing
6) Sentiment/news TTL caches + timeout wrapper
7) Add `OrderIdea` + deterministic limit-price rules (ATR/support/resistance)
8) Fix regime warmups + ML thread-safety
9) CSV output appender for `live_signals.csv`

If you want, I can also provide a **minimal “regular-session bar filter”** (drop pre/post bars) based on `ts` converted to America/New_York time; but you can keep `includePrePost=false` and skip it initially.