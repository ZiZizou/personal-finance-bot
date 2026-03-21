#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Telegram Listener for Trading Bot
Handles commands via Telegram to control the trading bot

Commands:
/add SYMBOL - Add ticker to watchlist
/remove SYMBOL - Remove ticker
/analyze SYMBOL - Get last analysis for a symbol
/run - Run trading bot now
/list - List all tickers
/help - Show help
"""

import os
import io
import sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')
import sys
import time
import subprocess
import requests
import csv
import threading
import html
from pathlib import Path

# Import market scraper
try:
    from market_scraper import format_sentiment_message, format_news_message, get_market_sentiment, get_stock_news
    SCRAPER_AVAILABLE = True
except ImportError:
    SCRAPER_AVAILABLE = False
    print("Warning: market_scraper.py not available")

# Load from .env file
def load_env():
    env_file = Path(__file__).parent / ".env"
    if env_file.exists():
        with open(env_file) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    if "=" in line:
                        key, value = line.split("=", 1)
                        os.environ[key.strip()] = value.strip()

load_env()


# ============== Python API Helper Functions ==============

def get_signals_from_api(symbols):
    """
    Fetch signals from Python API for given symbols.

    Args:
        symbols: List of ticker symbols

    Returns:
        List of signal dicts or None if API unavailable
    """
    if not symbols:
        return None

    try:
        url = f"{PYTHON_API_URL}/api/batch/signals"
        params = {"symbols": ",".join(symbols)}
        response = requests.get(url, params=params, timeout=PYTHON_API_TIMEOUT)

        if response.status_code == 200:
            data = response.json()
            signals = []
            for sig in data.get("signals", []):
                signals.append({
                    "symbol": sig.get("ticker", ""),
                    "action": sig.get("signal", "hold").upper(),
                    "confidence": sig.get("confidence", 0.5),
                    "strength": sig.get("confidence", 0.5) * 100,
                    "price": sig.get("price"),
                    "timestamp": sig.get("timestamp", ""),
                    "source": sig.get("source", "python_api")
                })
            return signals
        else:
            print(f"API returned status {response.status_code}")
            return None

    except requests.exceptions.ConnectionError:
        print("Python API not available - falling back to CSV")
        return None
    except requests.exceptions.Timeout:
        print("Python API timeout - falling back to CSV")
        return None
    except Exception as e:
        print(f"Error calling Python API: {e}")
        return None


def get_sentiment_from_api(symbol):
    """
    Fetch sentiment from Python API for a symbol.

    Args:
        symbol: Ticker symbol

    Returns:
        Sentiment dict or None if API unavailable
    """
    try:
        url = f"{PYTHON_API_URL}/api/sentiment/{symbol}"
        response = requests.get(url, timeout=PYTHON_API_TIMEOUT)

        if response.status_code == 200:
            return response.json()
        return None

    except Exception:
        return None


# ============== Configuration ==============
TOKEN = os.environ.get("STOCK_TELEGRAM_BOT_TOKEN", "")
CHAT_ID = os.environ.get("STOCK_TELEGRAM_CHAT_ID", "")
TICKERS_FILE = "tickers.csv"
LIVE_SIGNALS_FILE = "live_signals.csv"
TRADING_BOT_PATH = "C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_cpp\\trading_bot_tech_analysis.exe"

# Python API configuration
PYTHON_API_URL = os.environ.get("PYTHON_API_URL", "http://localhost:8000")
PYTHON_API_TIMEOUT = 10  # seconds

# Python service paths
PYTHON_SERVICE_DIR = "C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_Python"
PYTHON_VENV_DIR = "C:\\Users\\Atharva\\Documents\\Trading_super\\venv"
PYTHON_VENV_PYTHON = os.path.join(PYTHON_VENV_DIR, "Scripts", "python.exe")
PYTHON_SERVICE_SCRIPT = os.path.join(PYTHON_SERVICE_DIR, "scripts", "run_service.py")

# ONNX model configuration
ONNX_MODEL_PATH = "C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_cpp\\models\\stock_predictor.onnx"

LAST_UPDATE_ID = 0
_python_api_process = None

# Lock file to ensure only one instance runs
LOCK_FILE = os.path.join(os.path.dirname(__file__), ".telegram_listener.lock")


def check_single_instance():
    """Ensure only one instance of telegram_listener runs at a time.
    Uses Windows-compatible file locking via msvcrt.
    """
    import msvcrt

    try:
        # Try to open the lock file in append mode
        lock_file = open(LOCK_FILE, 'a')

        # Try to lock the file exclusively without blocking
        try:
            msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
            # Successfully acquired lock
            lock_file.seek(0)
            lock_file.truncate()
            lock_file.write(str(os.getpid()))
            lock_file.flush()
            return True, lock_file
        except (IOError, OSError):
            # Another instance holds the lock
            lock_file.close()
            return False, None

    except (IOError, OSError):
        return False, None


def is_python_api_running():
    """Check if Python API is running."""
    try:
        response = requests.get(f"{PYTHON_API_URL}/health", timeout=2)
        return response.status_code == 200
    except Exception:
        return False


def wait_for_python_api(timeout=30):
    """Wait for Python API to become available."""
    import time
    start = time.time()
    while time.time() - start < timeout:
        if is_python_api_running():
            return True
        time.sleep(1)
    return False


def is_port_in_use(port):
    """Check if a port is already in use."""
    import socket
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(1)
            s.connect(("localhost", port))
            return True
    except (socket.error, socket.timeout):
        return False


_api_lock = threading.Lock()

def start_python_api_service():
    """Start the Python API service in background if not already running."""
    global _python_api_process

    with _api_lock:
        # Check if already running in this process
        if _python_api_process is not None and _python_api_process.poll() is None:
            if is_python_api_running():
                return True
            print("Python API process exists but not responding yet...")
            return wait_for_python_api(timeout=10)

        # Check if already running (any process)
        if is_python_api_running():
            print("Python API already running")
            return True

        # Check if port is in use (orphaned process)
        if is_port_in_use(8000):
            print("Port 8000 is in use - attempting to kill orphaned Python API process...")
            try:
                import subprocess
                # Try to kill any process using port 8000
                subprocess.run(
                    ["powershell", "-Command",
                     "Get-NetTCPConnection -LocalPort 8000 | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }"],
                    capture_output=True,
                    timeout=10
                )
                import time
                time.sleep(2)
            except Exception as e:
                print(f"Could not kill orphaned process: {e}")

        print("Starting Python API service...")
        try:
            import subprocess

            # Verify Python executable exists
            if not os.path.exists(PYTHON_VENV_PYTHON):
                print(f"Error: Python executable not found at {PYTHON_VENV_PYTHON}")
                return False

            # Verify service script exists
            if not os.path.exists(PYTHON_SERVICE_SCRIPT):
                print(f"Error: Service script not found at {PYTHON_SERVICE_SCRIPT}")
                return False

            # Prepare environment - CLEAR UV variables to prevent shim recursion
            env = os.environ.copy()
            uv_vars = [k for k in env.keys() if k.startswith("UV_")]
            for k in uv_vars:
                del env[k]
            
            # Also clear PYTHONPATH to avoid cross-contamination
            if "PYTHONPATH" in env:
                del env["PYTHONPATH"]

            # Start Python service in background using venv Python
            # Use CREATE_NO_WINDOW flag on Windows to avoid console window
            startupinfo = None
            creation_flags = 0
            if os.name == 'nt':
                startupinfo = subprocess.STARTUPINFO()
                startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startupinfo.wShowWindow = subprocess.SW_HIDE
                # Use CREATE_NEW_PROCESS_GROUP and DETACHED_PROCESS to decouple from parent
                creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP | 0x00000008  # 0x8 is DETACHED_PROCESS

            cmd = [PYTHON_VENV_PYTHON, PYTHON_SERVICE_SCRIPT, "--port", "8000"]
            print(f"Spawning command: {' '.join(cmd)}")
            print(f"Working directory: {PYTHON_SERVICE_DIR}")

            _python_api_process = subprocess.Popen(
                cmd,
                cwd=PYTHON_SERVICE_DIR,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                startupinfo=startupinfo,
                creationflags=creation_flags,
                close_fds=True
            )

            print(f"Python API process started with PID: {_python_api_process.pid}")

            # Wait for it to be ready
            if wait_for_python_api(timeout=30):
                print("Python API service started successfully")
                return True
            else:
                # Don't use communicate() on a long-running server - it will wait forever
                # Just check if the process is still running
                if _python_api_process.poll() is not None:
                    # Process exited - capture output
                    try:
                        stdout, stderr = _python_api_process.communicate(timeout=2)
                        if stdout:
                            print(f"Python API stdout: {stdout.decode('utf-8', errors='ignore')[:500]}")
                        if stderr:
                            print(f"Python API stderr: {stderr.decode('utf-8', errors='ignore')[:500]}")
                    except:
                        pass
                    print("Warning: Python API process exited unexpectedly - will retry on /run command")
                else:
                    # Process still running but health check failed - might be starting up slowly
                    print("Warning: Python API is slow to start but process is running - will retry on /run command")
                return False

        except Exception as e:
            print(f"Error starting Python API: {e}")
            import traceback
            traceback.print_exc()
            return False


def check_onnx_model():
    """Check if ONNX model exists and is accessible."""
    return os.path.exists(ONNX_MODEL_PATH)


def get_cpp_env():
    """Get environment variables for C++ trading bot."""
    import json

    env = os.environ.copy()

    # Configure C++ to use ONNX model
    env["USE_ONNX_MODEL"] = "true"
    env["ONNX_MODEL_PATH"] = ONNX_MODEL_PATH

    # Configure Python API URL
    env["PYTHON_API_URL"] = PYTHON_API_URL

    return env

def send_message(text):
    """Send a message to Telegram"""
    if not text:
        return
        
    url = f"https://api.telegram.org/bot{TOKEN}/sendMessage"
    data = {
        "chat_id": CHAT_ID,
        "text": text,
        "parse_mode": "HTML"
    }
    try:
        response = requests.post(url, data=data, timeout=10)
        res_data = response.json()
        if not res_data.get("ok"):
            print(f"Telegram API Error: {res_data.get('description', 'Unknown error')}")
            # If HTML parsing failed, try sending as plain text
            if "can't parse entities" in res_data.get('description', ''):
                print("Retrying as plain text...")
                data.pop("parse_mode")
                requests.post(url, data=data, timeout=10)
    except Exception as e:
        print(f"Error sending message: {e}")

def get_updates():
    """Get updates from Telegram"""
    global LAST_UPDATE_ID
    url = f"https://api.telegram.org/bot{TOKEN}/getUpdates"
    # Short timeout for faster loops
    params = {"timeout": 10}
    if LAST_UPDATE_ID > 0:
        params["offset"] = LAST_UPDATE_ID + 1

    try:
        response = requests.get(url, params=params, timeout=35)
        data = response.json()
        if data.get("ok"):
            return data.get("result", [])
    except Exception as e:
        print(f"Error getting updates: {e}")
    return []

def load_tickers():
    """Load tickers from CSV file"""
    tickers = []
    try:
        with open(TICKERS_FILE, 'r') as f:
            reader = csv.reader(f)
            next(reader)  # Skip header
            for row in reader:
                if row:
                    tickers.append((row[0], row[1] if len(row) > 1 else "stock"))
    except FileNotFoundError:
        pass
    return tickers

def save_tickers(tickers):
    """Save tickers to CSV file"""
    with open(TICKERS_FILE, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["symbol", "type"])
        for sym, typ in tickers:
            writer.writerow([sym, typ])

def add_ticker(symbol):
    """Add a ticker to the watchlist"""
    symbol = symbol.upper().strip()
    tickers = load_tickers()

    # Check if already exists
    for sym, typ in tickers:
        if sym == symbol:
            return f"⚠️ {symbol} is already in the watchlist!"

    tickers.append((symbol, "stock"))
    save_tickers(tickers)
    return f"✅ Added {symbol} to watchlist"

def remove_ticker(symbol):
    """Remove a ticker from the watchlist"""
    symbol = symbol.upper().strip()
    tickers = load_tickers()

    new_tickers = [(s, t) for s, t in tickers if s != symbol]
    if len(new_tickers) == len(tickers):
        return f"⚠️ {symbol} not found in watchlist"

    save_tickers(new_tickers)
    return f"✅ Removed {symbol} from watchlist"

def list_tickers():
    """List all tickers"""
    tickers = load_tickers()
    if not tickers:
        return "📋 Watchlist is empty"

    count = len(tickers)
    ticker_list = "\n".join([f"• {s} ({t})" for s, t in tickers[:20]])
    if count > 20:
        ticker_list += f"\n... and {count - 20} more"
    return f"📋 Watchlist ({count} tickers):\n{ticker_list}"

def get_all_signals():
    """Get latest signals from live_signals.csv - returns only the most recent entry per symbol"""
    # Use dict to keep only the latest entry for each symbol
    latest_signals = {}
    try:
        with open(LIVE_SIGNALS_FILE, 'r') as f:
            reader = csv.reader(f)
            next(reader)  # Skip header

            for row in reader:
                if len(row) >= 13:
                    # Skip invalid rows (empty symbol or VIX/XEQT)
                    if not row[1] or row[1] in ["VIX", "XEQT"]:
                        continue
                    if row[1].startswith(","):
                        continue

                    try:
                        symbol = row[1]
                        timestamp = row[0]

                        signal = {
                            'timestamp': timestamp,
                            'symbol': symbol,
                            'last_close': float(row[2]) if row[2] else 0,
                            'regime': row[3],
                            'action': row[4],
                            'strength': float(row[5]) if row[5] else 0,
                            'confidence': float(row[6]) if row[6] else 0,
                            'stop_loss': float(row[8]) if row[8] else 0,
                            'take_profit': float(row[9]) if row[9] else 0,
                            'reason': row[12] if len(row) > 12 else ""
                        }

                        # Only keep the latest entry for each symbol (by timestamp)
                        if symbol not in latest_signals:
                            latest_signals[symbol] = signal
                        else:
                            # Compare timestamps - format is "YYYY-MM-DD HH:MM:SS"
                            # String comparison works for chronological order
                            if timestamp > latest_signals[symbol]['timestamp']:
                                latest_signals[symbol] = signal
                    except (ValueError, IndexError):
                        continue
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"Error reading signals: {e}")

    return list(latest_signals.values())

def analyze_signals_status(include_market_data=True):
    """Analyze all signals and return status summary.

    Uses hybrid approach: tries Python API first, falls back to live_signals.csv.
    """
    # First try to get signals from Python API
    tickers = load_tickers()
    ticker_symbols = [t[0] for t in tickers]

    signals = None
    signals_from_api = False

    if ticker_symbols:
        api_signals = get_signals_from_api(ticker_symbols)
        if api_signals:
            signals = api_signals
            signals_from_api = True
            print(f"Using {len(signals)} signals from Python API")

    # Fall back to CSV if API unavailable
    if signals is None:
        signals = get_all_signals()
        print(f"Using {len(signals)} signals from live_signals.csv")

    # Normalize signal format for consistent access
    # API signals have: symbol, action, confidence, strength, price
    # CSV signals have: symbol, action, strength, confidence, last_close, reason
    normalized_signals = []
    for s in signals:
        normalized = {
            'symbol': s.get('symbol', ''),
            'action': s.get('action', 'HOLD').upper(),
            'strength': s.get('strength', 0) * 100 if s.get('strength', 0) <= 1 else s.get('strength', 0),
            'confidence': s.get('confidence', 0.5) * 100 if s.get('confidence', 0.5) <= 1 else s.get('confidence', 0.5),
            'last_close': s.get('last_close', s.get('price', 0)),
            'reason': s.get('reason', s.get('source', '')),
            'timestamp': s.get('timestamp', ''),
        }
        normalized_signals.append(normalized)
    signals = normalized_signals

    status = ""

    # Add source indicator
    if signals_from_api:
        status += "🤖 Source: Python API (ONNX Models)\n\n"
    else:
        status += "⚙️ Source: C++ Live Signals\n\n"

    # Include market sentiment from Investopedia
    if include_market_data and SCRAPER_AVAILABLE:
        try:
            sentiment = get_market_sentiment()
            emoji = "🟡"
            if sentiment["sentiment"] == "BULLISH":
                emoji = "🟢"
            elif sentiment["sentiment"] == "BEARISH":
                emoji = "🔴"

            status += f"📈 Market: {emoji} {sentiment['sentiment']} ({sentiment['sentiment_score']}/100)\n\n"
        except Exception as e:
            print(f"Error getting sentiment: {e}")

    if not signals:
        return status + "⚠️ No signals available"

    buy_count = sum(1 for s in signals if s['action'] == 'BUY')
    sell_count = sum(1 for s in signals if s['action'] == 'SELL')
    hold_count = sum(1 for s in signals if s['action'] == 'HOLD')
    total = len(signals)

    # Check if all are HOLD
    all_hold = (hold_count == total)

    status += f"📊 Signals Summary:\n"
    status += f"• BUY: {buy_count}\n"
    status += f"• SELL: {sell_count}\n"
    status += f"• HOLD: {hold_count}\n"
    status += f"• Total: {total}\n"

    if all_hold:
        status += "\n🔔 ALL SIGNALS ARE HOLD\n"

    # Find signals close to flipping (strength near 50)
    # Close to BUY: strength < 60 (could flip to BUY)
    # Close to SELL: strength > 40 (could flip to SELL)
    # We look for HOLD signals with strength in the 40-60 range

    near_buy = [s for s in signals if s['action'] == 'HOLD' and s['strength'] < 60 and s['strength'] > 30]
    near_sell = [s for s in signals if s['action'] == 'HOLD' and s['strength'] > 40 and s['strength'] < 70]

    # Sort by proximity to flipping
    near_buy.sort(key=lambda x: 60 - x['strength'])  # Closest to 60 first
    near_sell.sort(key=lambda x: x['strength'] - 40)  # Closest to 40 first

    # Show actual BUY signals first (these are real signals from C++)
    if buy_count > 0:
        buy_signals = [s for s in signals if s['action'] == 'BUY']
        buy_signals.sort(key=lambda x: x['strength'], reverse=True)
        status += "\n🟢 ACTUAL BUY Signals:\n"
        for s in buy_signals[:15]:
            price_str = f"${s['last_close']:.2f}" if s.get('last_close') else "N/A"
            status += f"  • {s['symbol']}: strength={s['strength']:.1f}, conf={s['confidence']:.0f}% @ {price_str}\n"

    # Show actual SELL signals (these are real signals from C++)
    if sell_count > 0:
        sell_signals = [s for s in signals if s['action'] == 'SELL']
        sell_signals.sort(key=lambda x: x['strength'], reverse=True)
        status += "\n🔴 ACTUAL SELL Signals:\n"
        for s in sell_signals[:15]:
            price_str = f"${s['last_close']:.2f}" if s.get('last_close') else "N/A"
            status += f"  • {s['symbol']}: strength={s['strength']:.1f}, conf={s['confidence']:.0f}% @ {price_str}\n"

    # Then show near signals (these are HOLD signals that might flip)
    if near_buy:
        status += "\n🟡 Near BUY (HOLD signals with strength < 60):\n"
        for s in near_buy[:10]:
            price_str = f"${s['last_close']:.2f}" if s.get('last_close') else "N/A"
            status += f"  • {s['symbol']}: {s['strength']:.1f} @ {price_str}\n"

    if near_sell:
        status += "\n🟠 Near SELL (HOLD signals with strength > 40):\n"
        for s in near_sell[:10]:
            price_str = f"${s['last_close']:.2f}" if s.get('last_close') else "N/A"
            status += f"  • {s['symbol']}: {s['strength']:.1f} @ {price_str}\n"

    if not near_buy and not near_sell:
        status += "\nNo HOLD signals are close to flipping at this time."

    # Always show all HOLD signals so users can see what tickers are being tracked
    if hold_count > 0:
        status += "\n⚪ HOLD Signals (no clear direction):\n"
        # Sort by strength (highest first)
        hold_signals = [s for s in signals if s['action'] == 'HOLD']
        hold_signals.sort(key=lambda x: x['strength'], reverse=True)
        for s in hold_signals[:15]:  # Top 15 by strength
            price_str = f"${s['last_close']:.2f}" if s.get('last_close') else "N/A"
            status += f"  • {s['symbol']}: strength={s['strength']:.1f} @ {price_str}\n"

    # Include latest stock news from Finviz
    if include_market_data and SCRAPER_AVAILABLE:
        try:
            news_data = get_stock_news()
            news_items = news_data.get("news", [])[:10]
            if news_items:
                status += "\n📰 Latest News:\n"
                for item in news_items:
                    headline = html.escape(item['headline'])
                    symbol = f" [{item['symbol']}]" if item.get('symbol') else ""
                    status += f"  • {headline}{symbol}\n"
        except Exception as e:
            print(f"Error getting news: {e}")

    return status

def get_last_analysis(symbol):
    """Get last analysis for a symbol from live_signals.csv"""
    symbol = symbol.upper().strip()

    try:
        with open(LIVE_SIGNALS_FILE, 'r') as f:
            reader = csv.reader(f)
            next(reader)  # Skip header

            last_entry = None
            for row in reader:
                if len(row) > 4 and row[1] == symbol:
                    last_entry = row

            if not last_entry:
                return None

            # Parse: timestamp,symbol,lastClose,regime,action,strength,confidence,limitPrice,stopLoss,takeProfit,targets,sentiment,reason
            timestamp = last_entry[0]
            sym = last_entry[1]
            last_close = last_entry[2]
            action = last_entry[4]
            strength = last_entry[5]
            confidence = last_entry[6]
            stop_loss = last_entry[8]
            take_profit = last_entry[9]
            reason = last_entry[12] if len(last_entry) > 12 else ""
            reason = html.escape(reason)

            # Format response
            response = f"<b>{action}</b> @ ${last_close}\n"
            response += f"Strength: {strength}\n"

            if action in ["BUY", "SELL"]:
                response += f"Confidence: {confidence}%\n"
                response += f"Stop Loss: ${stop_loss}\n"
                response += f"Take Profit: ${take_profit}\n"

            response += f"Reason: {reason}\n"
            response += f"Time: {timestamp}"

            return response
    except FileNotFoundError:
        return None
    except Exception as e:
        print(f"Error reading analysis: {e}")
        return None

def analyze_symbol(symbol):
    """Analyze a symbol - return last analysis or add to watchlist"""
    symbol = symbol.upper().strip()

    # Check if in watchlist
    tickers = load_tickers()
    in_watchlist = any(s == symbol for s, t in tickers)

    if in_watchlist:
        analysis = get_last_analysis(symbol)
        if analysis:
            return f"📊 Analysis for {symbol}:\n\n{analysis}"
        else:
            return f"⚠️ No analysis data for {symbol}. Run /run to generate."
    else:
        # Add to watchlist
        result = add_ticker(symbol)
        return f"{result}\n\nAnalysis will be available after running /run"

def get_fundamentals(symbol):
    """Fetch and format fundamental data from Yahoo Finance"""
    import json
    import re

    symbol = symbol.upper().strip()

    try:
        # Fetch from Yahoo Finance quote endpoint
        url = f"https://query1.finance.yahoo.com/v7/finance/quote?symbols={symbol}"
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        }

        response = requests.get(url, headers=headers, timeout=10)
        data = response.json()

        if "quoteResponse" not in data or not data["quoteResponse"].get("result"):
            return f"⚠️ No data found for {symbol}"

        q = data["quoteResponse"]["result"][0]

        # Extract key fields
        def safe_val(key, default=0):
            return q.get(key, default)

        def fmt_num(val, prefix="", suffix=""):
            if val is None or val == 0:
                return "N/A"
            if abs(val) >= 1e12:
                return f"{prefix}{val/1e12:.2f}T{suffix}"
            elif abs(val) >= 1e9:
                return f"{prefix}{val/1e9:.2f}B{suffix}"
            elif abs(val) >= 1e6:
                return f"{prefix}{val/1e6:.2f}M{suffix}"
            elif abs(val) >= 1e3:
                return f"{prefix}{val/1e3:.2f}K{suffix}"
            return f"{prefix}{val:.2f}{suffix}"

        def fmt_pct(val):
            if val is None or val == 0:
                return "N/A"
            return f"{val*100:.2f}%"

        def fmt_ratio(val):
            if val is None or val == 0:
                return "N/A"
            return f"{val:.2f}"

        # Build response
        response = f"📈 <b>Fundamentals for {symbol}</b>\n"
        response += f"<i>{q.get('shortName', symbol)}</i>\n\n"

        # Price & Market Cap
        response += f"<b>Price:</b> ${safe_val('regularMarketPrice', 0):.2f}\n"
        response += f"<b>Market Cap:</b> {fmt_num(safe_val('marketCap'))}\n"
        response += f"<b>52W Range:</b> ${safe_val('fiftyTwoWeekLow', 0):.2f} - ${safe_val('fiftyTwoWeekHigh', 0):.2f}\n\n"

        # Valuation
        response += f"<b>Valuation</b>\n"
        pe = safe_val('trailingPE')
        fpe = safe_val('forwardPE')
        peg = safe_val('pegRatio')
        pb = safe_val('priceToBook')
        ps = safe_val('priceToSalesTrailing12Months')

        response += f"P/E (TTM): {fmt_ratio(pe)}\n"
        response += f"P/E (Fwd): {fmt_ratio(fpe)}\n"
        response += f"PEG: {fmt_ratio(peg)}\n"
        response += f"P/B: {fmt_ratio(pb)}\n"
        response += f"P/S: {fmt_ratio(ps)}\n\n"

        # Growth
        response += f"<b>Growth</b>\n"
        eg = safe_val('earningsGrowth')
        rg = safe_val('revenueGrowth')
        eps = safe_val('epsTrailingTwelveMonths')

        response += f"EPS (TTM): ${fmt_ratio(eps)}\n"
        response += f"Earnings Growth: {fmt_pct(eg)}\n"
        response += f"Revenue Growth: {fmt_pct(rg)}\n\n"

        # Margins
        response += f"<b>Margins</b>\n"
        gm = safe_val('grossMargins')
        om = safe_val('operatingMargins')
        pm = safe_val('profitMargins')

        response += f"Gross: {fmt_pct(gm)}\n"
        response += f"Operating: {fmt_pct(om)}\n"
        response += f"Profit: {fmt_pct(pm)}\n\n"

        # Financial Health
        response += f"<b>Financial Health</b>\n"
        de = safe_val('debtToEquity')
        cr = safe_val('currentRatio')
        qr = safe_val('quickRatio')
        tc = safe_val('totalCash')
        td = safe_val('totalDebt')
        fcf = safe_val('freeCashflow')

        response += f"Debt/Equity: {fmt_ratio(de)}\n"
        response += f"Current Ratio: {fmt_ratio(cr)}\n"
        response += f"Quick Ratio: {fmt_ratio(qr)}\n"
        response += f"Cash: {fmt_num(tc)}\n"
        response += f"Debt: {fmt_num(td)}\n"
        response += f"FCF: {fmt_num(fcf)}\n\n"

        # Dividends
        response += f"<b>Dividends</b>\n"
        dy = safe_val('dividendYield')
        dr = safe_val('dividendRate')
        pr = safe_val('payoutRatio')

        response += f"Yield: {fmt_pct(dy)}\n"
        response += f"Rate: ${fmt_ratio(dr)}\n"
        response += f"Payout: {fmt_pct(pr)}\n\n"

        # Analyst & Sentiment
        response += f"<b>Analyst Sentiment</b>\n"
        ar = q.get('averageRating', 0)
        if ar > 0:
            rating_map = {1: "Strong Buy", 2: "Buy", 3: "Hold", 4: "Sell", 5: "Strong Sell"}
            ar_str = rating_map.get(int(ar + 0.5), f"{ar:.1f}")
            response += f"Rating: {ar} ({ar_str})\n"
        else:
            response += f"Rating: N/A\n"

        spf = safe_val('shortPercentFloat')
        sr = safe_val('shortRatio')
        hpi = safe_val('heldPercentInstitutions')

        response += f"Short % Float: {fmt_pct(spf)}\n"
        response += f"Short Ratio: {fmt_ratio(sr)}\n"
        response += f"Inst. Ownership: {fmt_pct(hpi)}\n\n"

        # Company Info
        sector = q.get('sector', 'N/A')
        industry = q.get('industry', 'N/A')
        response += f"<b>Company</b>\n"
        response += f"Sector: {sector}\n"
        response += f"Industry: {industry}\n"

        # Beta
        beta = safe_val('beta')
        if beta > 0:
            response += f"Beta: {beta:.2f}\n"

        return response

    except Exception as e:
        return f"⚠️ Error fetching fundamentals: {str(e)}"

def run_trading_bot_async():
    """Run the trading bot in background - called from thread"""
    try:
        # Step 1: Start Python API service if not running
        send_message("🔄 Starting Python API service...")
        if not start_python_api_service():
            send_message("⚠️ Failed to start Python API, continuing anyway...")

        # Step 2: Check ONNX model availability
        onnx_available = check_onnx_model()
        if onnx_available:
            send_message("✅ ONNX model found - ML predictions enabled")
        else:
            send_message("⚠️ ONNX model not found - using fallback signals")

        # Step 3: Run C++ trading bot with ONNX configuration
        env = get_cpp_env()
        send_message("🚀 Running Trading Bot...")

        result = subprocess.run(
            [TRADING_BOT_PATH, "scheduled"],
            capture_output=True,
            timeout=300,
            cwd="C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_cpp",
            env=env
        )

        if result.returncode == 0:
            send_message("✅ Trading Bot completed successfully!")
            # Automatically send signals summary after successful run
            send_message("📊 Fetching latest signals...")
            signals_msg = analyze_signals_status(include_market_data=True)
            send_message(signals_msg)
        else:
            send_message(f"⚠️ Trading Bot finished with errors (code: {result.returncode})")
            if result.stderr:
                send_message(f"Error details: {result.stderr.decode('utf-8', errors='ignore')[:500]}")
    except subprocess.TimeoutExpired:
        send_message("⚠️ Trading Bot timed out (>5 min)")
    except Exception as e:
        send_message(f"⚠️ Error running bot: {str(e)}")

def run_trading_bot():
    """Run the trading bot in a background thread"""
    send_message("🔄 Starting Trading Bot in background...")
    thread = threading.Thread(target=run_trading_bot_async)
    thread.daemon = True
    thread.start()
    return "🚀 Trading Bot started in background!"


def scrape_news_async():
    """Scrape news in background - called from thread"""
    try:
        # Ensure Python API is running
        if not is_python_api_running():
            send_message("🔄 Starting Python API service...")
            if not start_python_api_service():
                return "⚠️ Failed to start Python API"

        # Call the sentiment API which will fetch fresh news if needed
        send_message("📰 Scraping latest news...")

        # Get sentiment for a few popular tickers to trigger news fetch
        tickers = load_tickers()
        ticker_symbols = [t[0] for t in tickers[:5]] if tickers else ["AAPL", "MSFT", "GOOGL"]

        for symbol in ticker_symbols:
            try:
                url = f"{PYTHON_API_URL}/api/sentiment/{symbol}"
                response = requests.get(url, timeout=10)
                if response.status_code == 200:
                    data = response.json()
                    send_message(f"  {symbol}: score={data.get('sentiment_score', 'N/A'):.2f}, articles={data.get('article_count', 0)}")
            except Exception:
                pass

        send_message("✅ News scraping completed!")
        return "📰 News scraping completed!"

    except Exception as e:
        send_message(f"⚠️ Error scraping news: {str(e)}")
        return f"⚠️ Error: {str(e)}"


def train_models_async():
    """Train ONNX models in background - called from thread"""
    try:
        # Ensure Python API is running
        if not is_python_api_running():
            send_message("🔄 Starting Python API service...")
            if not start_python_api_service():
                return "⚠️ Failed to start Python API"

        send_message("🧠 Training ONNX models...")
        send_message("This may take several minutes...")

        # Run the training script
        train_script = os.path.join(PYTHON_SERVICE_DIR, "scripts", "train_model.py")
        result = subprocess.run(
            [PYTHON_VENV_PYTHON, train_script, "--ticker", "AAPL", "--start", "2022-01-01"],
            capture_output=True,
            timeout=300,
            cwd=PYTHON_SERVICE_DIR
        )

        if result.returncode == 0:
            # Copy model to C++ directory
            src_model = os.path.join(PYTHON_SERVICE_DIR, "models", "AAPL_ridge.onnx")
            dst_model = os.path.join("C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_cpp\\models", "stock_predictor.onnx")

            if os.path.exists(src_model):
                import shutil
                os.makedirs(os.path.dirname(dst_model), exist_ok=True)
                shutil.copy2(src_model, dst_model)
                send_message(f"✅ Model trained and copied to C++: {dst_model}")
            else:
                send_message("✅ Model training completed (check Python logs for path)")

            # Create reload signal
            send_message("🔔 Models will be used on next /run")
            return "✅ ONNX model training completed!"
        else:
            error_msg = result.stderr.decode('utf-8', errors='ignore')[:500] if result.stderr else "Unknown error"
            send_message(f"⚠️ Training failed: {error_msg}")
            return f"⚠️ Training failed"

    except subprocess.TimeoutExpired:
        send_message("⚠️ Training timed out (>5 min)")
        return "⚠️ Training timed out"
    except Exception as e:
        send_message(f"⚠️ Error training models: {str(e)}")
        return f"⚠️ Error: {str(e)}"


def scrape_news():
    """Scrape news in a background thread"""
    send_message("📰 Starting news scraping in background...")
    thread = threading.Thread(target=scrape_news_async)
    thread.daemon = True
    thread.start()
    return "📰 News scraping started in background!"


def train_models():
    """Train models in a background thread"""
    send_message("🧠 Starting model training in background...")
    thread = threading.Thread(target=train_models_async)
    thread.daemon = True
    thread.start()
    return "🧠 Model training started in background!"


# ============== Manual Ticker Override Functions ==============

MANUAL_TICKERS_FILE = "C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_Python\\data\\manual_tickers.txt"
SELECTED_TICKERS_FILE = "C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_Python\\models\\selected_tickers.txt"


def get_manual_tickers():
    """Get manually selected tickers from override file."""
    if os.path.exists(MANUAL_TICKERS_FILE):
        try:
            with open(MANUAL_TICKERS_FILE, 'r') as f:
                content = f.read().strip()
                if content:
                    return [t.strip().upper() for t in content.split(',') if t.strip()]
        except Exception:
            pass
    return []


def save_manual_tickers(tickers):
    """Save manually selected tickers to override file."""
    try:
        os.makedirs(os.path.dirname(MANUAL_TICKERS_FILE), exist_ok=True)
        with open(MANUAL_TICKERS_FILE, 'w') as f:
            f.write(",".join(tickers))
        return True
    except Exception as e:
        print(f"Error saving manual tickers: {e}")
        return False


def get_selected_tickers():
    """Get currently selected tickers (from intelligent selection)."""
    if os.path.exists(SELECTED_TICKERS_FILE):
        try:
            with open(SELECTED_TICKERS_FILE, 'r') as f:
                content = f.read().strip()
                if content:
                    return [t.strip().upper() for t in content.split(',') if t.strip()]
        except Exception:
            pass
    return []


def show_ticker_selection():
    """Show current ticker selection status."""
    manual = get_manual_tickers()
    selected = get_selected_tickers()

    if manual:
        response = "🔧 <b>Manual Ticker Override</b>\n"
        response += f"Manually selected: {', '.join(manual)}\n"
        response += f"\nThese tickers will be trained instead of intelligent selection."
    elif selected:
        response = "📊 <b>Intelligent Ticker Selection</b>\n"
        response += f"Currently selected: {', '.join(selected)}\n"
        response += f"\nUse /swap OLD NEW to replace a ticker."
    else:
        response = "📋 <b>No Ticker Selection</b>\n"
        response += "Run /run or /train to generate selection."

    return response


def swap_ticker(old_ticker, new_ticker):
    """
    Swap a ticker in the manual selection.

    Args:
        old_ticker: Ticker to remove (use 'AUTO' to clear manual selection)
        new_ticker: Ticker to add (use 'NONE' to just remove)

    Returns:
        Status message
    """
    old_ticker = old_ticker.upper().strip()
    new_ticker = new_ticker.upper().strip() if new_ticker else None

    # Check if old ticker exists in current selection
    manual = get_manual_tickers()
    selected = get_selected_tickers()
    current = manual if manual else selected

    if not current:
        return "⚠️ No tickers selected yet. Run /run or /train first."

    # Handle special cases
    if old_ticker == "AUTO":
        # Clear manual selection - revert to intelligent selection
        if os.path.exists(MANUAL_TICKERS_FILE):
            os.remove(MANUAL_TICKERS_FILE)
        return "✅ Cleared manual ticker selection. Will use intelligent selection on next run."

    if old_ticker not in current and old_ticker not in [t.upper() for t in (manual if manual else selected)]:
        available = ', '.join(current)
        return f"⚠️ {old_ticker} not in current selection.\nCurrent: {available}"

    if old_ticker in current:
        # Perform the swap
        if manual:
            # Modify manual list
            manual = [t for t in manual if t != old_ticker]
            if new_ticker and new_ticker != "NONE":
                manual.append(new_ticker)
            save_manual_tickers(manual)
            status = f"✅ Replaced {old_ticker} with {new_ticker}"
        else:
            # Create new manual list from selected
            manual = [t for t in selected if t != old_ticker]
            if new_ticker and new_ticker != "NONE":
                manual.append(new_ticker)
            save_manual_tickers(manual)
            status = f"✅ Replaced {old_ticker} with {new_ticker}"

        final_list = get_manual_tickers()
        return f"{status}\nManual selection: {', '.join(final_list)}"

    return "⚠️ Swap failed. Try /selected to see current tickers."


def add_ticker_to_selection(ticker):
    """Add a ticker to the manual selection."""
    ticker = ticker.upper().strip()

    manual = get_manual_tickers()
    selected = get_selected_tickers()
    current = manual if manual else selected

    if not current:
        return f"⚠️ No tickers selected yet. Run /run or /train first.\nYou can use /set TICKER1,TICKER2,... to set manual selection."

    if ticker in current:
        return f"ℹ️ {ticker} is already in selection"

    if manual:
        manual.append(ticker)
        save_manual_tickers(manual)
    else:
        manual = selected + [ticker]
        save_manual_tickers(manual)

    return f"✅ Added {ticker}\nManual selection: {', '.join(get_manual_tickers())}"


def remove_ticker_from_selection(ticker):
    """Remove a ticker from the manual selection."""
    ticker = ticker.upper().strip()

    manual = get_manual_tickers()
    if not manual:
        return "⚠️ No manual tickers set. Use /swap to replace a ticker."

    if ticker not in manual:
        return f"⚠️ {ticker} not in manual selection"

    manual = [t for t in manual if t != ticker]
    if manual:
        save_manual_tickers(manual)
        return f"✅ Removed {ticker}\nManual selection: {', '.join(manual)}"
    else:
        # If empty, remove the file
        if os.path.exists(MANUAL_TICKERS_FILE):
            os.remove(MANUAL_TICKERS_FILE)
        return f"✅ Removed {ticker}. Manual selection cleared (will use intelligent)."


def set_manual_tickers(ticker_list):
    """Set the manual ticker selection directly."""
    tickers = [t.strip().upper() for t in ticker_list.split(',') if t.strip()]

    if not tickers:
        return "⚠️ No tickers provided. Usage: /set TICKER1,TICKER2,..."

    if len(tickers) > 7:
        return f"⚠️ Maximum 7 tickers allowed, got {len(tickers)}"

    save_manual_tickers(tickers)
    return f"✅ Set manual tickers: {', '.join(tickers)}\nThese will be trained on next /run or /train"


def get_pairs_status():
    """Get status of predefined pairs for pairs trading"""
    # Predefined pairs to monitor
    pairs = [
        ("SPY", "IVV", "S&P 500 ETFs"),
        ("QQQ", "TQQQ", "NASDAQ ETFs"),
        ("GLD", "IAU", "Gold ETFs"),
        ("EEM", "VWO", "Emerging Markets"),
    ]

    status = "📊 <b>Pairs Trading Status</b>\n\n"

    try:
        import numpy as np

        for sym1, sym2, desc in pairs:
            try:
                # Fetch data for both symbols
                p1 = yf.download(sym1, period="60d", interval="1d", progress=False)
                p2 = yf.download(sym2, period="60d", interval="1d", progress=False)

                if len(p1) < 30 or len(p2) < 30:
                    status += f"• {sym1}/{sym2}: ⚠️ Insufficient data\n"
                    continue

                # Calculate spread and correlation
                closes1 = p1['Close'].dropna().values.flatten()
                closes2 = p2['Close'].dropna().values.flatten()

                min_len = min(len(closes1), len(closes2))
                closes1 = closes1[-min_len:]
                closes2 = closes2[-min_len:]

                # Simple correlation
                corr = np.corrcoef(closes1, closes2)[0, 1]

                # Calculate spread ratio and Z-score
                ratios = closes1 / closes2
                ratio = ratios[-1]
                ratio_ma20 = np.mean(ratios[-20:])
                ratio_std = np.std(ratios[-20:])
                z_score = (ratio - ratio_ma20) / ratio_std if ratio_std > 0 else 0

                # Determine signal
                if abs(z_score) > 2.0:
                    signal = "🔴 SHORT" if z_score > 0 else "🟢 LONG"
                elif abs(z_score) > 1.0:
                    signal = "⚠️ NEAR"
                else:
                    signal = "✅ WAIT"

                status += f"<b>{sym1}/{sym2}</b> ({desc})\n"
                status += f"  Ratio: {ratio:.4f}, Z: {z_score:.2f}\n"
                status += f"  Corr: {corr:.3f} | {signal}\n\n"

            except Exception as e:
                status += f"• {sym1}/{sym2}: Error\n"

        status += "\n💡 <i>Z > 2: Short spread | Z < -2: Long spread</i>"

    except ImportError:
        status += "⚠️ yfinance not installed\n"
        status += "Install with: pip install yfinance"

    return status

def process_command(text):
    """Process a command"""
    text = text.strip()

    if text == "/help":
        return """📝 Commands:
