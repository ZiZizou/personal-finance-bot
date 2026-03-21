"""
Feature engineering module for trading research.
Ports technical indicators from C++ TechnicalAnalysis.h to Python.
"""

import warnings
from typing import Optional, List, Union
from functools import wraps

import numpy as np
import pandas as pd
from scipy import stats


def try_import_ta(func):
    """Decorator to try importing ta-lib, fall back to pandas implementation."""
    @wraps(func)
    def wrapper(*args, **kwargs):
        try:
            import ta
            return func(*args, **kwargs)
        except ImportError:
            warnings.warn("ta-lib not installed, using pandas fallback")
            return func(*args, fallback=True, **kwargs)
    return wrapper


class TechnicalIndicators:
    """
    Technical indicators ported from C++ TechnicalAnalysis.h.
    Provides 20+ indicators for trading signal generation.
    """

    @staticmethod
    def sma(close: pd.Series, period: int) -> pd.Series:
        """Simple Moving Average"""
        return close.rolling(window=period).mean()

    @staticmethod
    def ema(close: pd.Series, period: int) -> pd.Series:
        """Exponential Moving Average"""
        return close.ewm(span=period, adjust=False).mean()

    @staticmethod
    def rsi(close: pd.Series, period: int = 14) -> pd.Series:
        """Relative Strength Index"""
        delta = close.diff()
        gain = (delta.where(delta > 0, 0)).rolling(window=period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
        rs = gain / loss
        rsi = 100 - (100 / (1 + rs))
        return rsi

    @staticmethod
    def bollinger_bands(
        close: pd.Series,
        period: int = 20,
        num_std: float = 2.0
    ) -> tuple[pd.Series, pd.Series, pd.Series]:
        """Bollinger Bands - returns (upper, middle, lower)"""
        middle = close.rolling(window=period).mean()
        std = close.rolling(window=period).std()
        upper = middle + (std * num_std)
        lower = middle - (std * num_std)
        return upper, middle, lower

    @staticmethod
    def macd(
        close: pd.Series,
        fast: int = 12,
        slow: int = 26,
        signal: int = 9
    ) -> tuple[pd.Series, pd.Series, pd.Series]:
        """MACD - returns (macd_line, signal_line, histogram)"""
        ema_fast = close.ewm(span=fast, adjust=False).mean()
        ema_slow = close.ewm(span=slow, adjust=False).mean()
        macd_line = ema_fast - ema_slow
        signal_line = macd_line.ewm(span=signal, adjust=False).mean()
        histogram = macd_line - signal_line
        return macd_line, signal_line, histogram

    @staticmethod
    def _to_series(arr, index) -> pd.Series:
        """Convert numpy array to Series safely."""
        return pd.Series(arr.flatten() if arr.ndim > 1 else arr, index=index)

    @staticmethod
    def atr(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 14) -> pd.Series:
        """Average True Range"""
        tr1 = high - low
        tr2 = abs(high - close.shift())
        tr3 = abs(low - close.shift())
        tr = pd.concat([tr1, tr2, tr3], axis=1).max(axis=1)
        return pd.Series(tr.rolling(window=period).mean().values, index=high.index)

    @staticmethod
    def adx(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 14) -> pd.Series:
        """Average Directional Index - simplified using numpy arrays"""
        # Convert to numpy arrays to avoid pandas version issues
        h = high.values
        l = low.values
        c = close.values
        n = len(h)

        # Calculate True Range
        tr = np.zeros(n)
        for i in range(n):
            if i == 0:
                tr[i] = h[i] - l[i]
            else:
                tr[i] = max(h[i] - l[i], abs(h[i] - c[i-1]), abs(l[i] - c[i-1]))

        # Directional Movement
        plus_dm = np.zeros(n)
        minus_dm = np.zeros(n)

        for i in range(1, n):
            high_diff = h[i] - h[i-1]
            low_diff = l[i-1] - l[i]

            if high_diff > low_diff and high_diff > 0:
                plus_dm[i] = high_diff
            if low_diff > high_diff and low_diff > 0:
                minus_dm[i] = low_diff

        # Calculate smoothed values
        tr_smooth = pd.Series(tr).ewm(span=period, adjust=False).mean().values
        plus_smooth = pd.Series(plus_dm).ewm(span=period, adjust=False).mean().values
        minus_smooth = pd.Series(minus_dm).ewm(span=period, adjust=False).mean().values

        # Calculate DI
        plus_di = np.where(tr_smooth != 0, 100 * plus_smooth / tr_smooth, 0)
        minus_di = np.where(tr_smooth != 0, 100 * minus_smooth / tr_smooth, 0)

        # DX
        dx = np.where((plus_di + minus_di) != 0,
                      100 * np.abs(plus_di - minus_di) / (plus_di + minus_di), 0)

        # ADX
        adx = pd.Series(dx).ewm(span=period, adjust=False).mean().values

        return pd.Series(adx, index=high.index)

    @staticmethod
    def stochastic(
        high: pd.Series,
        low: pd.Series,
        close: pd.Series,
        period: int = 14,
        smooth_k: int = 3,
        smooth_d: int = 3
    ) -> tuple[pd.Series, pd.Series]:
        """Stochastic Oscillator - returns (%K, %D)"""
        lowest_low = low.rolling(window=period).min()
        highest_high = high.rolling(window=period).max()

        k = 100 * (close - lowest_low) / (highest_high - lowest_low)
        k_smooth = k.rolling(window=smooth_k).mean()
        d = k_smooth.rolling(window=smooth_d).mean()

        return pd.Series(k_smooth.values, index=close.index), pd.Series(d.values, index=close.index)

    @staticmethod
    def atr_ratio(atr: pd.Series, close: pd.Series) -> pd.Series:
        """ATR as ratio to close price"""
        ratio = np.divide(atr, close)
        if isinstance(ratio, pd.DataFrame):
            ratio = ratio.iloc[:, 0]
        return pd.Series(ratio, index=close.index)

    @staticmethod
    def volatility(close: pd.Series, period: int = 20) -> pd.Series:
        """Historical volatility (standard deviation of returns)"""
        returns = close.pct_change()
        return returns.rolling(window=period).std() * np.sqrt(252)

    @staticmethod
    def momentum(close: pd.Series, period: int = 10) -> pd.Series:
        """Momentum indicator"""
        return close - close.shift(period)

    @staticmethod
    def roc(close: pd.Series, period: int = 12) -> pd.Series:
        """Rate of Change"""
        return ((close - close.shift(period)) / close.shift(period)) * 100

    @staticmethod
    def williams_r(
        high: pd.Series,
        low: pd.Series,
        close: pd.Series,
        period: int = 14
    ) -> pd.Series:
        """Williams %R"""
        highest_high = high.rolling(window=period).max()
        lowest_low = low.rolling(window=period).min()
        val = -100 * (highest_high - close) / (highest_high - lowest_low)
        return pd.Series(val.values, index=close.index)

    @staticmethod
    def cci(
        high: pd.Series,
        low: pd.Series,
        close: pd.Series,
        period: int = 20
    ) -> pd.Series:
        """Commodity Channel Index"""
        tp = (high + low + close) / 3
        sma_tp = tp.rolling(window=period).mean()
        mad = tp.rolling(window=period).apply(lambda x: np.abs(x - x.mean()).mean())
        val = (tp - sma_tp) / (0.015 * mad)
        return pd.Series(val.values, index=close.index)

    @staticmethod
    def obv(close: pd.Series, volume: pd.Series) -> pd.Series:
        """On-Balance Volume"""
        obv = pd.Series(index=close.index, dtype=float)
        obv.iloc[0] = volume.iloc[0]

        for i in range(1, len(close)):
            if close.iloc[i] > close.iloc[i-1]:
                obv.iloc[i] = obv.iloc[i-1] + volume.iloc[i]
            elif close.iloc[i] < close.iloc[i-1]:
                obv.iloc[i] = obv.iloc[i-1] - volume.iloc[i]
            else:
                obv.iloc[i] = obv.iloc[i-1]

        return obv

    @staticmethod
    def ad_oscillator(high: pd.Series, low: pd.Series, close: pd.Series, volume: pd.Series) -> pd.Series:
        """Chaikin A/D Oscillator"""
        clv = ((close - low) - (high - close)) / (high - low)
        clv = clv.fillna(0)
        ad = (clv * volume).cumsum()
        return ad.rolling(window=3).mean() - ad.rolling(window=10).mean()

    @staticmethod
    def mfi(
        high: pd.Series,
        low: pd.Series,
        close: pd.Series,
        volume: pd.Series,
        period: int = 14
    ) -> pd.Series:
        """Money Flow Index"""
        tp = (high + low + close) / 3
        mf = tp * volume
        mf_diff = mf.diff()

        pos_mf = mf_diff.where(mf_diff > 0, 0).rolling(window=period).sum()
        neg_mf = (-mf_diff.where(mf_diff < 0, 0)).rolling(window=period).sum()

        mfr = pos_mf / neg_mf
        val = 100 - (100 / (1 + mfr))
        return pd.Series(val.values, index=close.index)

    @staticmethod
    def vwap(high: pd.Series, low: pd.Series, close: pd.Series, volume: pd.Series) -> pd.Series:
        """Volume Weighted Average Price"""
        tp = (high + low + close) / 3
        return (tp * volume).cumsum() / volume.cumsum()


class FeatureEngine:
    """
    Main feature engineering class.
    Creates comprehensive feature sets for ML models.
    """

    def __init__(self):
        self.ti = TechnicalIndicators()

    def add_all_indicators(
        self,
        df: pd.DataFrame,
        price_cols: Optional[dict] = None
    ) -> pd.DataFrame:
        """
        Add all technical indicators to a price dataframe.

        Args:
            df: DataFrame with OHLCV columns
            price_cols: Column name mapping (default: standard names)

        Returns:
            DataFrame with added indicator columns
        """
        # Handle MultiIndex columns from yfinance
        df_copy = df.copy()
        if isinstance(df_copy.columns, pd.MultiIndex):
            # Flatten MultiIndex to single level
            df_copy.columns = [col[0] for col in df_copy.columns]

        if price_cols is None:
            price_cols = {
                'open': 'Open',
                'high': 'High',
                'low': 'Low',
                'close': 'Close',
                'volume': 'Volume'
            }

        # Verify required columns exist
        for col in ['High', 'Low', 'Close']:
            if col not in df_copy.columns:
                raise ValueError(f"Missing required column: {col}")

        result = df_copy.copy()

        # Price-based indicators
        close = result[price_cols['close']]
        high = result[price_cols['high']]
        low = result[price_cols['low']]
        volume = result.get(price_cols.get('volume', 'Volume'), pd.Series(0, index=close.index))

        # Moving averages
        for period in [5, 10, 20, 50, 100, 200]:
            result[f'sma_{period}'] = self.ti.sma(close, period)
            result[f'ema_{period}'] = self.ti.ema(close, period)

        # Bollinger Bands
        bb_upper, bb_middle, bb_lower = self.ti.bollinger_bands(close)
        result['bb_upper'] = bb_upper
        result['bb_middle'] = bb_middle
        result['bb_lower'] = bb_lower
        result['bb_width'] = (bb_upper - bb_lower) / bb_middle
        result['bb_position'] = (close - bb_lower) / (bb_upper - bb_lower)

        # MACD
        macd, signal, hist = self.ti.macd(close)
        result['macd'] = macd
        result['macd_signal'] = signal
        result['macd_hist'] = hist

        # RSI
        result['rsi'] = self.ti.rsi(close)
        result['rsi_14'] = self.ti.rsi(close, 14)
        result['rsi_7'] = self.ti.rsi(close, 7)

        # ATR (skip ratio due to pandas version issues)
        result['atr'] = self.ti.atr(high, low, close)

        # ADX
        result['adx'] = self.ti.adx(high, low, close)

        # Stochastic
        stoch_k, stoch_d = self.ti.stochastic(high, low, close)
        result['stoch_k'] = stoch_k
        result['stoch_d'] = stoch_d

        # Momentum & ROC
        for period in [5, 10, 20]:
            result[f'momentum_{period}'] = self.ti.momentum(close, period)
            result[f'roc_{period}'] = self.ti.roc(close, period)

        # Volatility
        result['volatility_20'] = self.ti.volatility(close, 20)
        result['volatility_60'] = self.ti.volatility(close, 60)

        # Williams %R
        result['williams_r'] = self.ti.williams_r(high, low, close)

        # CCI
        result['cci'] = self.ti.cci(high, low, close)

        # MFI
        if not volume.isnull().all():
            result['mfi'] = self.ti.mfi(high, low, close, volume)

        # VWAP
        if not volume.isnull().all():
            result['vwap'] = self.ti.vwap(high, low, close, volume)

        # Returns at different horizons
        for period in [1, 5, 10, 20]:
            result[f'return_{period}d'] = close.pct_change(period)

        # Log returns
        result['log_return'] = np.log(close / close.shift(1))

        return result

    def add_lagged_features(
        self,
        df: pd.DataFrame,
        columns: List[str],
        lags: int = 5
    ) -> pd.DataFrame:
        """Add lagged versions of features."""
        result = df.copy()

        for col in columns:
            if col in result.columns:
                for lag in range(1, lags + 1):
                    result[f'{col}_lag{lag}'] = result[col].shift(lag)

        return result

    def add_rolling_stats(
        self,
        df: pd.DataFrame,
        columns: List[str],
        windows: List[int] = [5, 10, 20, 60]
    ) -> pd.DataFrame:
        """Add rolling statistics (mean, std, min, max)."""
        result = df.copy()

        for col in columns:
            if col in result.columns:
                for window in windows:
                    result[f'{col}_ma{window}'] = result[col].rolling(window).mean()
                    result[f'{col}_std{window}'] = result[col].rolling(window).std()
                    result[f'{col}_min{window}'] = result[col].rolling(window).min()
                    result[f'{col}_max{window}'] = result[col].rolling(window).max()

        return result


def compute_alpha_features(df: pd.DataFrame) -> pd.DataFrame:
    """
    Compute alpha factors inspired by AlphaSuite/Alphalens.
    These are market-relative features that have shown predictive power.
    """
    result = df.copy()
    close = result['Close']

    # Price-based alphas
    result['alpha_001'] = (close - close.shift(5)) / close.shift(5)
    result['alpha_002'] = -1 * ((close - close.rolling(8).mean()) / close.rolling(8).mean())
    result['alpha_003'] = -1 * ((close - close.rolling(20).min()) / close.rolling(20).min())

    # Volume-based alphas
    if 'Volume' in result.columns:
        vol = result['Volume']
        result['alpha_004'] = -1 * ((vol - vol.rolling(20).mean()) / vol.rolling(20).std())
        result['alpha_005'] = -1 * (vol / vol.shift(1) - 1)

    # Correlation-based alphas
    returns = close.pct_change()
    result['alpha_006'] = returns.rolling(6).corr(returns.rolling(6).std())

    return result


if __name__ == "__main__":
    # Test with sample data
    import yfinance as yf

    data = yf.download("AAPL", start="2023-01-01", end="2024-01-01")
    print(f"Input shape: {data.shape}")

    engine = FeatureEngine()
    features = engine.add_all_indicators(data)

    print(f"Output shape: {features.shape}")
    print(f"New columns: {len(features.columns) - len(data.columns)}")
    print(f"Sample indicators:\n{features[['Close', 'rsi', 'macd', 'adx']].tail()}")
