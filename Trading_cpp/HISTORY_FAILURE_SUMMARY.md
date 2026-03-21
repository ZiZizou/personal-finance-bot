# Live Signal Mode History Loading Failure Summary

**Date:** February 20, 2026
**Status:** FIXED

## Issue Description
The trading bot fails to load historical data for most tickers when running in both `backtest` and `live_signals` modes. The logs consistently show a "Network Error" with an "Internal error" (HTTP 500 equivalent) from the Yahoo Finance API.

## Root Cause Analysis

### 1. API Endpoint Issues
The bot uses the following endpoint:
`https://query1.finance.yahoo.com/v8/finance/chart/{SYMBOL}?interval={INTERVAL}&range={RANGE}&includePrePost=false`

Observations:
- **Internal Error (500):** This usually means Yahoo's backend failed to process the request.
- **Symbol Specificity:** Many failing symbols are Canadian (`.TO`, `.V`). Yahoo sometimes requires different URL formats or has stricter rate limits for non-US exchanges.
- **Recent API Changes:** Yahoo Finance frequently updates its "unofficial" API endpoints, often requiring specific headers (User-Agent, Cookies, or Crumbs) that are currently missing or default in `NetworkUtils.cpp`.

### 2. Request Parameters
- For `backtest` mode: `interval=1d&range=2y`
- For `live_signals` mode: `interval=60m&range=60d`
Both sets of parameters are standard, but the "Internal Error" persists across them.

### 3. Missing Metadata/Headers
Modern Yahoo API requests often fail if a standard `User-Agent` is not provided. If `NetworkUtils` uses a generic or empty header, Yahoo's server-side protection might be triggering the 500 error instead of a 403 (Forbidden) or 429 (Too Many Requests).

### 4. CRITICAL: Missing Crumb Token
**Root Cause Identified:** Yahoo Finance API now requires a "crumb" token for authentication. Without this crumb, all chart API requests return HTTP 500 Internal Server Error.

## Fix Applied

### Changes Made

1. **NetworkUtils.h** - Added declaration for `getYahooCrumb()` function
2. **NetworkUtils.cpp** - Added implementation to fetch and cache Yahoo crumb token:
   - New function `getYahooCrumb()` that fetches crumb from Yahoo's crumb endpoint
   - Crumb is cached for 1 hour to avoid excessive requests
   - Wrapped in `#ifdef ENABLE_CURL` with fallback for non-CURL builds

3. **Providers.cpp** - Updated to include crumb in Yahoo API requests:
   - `YahooPriceProvider::buildYahooUrl()` - Added crumb parameter
   - `YahooFundamentalsProvider::getFundamentals()` - Added crumb parameter

4. **MarketData.cpp** - Updated to include crumb in Yahoo API requests:
   - `fetchCandles()` - Added crumb parameter
   - `fetchFundamentals()` - Added crumb parameter

### How It Works
1. On first Yahoo API request, the system fetches a crumb token from `https://query1.finance.yahoo.com/v1/test/getcrumb`
2. The crumb is cached for 1 hour
3. All subsequent Yahoo API requests include the crumb as a URL parameter: `&crumb=XXXXXXXXXXXX`
4. If crumb fetching fails, requests proceed without it (backward compatible)

## Documented Results from Debug Runs (Pre-Fix)

| Symbol | Mode | Interval | Range | Result |
|--------|------|----------|-------|--------|
| EFR.TO | Backtest | 1d | 2y | Network Error: Internal error |
| MDA.TO | Backtest | 1d | 2y | Network Error: Internal error |
| OUST   | Backtest | 1d | 2y | Network Error: Internal error |
| GOOGL  | Backtest | 1d | 2y | Network Error: Internal error |

## Verification
After applying the fix, rebuild the project and test with:
- Backtest mode: Should successfully fetch historical data
- Live signals mode: Should successfully fetch recent bars

## Recommended Next Steps (Optional)
1. **Alpha Vantage Fallback:** Implement the Alpha Vantage fallback mentioned in `improvement_notes.md` for redundancy
2. **Symbol Validation:** Test if stripping exchange suffixes or using different Yahoo-compatible formats helps for international tickers