/add SYMBOL - Add ticker to watchlist
/remove SYMBOL - Remove ticker
/analyze SYMBOL - Get last analysis
/fundamentals SYMBOL - Get fundamental data
/run - Run trading bot now (starts Python API + C++ bot)
/scrape - Scrape latest news (populates sentiment DB)
/train - Train ONNX models on current data
/list - Show all tickers
/signals - Show all signal statuses
/sentiment - Show market sentiment
/news - Show latest stock news
/pairs - Show pairs trading opportunities

🔧 Ticker Selection (for training):
/selected - Show current ticker selection
/swap OLD NEW - Replace a ticker in selection
/set T1,T2,... - Set manual tickers (max 7)
/addticker T - Add ticker to selection
/removeticker T - Remove ticker from selection
/auto - Clear manual selection, use intelligent selection
/help - Show this help"""

    elif text == "/sentiment":
        if not SCRAPER_AVAILABLE:
            return "⚠️ Market scraper not available"
        try:
            return format_sentiment_message()
        except Exception as e:
            return f"⚠️ Error getting sentiment: {str(e)}"

    elif text == "/news":
        if not SCRAPER_AVAILABLE:
            return "⚠️ Market scraper not available"
        try:
            return format_news_message()
        except Exception as e:
            return f"⚠️ Error getting news: {str(e)}"

    elif text == "/pairs":
        try:
            return get_pairs_status()
        except Exception as e:
            return f"⚠️ Error getting pairs: {str(e)}"
            return f"⚠️ Error getting news: {str(e)}"

    elif text == "/list":
        return list_tickers()

    elif text == "/scrape":
        return scrape_news_async()

    elif text == "/train":
        return train_models_async()

    elif text == "/selected":
        return show_ticker_selection()

    elif text.startswith("/swap ") or text.startswith("/replace "):
        parts = text.split()
        if len(parts) >= 3:
            old_ticker = parts[1]
            new_ticker = parts[2]
            return swap_ticker(old_ticker, new_ticker)
        elif len(parts) == 2:
            # Just one ticker - assume they want to remove
            return swap_ticker(parts[1], "NONE")
        return "Usage: /swap OLD_TICKER NEW_TICKER\nExample: /swap NVDA AMD"

    elif text.startswith("/set "):
        ticker_list = text[5:].strip()
        return set_manual_tickers(ticker_list)

    elif text.startswith("/addticker ") or text.startswith("/addtick "):
        ticker = text.split(maxsplit=1)[1].strip()
        return add_ticker_to_selection(ticker)

    elif text.startswith("/removeticker ") or text.startswith("/removetick "):
        ticker = text.split(maxsplit=1)[1].strip()
        return remove_ticker_from_selection(ticker)

    elif text == "/auto":
        return swap_ticker("AUTO", None)

    elif text == "/run":
        return run_trading_bot()

    elif text.startswith("/add "):
        symbol = text[5:].strip()
        return add_ticker(symbol)

    elif text.startswith("/remove "):
        symbol = text[8:].strip()
        return remove_ticker(symbol)

    elif text.startswith("/analyze "):
        symbol = text[9:].strip()
        return analyze_symbol(symbol)

    elif text.startswith("/fundamentals ") or text.startswith("/fund "):
        symbol = text.split(maxsplit=1)[1].strip() if " " in text else ""
        if not symbol:
            return "Usage: /fundamentals SYMBOL\nExample: /fundamentals AAPL"
        return get_fundamentals(symbol)

    elif text == "/signals" or text == "/status":
        return analyze_signals_status()

    else:
        return f"Unknown command: {text}\nUse /help for available commands"

def main():
    global LAST_UPDATE_ID

    # ===== DETAILED LOGGING FOR DEBUGGING =====
    print(f"\n--- Process Startup ---")
    print(f"PID: {os.getpid()}")
    print(f"Parent PID: {os.getppid() if hasattr(os, 'getppid') else 'N/A'}")
    print(f"Executable: {sys.executable}")
    print(f"Arguments: {sys.argv}")
    print(f"Working Dir: {os.getcwd()}")
    print(f"-----------------------\n")

    # ===== Ensure only one instance runs =====
    is_single, lock_file = check_single_instance()
    if not is_single:
        print(f"ERROR: Another telegram_listener instance is already running (PID {os.getpid()} detected conflict)!")
        print("If you believe this is an error, delete the lock file:")
        print(f"  {LOCK_FILE}")
        sys.exit(1)

    if not TOKEN or not CHAT_ID:
        print("Error: STOCK_TELEGRAM_BOT_TOKEN and STOCK_TELEGRAM_CHAT_ID must be set")
        sys.exit(1)

    print(f"Telegram Listener started (PID: {os.getpid()})...")
    print(f"Token: {TOKEN[:5]}...")
    print(f"Chat ID: {CHAT_ID}")

    # ===== STARTUP: Start Python API and train initial models =====
    print("\n" + "="*50)
    print("Initializing Python API and training models...")
    print("="*50)

    # Step 1: Start Python API service
    print("Starting Python API service...")
    if start_python_api_service():
        print("Python API started successfully")

        # Step 2: Wait for API to be ready, then trigger training
        print("Triggering initial model training...")
        time.sleep(3)  # Give API a moment to fully initialize

        try:
            # Load tickers from C++ tickers.csv
            tickers = load_tickers()
            ticker_symbols = [t[0] for t in tickers]

            if not ticker_symbols:
                # Fallback to common tickers if none in CSV
                ticker_symbols = ["AAPL", "MSFT", "GOOGL", "AMZN", "META", "NVDA", "TSLA"]
                print(f"No tickers in CSV, using defaults: {ticker_symbols}")

            # Select up to 7 tickers for training
            selected_tickers = ticker_symbols[:7]
            print(f"Training models for: {selected_tickers}")

            # Call API to select tickers and train models
            url = f"{PYTHON_API_URL}/api/models/select-tickers"
            response = requests.post(url, json={"tickers": selected_tickers}, timeout=120)

            if response.status_code == 200:
                result = response.json()
                trained = result.get("trained", [])
                print(f"Training completed for: {trained}")

                # Copy model to C++ directory for ONNX inference
                if trained:
                    import shutil
                    src_model = os.path.join(PYTHON_SERVICE_DIR, "models", "active", f"{trained[0]}.onnx")
                    dst_model = os.path.join("C:\\Users\\Atharva\\Documents\\Trading_super\\Trading_cpp\\models", "stock_predictor.onnx")

                    if os.path.exists(src_model):
                        os.makedirs(os.path.dirname(dst_model), exist_ok=True)
                        shutil.copy2(src_model, dst_model)
                        print(f"Model copied to: {dst_model}")
            else:
                print(f"Training API returned: {response.status_code}")
        except Exception as e:
            print(f"Could not trigger training: {e}")
    else:
        print("Warning: Python API failed to start - will try on /run")

    print("="*50)
    print("Startup complete!")
    print("="*50 + "\n")

    scraper_status = "✅" if SCRAPER_AVAILABLE else "⚠️"
    send_message(f"✅ Telegram Listener started!\n\nCommands:\n/add SYMBOL - Add ticker\n/remove SYMBOL - Remove ticker\n/analyze SYMBOL - Get last analysis\n/fundamentals SYMBOL - Get fundamental data\n/run - Run trading bot now\n/list - Show all tickers\n/signals - Show all signal statuses\n/sentiment - Market sentiment {scraper_status}\n/news - Stock news {scraper_status}\n/pairs - Pairs trading opportunities\n/help - Show help\n\nPolling every 2.5 minutes!")

    while True:
        try:
            print(f"[{time.strftime('%H:%M:%S')}] Polling for updates...")
            updates = get_updates()
            print(f"Found {len(updates)} updates")

            for update in updates:
                if "update_id" in update:
                    LAST_UPDATE_ID = update["update_id"]

                    if "message" in update:
                        message = update["message"]
                        if "text" in message and "chat" in message:
                            chat_id = str(message["chat"]["id"])

                            if chat_id == CHAT_ID:
                                text = message["text"]
                                print(f"Received: {text}")

                                response = process_command(text)
                                send_message(response)

            # Poll every 5 minutes (300 seconds)
            time.sleep(150)

        except KeyboardInterrupt:
            print("\nListener stopped")
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(5)

if __name__ == "__main__":
    main()
