"""
Data handling module for trading research.
Supports Yahoo Finance, CSV files, and Qlib data format.
"""

import os
from datetime import datetime, timedelta
from typing import Optional, List, Union
import warnings

import numpy as np
import pandas as pd
import yfinance as yf
from tqdm import tqdm


def fetch_yahoo_data(
    tickers: Union[str, List[str]],
    start_date: str,
    end_date: Optional[str] = None,
    interval: str = "1d",
    progress: bool = True
) -> pd.DataFrame:
    """
    Fetch historical data from Yahoo Finance.

    Args:
        tickers: Stock ticker(s) - single string or list
        start_date: Start date in YYYY-MM-DD format
        end_date: End date in YYYY-MM-DD format (default: today)
        interval: Data interval (1d, 1h, 5m, etc.)
        progress: Show progress bar

    Returns:
        DataFrame with MultiIndex (ticker, date) or single ticker columns
    """
    if isinstance(tickers, str):
        tickers = [tickers]

    if end_date is None:
        end_date = datetime.now().strftime("%Y-%m-%d")

    all_data = []

    if progress:
        tickers = tqdm(tickers, desc="Fetching data")

    for ticker in tickers:
        try:
            data = yf.download(
                ticker,
                start=start_date,
                end=end_date,
                interval=interval,
                progress=False,
                auto_adjust=True,
                threads=True
            )

            if not data.empty:
                data.columns = data.columns.get_level_values(0)
                data['ticker'] = ticker
                all_data.append(data)

        except Exception as e:
            warnings.warn(f"Failed to fetch {ticker}: {e}")
            continue

    if not all_data:
        raise ValueError("No data fetched for any ticker")

    # Combine all tickers
    result = pd.concat(all_data)
    result.index.name = 'date'

    return result


def load_csv_directory(
    directory: str,
    pattern: str = "*.csv",
    date_column: str = "Date",
    index_col: Optional[str] = None
) -> pd.DataFrame:
    """
    Load all CSV files from a directory and combine them.

    Args:
        directory: Path to CSV files
        pattern: Glob pattern for files
        date_column: Name of date column to parse
        index_col: Column to use as index

    Returns:
        Combined DataFrame with ticker in MultiIndex
    """
    import glob

    files = glob.glob(os.path.join(directory, pattern))

    if not files:
        raise ValueError(f"No files found matching {pattern} in {directory}")

    all_data = []

    for file in files:
        ticker = os.path.splitext(os.path.basename(file))[0]

        try:
            df = pd.read_csv(file, parse_dates=[date_column])
            df = df.set_index(date_column)

            if index_col and index_col in df.columns:
                df = df.set_index(index_col)

            df['ticker'] = ticker
            all_data.append(df)

        except Exception as e:
            warnings.warn(f"Failed to load {file}: {e}")
            continue

    return pd.concat(all_data)


def resample_ohlcv(
    df: pd.DataFrame,
    freq: str = "1W",
    agg: Optional[dict] = None
) -> pd.DataFrame:
    """
    Resample OHLCV data to different frequency.

    Args:
        df: DataFrame with OHLCV columns
        freq: Pandas frequency string (1H, 1D, 1W, 1M, etc.)
        agg: Custom aggregation dict

    Returns:
        Resampled DataFrame
    """
    if agg is None:
        agg = {
            'Open': 'first',
            'High': 'max',
            'Low': 'min',
            'Close': 'last',
            'Volume': 'sum'
        }

    if 'ticker' in df.columns:
        # Group by ticker
        resampled = df.groupby('ticker').resample(freq).agg(agg)
        return resampled.drop('ticker', axis=1)
    else:
        return df.resample(freq).agg(agg)


def calculate_returns(
    df: pd.DataFrame,
    periods: int = 1,
    log_returns: bool = False
) -> pd.DataFrame:
    """
    Calculate returns from price data.

    Args:
        df: DataFrame with price data
        periods: Number of periods for return calculation
        log_returns: Use log returns instead of simple returns

    Returns:
        DataFrame with returns
    """
    if log_returns:
        return np.log(df / df.shift(periods))
    else:
        return df.pct_change(periods)


def create_rolling_windows(
    df: pd.DataFrame,
    window: int,
    step: int = 1
) -> np.ndarray:
    """
    Create rolling windows from time series for ML training.

    Args:
        df: Input DataFrame
        window: Window size
        step: Step size between windows

    Returns:
        Numpy array of shape (n_windows, window, n_features)
    """
    n = len(df)
    n_windows = (n - window) // step + 1

    if n_windows <= 0:
        raise ValueError(f"Data too short for window size {window}")

    windows = []
    for i in range(0, n - window + 1, step):
        window_data = df.iloc[i:i + window].values
        windows.append(window_data)

    return np.array(windows)


def load_cpp_tickers(tickers_path: Optional[str] = None) -> List[str]:
    """
    Load ticker list from C++ project's tickers.csv.

    Args:
        tickers_path: Path to tickers.csv file. If None, defaults to
                      ../Trading_cpp/tickers.csv relative to this file.

    Returns:
        List of ticker symbols
    """
    import csv

    if tickers_path is None:
        # Default path: Trading_cpp/tickers.csv relative to project root
        # src/data/__init__.py -> src/data -> src -> Trading_Python -> Trading_super
        current_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(current_dir)))
        tickers_path = os.path.join(project_root, "Trading_cpp", "tickers.csv")

    tickers = []
    try:
        with open(tickers_path, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                # Handle both "symbol" and "ticker" column names
                ticker = row.get('symbol') or row.get('ticker')
                if ticker:
                    tickers.append(ticker.strip())
    except FileNotFoundError:
        warnings.warn(f"Tickers file not found: {tickers_path}")
    except Exception as e:
        warnings.warn(f"Error loading tickers: {e}")

    return tickers


class DataHandler:
    """
    Main data handler class for managing market data.
    """

    def __init__(
        self,
        data_dir: str = "./data",
        cache_dir: str = "./cache"
    ):
        self.data_dir = data_dir
        self.cache_dir = cache_dir

        os.makedirs(data_dir, exist_ok=True)
        os.makedirs(cache_dir, exist_ok=True)

        self._data_cache = {}

    def get(
        self,
        ticker: str,
        start: str,
        end: Optional[str] = None,
        force_refresh: bool = False
    ) -> pd.DataFrame:
        """
        Get data for a ticker, with caching.

        Args:
            ticker: Stock ticker
            start: Start date
            end: End date
            force_refresh: Force re-download even if cached

        Returns:
            DataFrame with OHLCV data
        """
        cache_key = f"{ticker}_{start}_{end}"

        if not force_refresh and cache_key in self._data_cache:
            return self._data_cache[cache_key]

        df = fetch_yahoo_data(ticker, start, end)
        self._data_cache[cache_key] = df

        return df

    def save_to_csv(self, df: pd.DataFrame, filename: str):
        """Save DataFrame to CSV."""
        filepath = os.path.join(self.data_dir, filename)
        df.to_csv(filepath)

    def load_from_csv(self, filename: str) -> pd.DataFrame:
        """Load DataFrame from CSV."""
        filepath = os.path.join(self.data_dir, filename)
        return pd.read_csv(filepath, index_col=0, parse_dates=True)


if __name__ == "__main__":
    # Test data fetching
    data = fetch_yahoo_data(["AAPL", "MSFT"], "2023-01-01")
    print(data.head())
    print(f"\nShape: {data.shape}")
