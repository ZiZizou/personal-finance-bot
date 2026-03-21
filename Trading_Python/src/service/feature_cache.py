"""
Feature caching module for trading research.
Provides caching of computed features to avoid redundant calculations.
"""

import os
import pickle
import time
import logging
from pathlib import Path
from typing import Optional, Dict, Any, List
from datetime import datetime
import pandas as pd

logger = logging.getLogger(__name__)


class FeatureCache:
    """
    Cache for storing computed features.

    Features are stored as pickle files in the cache directory.
    TTL (time-to-live) support to ensure fresh data.
    """

    DEFAULT_TTL = 900  # 15 minutes in seconds

    def __init__(self, cache_dir: str = "./data/features", ttl: int = DEFAULT_TTL):
        """
        Initialize the feature cache.

        Args:
            cache_dir: Directory to store cached features
            ttl: Time-to-live in seconds (default: 15 minutes)
        """
        self.cache_dir = Path(cache_dir)
        self.ttl = ttl
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def _get_cache_path(self, ticker: str) -> Path:
        """Get the cache file path for a ticker."""
        return self.cache_dir / f"{ticker.upper()}.pkl"

    def get(self, ticker: str) -> Optional[pd.DataFrame]:
        """
        Get cached features for a ticker.

        Args:
            ticker: Stock ticker symbol

        Returns:
            Cached features DataFrame or None if not found/expired
        """
        cache_path = self._get_cache_path(ticker)

        if not cache_path.exists():
            logger.debug(f"Cache miss for {ticker}: file not found")
            return None

        # Check TTL
        try:
            mtime = cache_path.stat().st_mtime
            age = time.time() - mtime
            if age > self.ttl:
                logger.debug(f"Cache expired for {ticker}: age={age:.0f}s > ttl={self.ttl}s")
                return None

            with open(cache_path, 'rb') as f:
                data = pickle.load(f)
                logger.debug(f"Cache hit for {ticker}")
                return data

        except Exception as e:
            logger.warning(f"Error reading cache for {ticker}: {e}")
            return None

    def set(self, ticker: str, features: pd.DataFrame) -> bool:
        """
        Store features in cache.

        Args:
            ticker: Stock ticker symbol
            features: DataFrame with cached features

        Returns:
            True if successful, False otherwise
        """
        cache_path = self._get_cache_path(ticker)

        try:
            with open(cache_path, 'wb') as f:
                pickle.dump(features, f)
            logger.debug(f"Cached features for {ticker}")
            return True
        except Exception as e:
            logger.error(f"Error caching features for {ticker}: {e}")
            return False

    def get_or_compute(
        self,
        ticker: str,
        compute_func: callable
    ) -> pd.DataFrame:
        """
        Get cached features or compute them if not available.

        Args:
            ticker: Stock ticker symbol
            compute_func: Function to compute features if not cached

        Returns:
            Features DataFrame
        """
        cached = self.get(ticker)
        if cached is not None:
            return cached

        # Compute and cache
        features = compute_func(ticker)
        self.set(ticker, features)
        return features

    def invalidate(self, ticker: str) -> bool:
        """
        Invalidate cached features for a ticker.

        Args:
            ticker: Stock ticker symbol

        Returns:
            True if file was deleted, False otherwise
        """
        cache_path = self._get_cache_path(ticker)
        if cache_path.exists():
            try:
                cache_path.unlink()
                logger.debug(f"Invalidated cache for {ticker}")
                return True
            except Exception as e:
                logger.error(f"Error invalidating cache for {ticker}: {e}")
        return False

    def invalidate_all(self) -> int:
        """
        Clear all cached features.

        Returns:
            Number of files deleted
        """
        count = 0
        for cache_file in self.cache_dir.glob("*.pkl"):
            try:
                cache_file.unlink()
                count += 1
            except Exception as e:
                logger.error(f"Error deleting {cache_file}: {e}")
        logger.info(f"Cleared {count} cached feature files")
        return count

    def list_cached(self) -> List[str]:
        """
        List all tickers with cached features.

        Returns:
            List of ticker symbols
        """
        return [f.stem for f in self.cache_dir.glob("*.pkl")]

    def get_cache_info(self) -> Dict[str, Any]:
        """
        Get information about the cache.

        Returns:
            Dict with cache statistics
        """
        cached_files = list(self.cache_dir.glob("*.pkl"))
        total_size = sum(f.stat().st_size for f in cached_files)

        # Check file ages
        now = time.time()
        expired = sum(1 for f in cached_files if now - f.stat().st_mtime > self.ttl)
        fresh = len(cached_files) - expired

        return {
            "total_files": len(cached_files),
            "fresh_files": fresh,
            "expired_files": expired,
            "total_size_bytes": total_size,
            "cache_dir": str(self.cache_dir),
            "ttl_seconds": self.ttl
        }


class SignalCache:
    """
    Cache for storing generated trading signals.
    """

    def __init__(self, cache_dir: str = "./data/signals"):
        """
        Initialize the signal cache.

        Args:
            cache_dir: Directory to store signals
        """
        self.cache_dir = Path(cache_dir)
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def _get_signal_path(self, ticker: str) -> Path:
        """Get the signal file path for a ticker."""
        return self.cache_dir / f"{ticker.upper()}_signal.json"

    def get_signal(self, ticker: str) -> Optional[Dict[str, Any]]:
        """
        Get cached signal for a ticker.

        Args:
            ticker: Stock ticker symbol

        Returns:
            Signal dict or None if not found
        """
        import json

        signal_path = self._get_signal_path(ticker)
        if not signal_path.exists():
            return None

        try:
            with open(signal_path, 'r') as f:
                return json.load(f)
        except Exception as e:
            logger.warning(f"Error reading signal for {ticker}: {e}")
            return None

    def set_signal(self, ticker: str, signal: Dict[str, Any]) -> bool:
        """
        Store signal in cache.

        Args:
            ticker: Stock ticker symbol
            signal: Signal dict with keys: signal, confidence, timestamp, etc.

        Returns:
            True if successful, False otherwise
        """
        import json

        signal_path = self._get_signal_path(ticker)
        try:
            with open(signal_path, 'w') as f:
                json.dump(signal, f, indent=2, default=str)
            return True
        except Exception as e:
            logger.error(f"Error saving signal for {ticker}: {e}")
            return False

    def get_all_signals(self) -> Dict[str, Dict[str, Any]]:
        """
        Get all cached signals.

        Returns:
            Dict mapping ticker -> signal dict
        """
        signals = {}
        for signal_file in self.cache_dir.glob("*_signal.json"):
            ticker = signal_file.stem.replace("_signal", "")
            sig = self.get_signal(ticker)
            if sig:
                signals[ticker] = sig
        return signals


if __name__ == "__main__":
    # Test the feature cache
    logging.basicConfig(level=logging.DEBUG)

    cache = FeatureCache(ttl=60)  # 1 minute TTL for testing

    # Create test data
    import numpy as np
    test_df = pd.DataFrame({
        'Close': np.random.randn(100).cumsum() + 100,
        'Volume': np.random.randint(1000, 10000, 100)
    })

    # Test set and get
    cache.set("TEST", test_df)
    retrieved = cache.get("TEST")
    print(f"Retrieved data shape: {retrieved.shape if retrieved is not None else None}")

    # Test cache info
    info = cache.get_cache_info()
    print(f"Cache info: {info}")

    # List cached
    print(f"Cached tickers: {cache.list_cached()}")