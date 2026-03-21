#!/usr/bin/env python
"""
Ticker Manager for Dynamic ONNX Model Management

Manages the 7 active ONNX models based on C++ ticker selection.
Tracks selected tickers, trains/retrains models, and cleans up unused models.
"""

import os
import sys
from datetime import datetime
from typing import List, Dict, Optional, Set
from pathlib import Path
import logging
import shutil

import numpy as np

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

logger = logging.getLogger(__name__)


class TickerManager:
    """
    Manages dynamic ONNX models for selected tickers.

    Maintains up to MAX_ACTIVE_MODELS (default 7) models trained on
    the most "hot" tickers based on volatility and news sentiment.
    """

    MAX_ACTIVE_MODELS = 7
    MODELS_DIR = "./models/active"
    ACTIVE_TICKERS_FILE = "./models/active_tickers.json"

    def __init__(self, max_models: int = 7):
        self.max_models = max_models
        self.active_tickers: Set[str] = set()
        self.last_update: Optional[datetime] = None

        # Create models directory
        os.makedirs(self.MODELS_DIR, exist_ok=True)

        # Load existing active tickers
        self._load_active_tickers()

    def _load_active_tickers(self):
        """Load active tickers from file if exists."""
        try:
            import json
            if os.path.exists(self.ACTIVE_TICKERS_FILE):
                with open(self.ACTIVE_TICKERS_FILE, 'r') as f:
                    data = json.load(f)
                    self.active_tickers = set(data.get('tickers', []))
                    self.last_update = datetime.fromisoformat(data.get('last_update', datetime.now().isoformat()))
                    logger.info(f"Loaded {len(self.active_tickers)} active tickers")
        except Exception as e:
            logger.warning(f"Failed to load active tickers: {e}")

    def _save_active_tickers(self):
        """Save active tickers to file."""
        try:
            import json
            data = {
                'tickers': list(self.active_tickers),
                'last_update': datetime.now().isoformat()
            }
            with open(self.ACTIVE_TICKERS_FILE, 'w') as f:
                json.dump(data, f)
            logger.info(f"Saved {len(self.active_tickers)} active tickers")
        except Exception as e:
            logger.error(f"Failed to save active tickers: {e}")

    def set_selected_tickers(self, tickers: List[str]) -> Dict:
        """
        Set the selected tickers for model training.

        Args:
            tickers: List of ticker symbols to train models for

        Returns:
            Dict with status and list of trained tickers
        """
        tickers = [t.upper().strip() for t in tickers if t]
        new_tickers = set(tickers[:self.max_models])

        # Check if anything changed
        if new_tickers == self.active_tickers:
            logger.info("No change in selected tickers")
            return {
                "status": "ok",
                "tickers": list(self.active_tickers),
                "trained": [],
                "message": "No changes - same tickers"
            }

        # Determine which tickers need new models
        tickers_to_train = new_tickers - self.active_tickers
        tickers_to_remove = self.active_tickers - new_tickers

        # Remove old models
        for ticker in tickers_to_remove:
            self._remove_model(ticker)

        # Update active tickers
        self.active_tickers = new_tickers
        self.last_update = datetime.now()
        self._save_active_tickers()

        # Train new models
        trained = []
        for ticker in tickers_to_train:
            success = self._train_model(ticker)
            if success:
                trained.append(ticker)

        return {
            "status": "ok",
            "tickers": list(self.active_tickers),
            "trained": trained,
            "removed": list(tickers_to_remove),
            "message": f"Trained {len(trained)} new models"
        }

    def _get_model_path(self, ticker: str) -> str:
        """Get the path to a ticker's model file."""
        return os.path.join(self.MODELS_DIR, f"{ticker}.onnx")

    def _remove_model(self, ticker: str):
        """Remove the model file for a ticker."""
        model_path = self._get_model_path(ticker)
        if os.path.exists(model_path):
            try:
                os.remove(model_path)
                logger.info(f"Removed model for {ticker}")
            except Exception as e:
                logger.error(f"Failed to remove model for {ticker}: {e}")

    def _train_model(self, ticker: str) -> bool:
        """
        Train and save an ONNX model for a single ticker.

        Args:
            ticker: Ticker symbol

        Returns:
            True if successful, False otherwise
        """
        try:
            logger.info(f"Training model for {ticker}...")

            # Fetch training data
            from src.data import fetch_yahoo_data
            from src.features import FeatureEngine
            from src.models import train_and_export

            # Get 2 years of data
            from datetime import timedelta
            end_date = datetime.now().strftime("%Y-%m-%d")
            start_date = (datetime.now() - timedelta(days=730)).strftime("%Y-%m-%d")

            data = fetch_yahoo_data(ticker, start_date=start_date, end_date=end_date, progress=False)

            if data.empty:
                logger.warning(f"No data fetched for {ticker}")
                return False

            # Add features
            engine = FeatureEngine()
            features = engine.add_all_indicators(data)

            # Drop ticker column if present (added by fetch_yahoo_data)
            if 'ticker' in features.columns:
                features = features.drop('ticker', axis=1)

            # Create target: binary signal
            features['return_1d'] = features['Close'].pct_change(1)
            features['signal'] = np.where(features['return_1d'] > 0, 1, -1)

            # Drop rows with NaN
            features = features.dropna()
            if len(features) < 50:
                logger.warning(f"Insufficient data for {ticker}")
                return False

            # Train model
            output_dir = self.MODELS_DIR
            model_path = self._get_model_path(ticker)

            trainer, onnx_path = train_and_export(
                features,
                target_col="signal",
                model_type="ridge",
                output_dir=output_dir,
                model_name=f"{ticker}"
            )

            logger.info(f"Model trained for {ticker}: {onnx_path}")
            return True

        except Exception as e:
            logger.error(f"Failed to train model for {ticker}: {e}")
            return False

    def get_model_status(self) -> Dict:
        """
        Get status of active models.

        Returns:
            Dict with active model information
        """
        active_models = 0
        model_files = []

        for ticker in self.active_tickers:
            model_path = self._get_model_path(ticker)
            if os.path.exists(model_path):
                active_models += 1
                model_files.append({
                    "ticker": ticker,
                    "path": model_path,
                    "size_bytes": os.path.getsize(model_path)
                })

        return {
            "active_models": active_models,
            "max_models": self.max_models,
            "tickers": list(self.active_tickers),
            "models": model_files,
            "last_update": self.last_update.isoformat() if self.last_update else None
        }

    def get_signal(self, ticker: str) -> Optional[Dict]:
        """
        Get signal for a ticker using its ONNX model.

        Args:
            ticker: Ticker symbol

        Returns:
            Signal dict or None if no model available
        """
        ticker = ticker.upper()

        if ticker not in self.active_tickers:
            logger.debug(f"{ticker} not in active tickers")
            return None

        model_path = self._get_model_path(ticker)
        if not os.path.exists(model_path):
            logger.warning(f"Model not found for {ticker}")
            return None

        try:
            # Get latest features
            from src.service.feature_cache import FeatureCache
            feature_cache = FeatureCache(ttl=900)
            features = feature_cache.get(ticker)

            if features is None or features.empty:
                logger.debug(f"No cached features for {ticker}")
                return None

            latest = features.iloc[-1:]

            # Run ONNX inference
            import onnxruntime as ort
            session = ort.InferenceSession(model_path)

            input_name = session.get_inputs()[0].name
            feature_cols = [c for c in latest.columns
                          if c not in ['return_1d', 'Open', 'High', 'Low', 'Close', 'Volume', 'ticker']]
            X = latest[feature_cols].values.astype(np.float32)
            X = np.nan_to_num(X, nan=0.0)

            prediction = session.run(None, {input_name: X})[0][0]

            # Convert prediction to signal
            if prediction > 0.01:
                signal_type = "buy"
                confidence = min(abs(prediction) * 10, 0.95)
            elif prediction < -0.01:
                signal_type = "sell"
                confidence = min(abs(prediction) * 10, 0.95)
            else:
                signal_type = "hold"
                confidence = 0.5

            price = float(latest['Close'].iloc[-1]) if 'Close' in latest.columns else None

            return {
                "ticker": ticker,
                "signal": signal_type,
                "confidence": float(confidence),
                "price": price,
                "timestamp": datetime.now().isoformat(),
                "source": "ticker_manager"
            }

        except ImportError:
            logger.warning("onnxruntime not installed")
            return None
        except Exception as e:
            logger.error(f"Error getting signal for {ticker}: {e}")
            return None


# Global ticker manager instance
_ticker_manager: Optional[TickerManager] = None


def get_ticker_manager() -> TickerManager:
    """Get the global ticker manager instance."""
    global _ticker_manager
    if _ticker_manager is None:
        _ticker_manager = TickerManager()
    return _ticker_manager


if __name__ == "__main__":
    # Test the ticker manager
    logging.basicConfig(level=logging.INFO)

    manager = TickerManager(max_models=7)

    # Test setting tickers
    result = manager.set_selected_tickers(["AAPL", "NVDA", "MSFT", "GOOGL", "AMZN", "META", "TSLA"])
    print(f"Set tickers result: {result}")

    # Get status
    status = manager.get_model_status()
    print(f"Model status: {status}")
