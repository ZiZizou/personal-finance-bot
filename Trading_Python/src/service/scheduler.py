#!/usr/bin/env python
"""
Background scheduler service for Trading-Python

Handles scheduled tasks:
- News scraping (every hour)
- Feature updates (every 15 minutes)
- Signal generation (every 5 minutes)
- Model retraining (daily at market close)

All tasks only run during extended market hours (8 AM - 6 PM ET, weekdays).
"""

import os
import sys
from datetime import datetime
from typing import Dict, List, Optional
from pathlib import Path
from apscheduler.schedulers.background import BackgroundScheduler
from apscheduler.triggers.interval import IntervalTrigger
from apscheduler.triggers.cron import CronTrigger
import logging

import numpy as np
import pandas as pd

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from src.service.market_hours import is_extended_hours, should_run_scheduled_task

logger = logging.getLogger(__name__)


class SchedulerService:
    """Background scheduler for trading tasks"""

    def __init__(self):
        self.scheduler = BackgroundScheduler()
        self._setup_jobs()

    def _setup_jobs(self):
        """Configure scheduled jobs"""

        # News scraping - every hour at :30 (only during market hours)
        # Runs 30 minutes before signal generation so sentiment is available
        self.scheduler.add_job(
            self.scrape_news,
            trigger=CronTrigger(minute=30),  # Run at :30 of every hour
            id="scrape_news",
            name="Scrape News",
            replace_existing=True
        )

        # Feature updates - every 15 minutes (only during market hours)
        self.scheduler.add_job(
            self.update_features,
            trigger=IntervalTrigger(minutes=15),
            id="update_features",
            name="Update Features",
            replace_existing=True
        )

        # Signal generation - hourly at top of hour (only during market hours)
        # Changed from 5-minute interval to hourly to reduce noise and align with human-in-the-loop
        self.scheduler.add_job(
            self.generate_signals,
            trigger=CronTrigger(hour="*", minute=0),
            id="generate_signals",
            name="Generate Signals",
            replace_existing=True
        )

        # Model retraining - daily at 16:00 (market close)
        self.scheduler.add_job(
            self.retrain_models,
            trigger=CronTrigger(hour=16, minute=0),
            id="retrain_models",
            name="Retrain Models",
            replace_existing=True
        )

    def scrape_news(self):
        """Background news scraping task - only runs during market hours"""
        # Check if we should run (market hours check)
        if not should_run_scheduled_task():
            logger.debug("Skipping news scraping - outside market hours")
            return

        logger.info("Starting scheduled news scraping...")

        try:
            from src.news import NewsScraper, NewsDatabase

            scraper = NewsScraper(
                db_path="./data/news.db",
                min_score_threshold=1,
                max_articles_per_source=20
            )

            articles = scraper.run(
                industries=None,
                update_db=True,
                cleanup=True
            )

            logger.info(f"News scraping completed. Fetched {len(articles)} articles")

        except Exception as e:
            logger.error(f"News scraping failed: {e}")

    def update_features(self):
        """Background feature update task - only runs during market hours"""
        # Check if we should run (market hours check)
        if not should_run_scheduled_task():
            logger.debug("Skipping feature update - outside market hours")
            return

        logger.info("Starting scheduled feature update...")

        try:
            # 1. Load ticker list from C++
            from src.data import load_cpp_tickers, fetch_yahoo_data
            from src.features import FeatureEngine
            from src.service.feature_cache import FeatureCache

            tickers = load_cpp_tickers()
            logger.info(f"Loaded {len(tickers)} tickers from C++")

            if not tickers:
                logger.warning("No tickers loaded, skipping feature update")
                return

            # 2. Fetch latest price data for recent period
            # Get last 60 days of data to compute indicators properly
            from datetime import timedelta
            end_date = datetime.now().strftime("%Y-%m-%d")
            start_date = (datetime.now() - timedelta(days=60)).strftime("%Y-%m-%d")

            # 3. Initialize feature cache
            cache = FeatureCache(ttl=900)  # 15 minute TTL

            # 4. Process each ticker
            engine = FeatureEngine()
            successful = 0
            failed = 0

            for ticker in tickers:
                try:
                    logger.debug(f"Processing {ticker}...")
                    data = fetch_yahoo_data(
                        ticker,
                        start_date=start_date,
                        end_date=end_date,
                        progress=False
                    )

                    if data.empty:
                        logger.warning(f"No data fetched for {ticker}")
                        failed += 1
                        continue

                    # Calculate technical indicators
                    features = engine.add_all_indicators(data)

                    # Add target variable (next day return)
                    features['return_1d'] = features['Close'].pct_change(1)

                    # Cache the features
                    cache.set(ticker, features)
                    successful += 1

                except Exception as e:
                    logger.warning(f"Failed to process {ticker}: {e}")
                    failed += 1

            logger.info(f"Feature update completed: {successful} successful, {failed} failed")

        except Exception as e:
            logger.error(f"Feature update failed: {e}")

    def generate_signals(self):
        """Background signal generation task - runs hourly during market hours"""
        # Check if we should run (market hours check)
        if not should_run_scheduled_task():
            logger.debug("Skipping signal generation - outside market hours")
            return

        logger.info("Starting scheduled signal generation (hourly)...")

        try:
            # 1. Load tickers to monitor: portfolio holdings + promising candidates
            from src.service.feature_cache import FeatureCache, SignalCache
            from src.api.routes.portfolio import _load_portfolio

            # Get portfolio tickers (holdings to monitor for rebalancing)
            portfolio_tickers = []
            try:
                portfolio_data = _load_portfolio()
                portfolio_tickers = [p['ticker'].upper() for p in portfolio_data.get('positions', []) if p.get('shares', 0) > 0]
                logger.info(f"Portfolio tickers to monitor: {portfolio_tickers}")
            except Exception as e:
                logger.warning(f"Could not load portfolio tickers: {e}")

            # Get selected tickers (promising stocks for new opportunities)
            selected_tickers = []
            selected_file = Path("./models/selected_tickers.txt")
            if selected_file.exists():
                try:
                    content = selected_file.read_text().strip()
                    if content:
                        selected_tickers = [t.strip().upper() for t in content.split(',') if t.strip()]
                        logger.info(f"Selected tickers to monitor: {selected_tickers}")
                except Exception as e:
                    logger.warning(f"Could not load selected tickers: {e}")

            # Combine and deduplicate: portfolio + selected tickers
            all_tickers = list(set(portfolio_tickers + selected_tickers))

            if not all_tickers:
                logger.warning("No tickers to monitor for signals")
                return

            tickers = all_tickers
            feature_cache = FeatureCache(ttl=900)
            signal_cache = SignalCache()

            # Create sets for efficient lookup
            portfolio_tickers_set = set(portfolio_tickers)
            selected_tickers_set = set(selected_tickers)

            # 2. Try to load ONNX model for inference
            onnx_model = None
            model_path = "./models/trading_model.onnx"

            try:
                import onnxruntime as ort
                if os.path.exists(model_path):
                    onnx_model = ort.InferenceSession(model_path)
                    logger.info(f"Loaded ONNX model from {model_path}")
                else:
                    logger.info("No trained ONNX model found, using fallback")
            except ImportError:
                logger.warning("onnxruntime not installed, using fallback")
            except Exception as e:
                logger.warning(f"Could not load ONNX model: {e}")

            # 3. Generate signals for each ticker
            generated = 0
            failed = 0

            for ticker in tickers:
                try:
                    # Get cached features
                    features = feature_cache.get(ticker)
                    if features is None or features.empty:
                        logger.debug(f"No cached features for {ticker}")
                        failed += 1
                        continue

                    # Get the latest features (last row)
                    latest = features.iloc[-1:]

                    # Check if we have ONNX model
                    if onnx_model is not None:
                        # Run inference
                        input_name = onnx_model.get_inputs()[0].name
                        feature_cols = [c for c in latest.columns
                                       if c not in ['return_1d', 'Open', 'High', 'Low', 'Close', 'Volume', 'ticker']]
                        X = latest[feature_cols].values.astype(np.float32)

                        # Handle NaN values
                        X = np.nan_to_num(X, nan=0.0)

                        # Run prediction
                        prediction = onnx_model.run(None, {input_name: X})[0][0]

                        # Convert prediction to signal
                        if prediction > 0.01:  # Positive return expected
                            signal_type = "buy"
                            confidence = min(abs(prediction) * 10, 0.95)
                        elif prediction < -0.01:  # Negative return expected
                            signal_type = "sell"
                            confidence = min(abs(prediction) * 10, 0.95)
                        else:
                            signal_type = "hold"
                            confidence = 0.5
                    else:
                        # Fallback: simple technical analysis
                        signal_type, confidence = self._generate_fallback_signal(latest)

                    # Get news sentiment for this ticker
                    sentiment_score = 0.0
                    try:
                        from src.news.database import NewsDatabase
                        db = NewsDatabase("./data/news.db")
                        sentiment_score = db.get_cumulative_score(ticker, days=7)
                    except Exception as e:
                        logger.warning(f"Could not get sentiment for {ticker}: {e}")

                    # Normalize sentiment to -1 to 1 range (scores are typically -30 to +30)
                    sentiment_normalized = max(-1.0, min(1.0, sentiment_score / 30.0))

                    # Adjust confidence based on sentiment alignment
                    # If sentiment is strongly negative and signal is buy, reduce confidence
                    if signal_type == "buy" and sentiment_normalized < -0.3:
                        confidence = confidence * 0.5
                        logger.info(f"{ticker}: Buy signal reduced due to negative sentiment ({sentiment_normalized:.2f})")
                    elif signal_type == "sell" and sentiment_normalized > 0.3:
                        confidence = confidence * 0.5
                        logger.info(f"{ticker}: Sell signal reduced due to positive sentiment ({sentiment_normalized:.2f})")

                    # Get old signal before updating
                    old_signal = signal_cache.get_signal(ticker)

                    # Create signal dict with sentiment
                    signal = {
                        "ticker": ticker.upper(),
                        "signal": signal_type,
                        "confidence": float(confidence),
                        "timestamp": datetime.now().isoformat(),
                        "price": float(latest['Close'].iloc[-1]) if 'Close' in latest.columns else None,
                        "source": "scheduler",
                        "sentiment_score": sentiment_normalized
                    }

                    # If signal changed to buy/sell, write notification for C++ listener
                    if old_signal:
                        old_type = old_signal.get('signal', 'hold')
                        new_type = signal['signal']
                        if old_type != new_type and new_type in ['buy', 'sell']:
                            is_portfolio_ticker = ticker.upper() in portfolio_tickers_set
                            is_selected_ticker = ticker.upper() in selected_tickers_set
                            self._write_notification(ticker, old_signal, signal,
                                                   is_portfolio=is_portfolio_ticker,
                                                   is_selected=is_selected_ticker)

                    # Save current as previous for next iteration
                    signal_cache.set_previous_signal(ticker, old_signal if old_signal else signal)

                    # Cache the new signal
                    signal_cache.set_signal(ticker, signal)
                    generated += 1

                except Exception as e:
                    logger.warning(f"Failed to generate signal for {ticker}: {e}")
                    failed += 1

            logger.info(f"Signal generation completed: {generated} generated, {failed} failed")

        except Exception as e:
            logger.error(f"Signal generation failed: {e}")

    def _generate_fallback_signal(self, features: pd.DataFrame) -> tuple:
        """
        Generate signal using simple technical analysis when no model is available.

        Args:
            features: DataFrame with technical indicators

        Returns:
            Tuple of (signal_type, confidence)
        """
        try:
            # Simple RSI-based signal
            rsi = features.get('rsi', features.get('rsi_14'))
            if rsi is not None:
                rsi_val = rsi.iloc[-1]
                if pd.notna(rsi_val):
                    if rsi_val < 30:
                        return "buy", 0.6
                    elif rsi_val > 70:
                        return "sell", 0.6

            # MACD-based signal
            macd = features.get('macd')
            macd_signal = features.get('macd_signal')
            if macd is not None and macd_signal is not None:
                macd_val = macd.iloc[-1]
                macd_sig_val = macd_signal.iloc[-1]
                if pd.notna(macd_val) and pd.notna(macd_sig_val):
                    if macd_val > macd_sig_val:
                        return "buy", 0.55
                    elif macd_val < macd_sig_val:
                        return "sell", 0.55

            # Default: hold
            return "hold", 0.5

        except Exception as e:
            logger.warning(f"Fallback signal generation failed: {e}")
            return "hold", 0.5

    def retrain_models(self):
        """Background model retraining task - trains on top 7 actionable tickers"""
        logger.info("Starting scheduled model retraining...")

        try:
            # 1. Load ticker list from C++
            from src.data import load_cpp_tickers, fetch_yahoo_data
            from src.features import FeatureEngine
            from src.models import train_and_export

            all_tickers = load_cpp_tickers()
            logger.info(f"Loaded {len(all_tickers)} tickers from C++")

            if not all_tickers:
                logger.warning("No tickers to train on")
                return

            # 2. Select top 7 actionable tickers based on volatility + sentiment
            tickers = self._select_top_tickers(all_tickers, max_tickers=7, days=30)

            if not tickers:
                logger.warning("No actionable tickers selected, using top by volatility")
                # Fallback: use top 7 by volatility only
                scores = self._calculate_ticker_actionability(all_tickers, days=30)
                sorted_by_vol = sorted(scores.items(), key=lambda x: x[1], reverse=True)
                tickers = [t for t, _ in sorted_by_vol[:7]]

            # Save selected tickers for C++ to read
            self._save_selected_tickers(tickers)

            # 2. Fetch recent training data (last 2 years)
            from datetime import timedelta
            end_date = datetime.now().strftime("%Y-%m-%d")
            start_date = (datetime.now() - timedelta(days=730)).strftime("%Y-%m-%d")  # ~2 years

            logger.info(f"Fetching training data from {start_date} to {end_date}")
            data = fetch_yahoo_data(tickers, start_date=start_date, end_date=end_date, progress=True)

            if data.empty:
                logger.error("No data fetched for training")
                return

            # 3. Perform correlation-based clustering
            logger.info("Performing correlation-based clustering...")
            clusters = self._cluster_tickers_by_correlation(data, n_clusters=10)

            logger.info(f"Created {len(clusters)} clusters")
            for i, cluster_tickers in enumerate(clusters):
                logger.info(f"  Cluster {i}: {len(cluster_tickers)} tickers")

            # 4. Train models per cluster
            engine = FeatureEngine()
            os.makedirs("./models/clusters", exist_ok=True)

            trained_clusters = 0
            for cluster_id, cluster_tickers in clusters.items():
                try:
                    logger.info(f"Training cluster {cluster_id}...")

                    # Get data for this cluster
                    cluster_data = data[data['ticker'].isin(cluster_tickers)].copy()
                    if cluster_data.empty:
                        logger.warning(f"No data for cluster {cluster_id}")
                        continue

                    # Add features
                    features = engine.add_all_indicators(cluster_data)

                    # Create target: binary signal (1 if positive return, -1 if negative)
                    features['return_1d'] = features['Close'].pct_change(1)
                    features['signal'] = np.where(features['return_1d'] > 0, 1, -1)

                    # Drop rows with NaN
                    features = features.dropna()
                    if len(features) < 50:
                        logger.warning(f"Insufficient data for cluster {cluster_id}")
                        continue

                    # Train model
                    output_dir = f"./models/clusters/cluster_{cluster_id}"
                    os.makedirs(output_dir, exist_ok=True)

                    trainer, onnx_path = train_and_export(
                        features,
                        target_col="signal",
                        model_type="ridge",
                        output_dir=output_dir,
                        model_name=f"cluster_{cluster_id}_ridge"
                    )

                    logger.info(f"  Cluster {cluster_id} trained: {onnx_path}")
                    trained_clusters += 1

                except Exception as e:
                    logger.error(f"Failed to train cluster {cluster_id}: {e}")

            # 5. Also train a general model on all data
            logger.info("Training general model on all tickers...")
            try:
                all_features = engine.add_all_indicators(data.copy())
                all_features['return_1d'] = all_features['Close'].pct_change(1)
                all_features['signal'] = np.where(all_features['return_1d'] > 0, 1, -1)
                all_features = all_features.dropna()

                if len(all_features) >= 50:
                    os.makedirs("./models", exist_ok=True)
                    trainer, onnx_path = train_and_export(
                        all_features,
                        target_col="signal",
                        model_type="ridge",
                        output_dir="./models",
                        model_name="trading_model"
                    )
                    logger.info(f"General model trained: {onnx_path}")
            except Exception as e:
                logger.error(f"Failed to train general model: {e}")

            # 6. Create reload signal file for C++
            self._create_reload_signal()

            # 7. Copy model to C++ directory
            self._copy_model_to_cpp()

            logger.info(f"Model retraining completed: {trained_clusters} cluster models trained")

        except Exception as e:
            logger.error(f"Model retraining failed: {e}")

    def _cluster_tickers_by_correlation(
        self,
        data: pd.DataFrame,
        n_clusters: int = 10
    ) -> Dict[int, List[str]]:
        """
        Cluster tickers by price correlation.

        Args:
            data: DataFrame with OHLCV data
            n_clusters: Number of clusters

        Returns:
            Dict mapping cluster_id -> list of tickers
        """
        try:
            from scipy.cluster.hierarchy import linkage, fcluster

            # Calculate returns for each ticker
            tickers = data['ticker'].unique()
            returns_dict = {}

            for ticker in tickers:
                ticker_data = data[data['ticker'] == ticker].sort_index()
                if 'Close' in ticker_data.columns:
                    returns = ticker_data['Close'].pct_change().dropna()
                    if len(returns) > 30:  # Need sufficient data
                        returns_dict[ticker] = returns

            if len(returns_dict) < 2:
                # Not enough data, return simple round-robin clustering
                return {i % n_clusters: [t] for i, t in enumerate(returns_dict.keys())}

            # Align all returns to same index
            returns_df = pd.DataFrame(returns_dict)
            returns_df = returns_df.dropna()

            if returns_df.shape[1] < 2 or returns_df.shape[0] < 30:
                return {i % n_clusters: [t] for i, t in enumerate(returns_dict.keys())}

            # Calculate correlation matrix
            corr_matrix = returns_df.corr()

            # Convert to distance matrix
            distance = 1 - corr_matrix.abs()

            # Hierarchical clustering
            Z = linkage(distance, method='ward')
            cluster_labels = fcluster(Z, n_clusters, criterion='maxclust')

            # Build result dict
            clusters = {}
            for ticker, label in zip(returns_dict.keys(), cluster_labels):
                if label not in clusters:
                    clusters[label] = []
                clusters[label].append(ticker)

            return clusters

        except Exception as e:
            logger.warning(f"Clustering failed: {e}, using simple distribution")
            # Fallback: distribute tickers evenly
            return {i % n_clusters: [t] for i, t in enumerate(tickers)}

    def _calculate_ticker_actionability(
        self,
        tickers: List[str],
        days: int = 30
    ) -> Dict[str, float]:
        """
        Calculate actionability score for each ticker based on multiple heuristics.

        Higher score = more likely to move = better candidate for model training.

        Score components (total = 100%):
        1. Volatility (25%) - Standard deviation of daily returns
        2. Volume Spike (15%) - Unusual volume compared to average
        3. Price Momentum (15%) - Recent returns (last 5 days)
        4. Gap Analysis (10%) - Price gaps (gaps often fill)
        5. News Recency (15%) - How recent is the news
        6. News Severity (10%) - How strong is the sentiment
        7. News Frequency (10%) - Number of news articles

        Args:
            tickers: List of ticker symbols
            days: Number of days to look back for data

        Returns:
            Dict mapping ticker -> actionability score
        """
        from datetime import timedelta

        scores = {}
        now = datetime.now()

        try:
            from src.news.database import NewsDatabase
            from src.data import fetch_yahoo_data

            db = NewsDatabase("./data/news.db")

            # Fetch recent price data for all tickers
            logger.info(f"Fetching price data for {len(tickers)} tickers...")
            start_date = (now - timedelta(days=days + 10)).strftime("%Y-%m-%d")  # Extra days for momentum
            price_data = fetch_yahoo_data(tickers, start_date=start_date, progress=False)

            # Calculate various metrics for each ticker
            volatilities = {}
            volume_spikes = {}
            momentum_scores = {}
            gap_scores = {}

            if not price_data.empty:
                for ticker in tickers:
                    ticker_data = price_data[price_data['ticker'] == ticker].copy()
                    if len(ticker_data) >= 20:  # Need sufficient data
                        ticker_data = ticker_data.sort_index()

                        # 1. Volatility (annualized)
                        returns = ticker_data['Close'].pct_change().dropna()
                        if len(returns) >= 10:
                            vol = returns.std() * (252 ** 0.5)
                            volatilities[ticker] = vol

                        # 2. Volume Spike (today vs 20-day avg)
                        if 'Volume' in ticker_data.columns and len(ticker_data) >= 21:
                            recent_vol = ticker_data['Volume'].iloc[-1]
                            avg_vol = ticker_data['Volume'].iloc[-21:-1].mean()
                            if avg_vol > 0:
                                volume_spikes[ticker] = min(recent_vol / avg_vol, 5.0) / 5.0  # Cap at 5x

                        # 3. Price Momentum (last 5 days return)
                        if len(ticker_data) >= 6:
                            recent_price = ticker_data['Close'].iloc[-1]
                            past_price = ticker_data['Close'].iloc[-6]
                            if past_price > 0:
                                momentum = (recent_price - past_price) / past_price
                                momentum_scores[ticker] = min(abs(momentum) * 10, 1.0)  # Scale

                        # 4. Gap Analysis (look for gaps > 2%)
                        if len(ticker_data) >= 2:
                            gaps = 0
                            for i in range(-min(5, len(ticker_data)-1), 0):
                                prev_close = ticker_data['Close'].iloc[i-1]
                                open_price = ticker_data['Open'].iloc[i]
                                if prev_close > 0:
                                    gap = abs(open_price - prev_close) / prev_close
                                    if gap > 0.02:  # Gap > 2%
                                        gaps += 1
                            gap_scores[ticker] = min(gaps / 3, 1.0)  # Max 3 gaps

            logger.info(f"Calculated price metrics for {len(volatilities)} tickers")

            # Get news sentiment for each ticker
            news_metrics = {}
            for ticker in tickers:
                try:
                    articles = db.get_ticker_history(ticker, days=days)
                    if articles:
                        # News Recency: hours since most recent article
                        latest_article = None
                        for a in articles:
                            if a.published_date:
                                try:
                                    pub_date = datetime.fromisoformat(a.published_date.replace('Z', '+00:00'))
                                    if latest_article is None or pub_date > latest_article:
                                        latest_article = pub_date
                                except:
                                    pass

                        if latest_article:
                            hours_since = (now - latest_article).total_seconds() / 3600
                            recency_score = max(0, 1 - hours_since / 72)  # 0 after 72 hours
                        else:
                            recency_score = 0

                        # News Severity: average absolute sentiment score
                        scores_list = [abs(a.score) for a in articles if a.score is not None]
                        if scores_list:
                            avg_severity = sum(scores_list) / len(scores_list)
                            severity_score = min(avg_severity / 30, 1.0)
                        else:
                            severity_score = 0

                        # News Frequency: number of articles (log scale)
                        freq_score = min(len(articles) / 20, 1.0)

                        news_metrics[ticker] = {
                            'recency': recency_score,
                            'severity': severity_score,
                            'frequency': freq_score
                        }
                except Exception as e:
                    logger.debug(f"Could not get news for {ticker}: {e}")

            logger.info(f"Calculated news metrics for {len(news_metrics)} tickers")

            # Normalize and combine scores
            max_vol = max(volatilities.values()) if volatilities else 1.0
            max_momentum = max(momentum_scores.values()) if momentum_scores else 1.0

            for ticker in tickers:
                # 1. Volatility (25%)
                vol_score = (volatilities.get(ticker, 0) / max_vol) if max_vol > 0 else 0
                vol_component = vol_score * 0.25

                # 2. Volume Spike (15%)
                vol_spike_component = volume_spikes.get(ticker, 0) * 0.15

                # 3. Price Momentum (15%)
                momentum_component = (momentum_scores.get(ticker, 0) / max_momentum) * 0.15 if max_momentum > 0 else 0

                # 4. Gap Analysis (10%)
                gap_component = gap_scores.get(ticker, 0) * 0.10

                # 5. News Recency (15%)
                recency_component = news_metrics.get(ticker, {}).get('recency', 0) * 0.15

                # 6. News Severity (10%)
                severity_component = news_metrics.get(ticker, {}).get('severity', 0) * 0.10

                # 7. News Frequency (10%)
                frequency_component = news_metrics.get(ticker, {}).get('frequency', 0) * 0.10

                # Total actionability score
                total_score = (vol_component + vol_spike_component + momentum_component +
                              gap_component + recency_component + severity_component + frequency_component)
                scores[ticker] = total_score

            return scores

        except Exception as e:
            logger.error(f"Error calculating ticker actionability: {e}")
            # Fallback: equal score for all
            return {t: 1.0 / len(tickers) for t in tickers}

    def _select_top_tickers(
        self,
        tickers: List[str],
        max_tickers: int = 7,
        days: int = 30
    ) -> List[str]:
        """
        Select top tickers based on actionability score.

        Args:
            tickers: List of candidate tickers
            max_tickers: Maximum number of tickers to select
            days: Days to look back for calculations

        Returns:
            List of top ticker symbols
        """
        # Check for manual ticker override
        manual_tickers = self._get_manual_tickers()
        if manual_tickers:
            logger.info(f"Using manual ticker override: {manual_tickers}")
            return manual_tickers[:max_tickers]

        logger.info(f"Selecting top {max_tickers} tickers from {len(tickers)} candidates...")

        # Calculate actionability scores
        scores = self._calculate_ticker_actionability(tickers, days)

        # Sort by score descending
        sorted_tickers = sorted(scores.items(), key=lambda x: x[1], reverse=True)

        # Log top candidates
        logger.info("Top 10 actionable tickers:")
        for i, (ticker, score) in enumerate(sorted_tickers[:10]):
            vol = scores.get(ticker, 0)
            logger.info(f"  {i+1}. {ticker}: score={score:.3f}")

        # Select top N
        top_tickers = [t for t, _ in sorted_tickers[:max_tickers]]
        logger.info(f"Selected {len(top_tickers)} tickers for training: {top_tickers}")

        return top_tickers

    def _get_manual_tickers(self) -> List[str]:
        """
        Check for manual ticker override file.

        Returns:
            List of manually selected tickers, or empty list if not set
        """
        manual_file = Path("./data/manual_tickers.txt")
        if manual_file.exists():
            try:
                with open(manual_file, 'r') as f:
                    content = f.read().strip()
                    if content:
                        tickers = [t.strip().upper() for t in content.split(',') if t.strip()]
                        logger.info(f"Found manual tickers override: {tickers}")
                        return tickers
            except Exception as e:
                logger.warning(f"Could not read manual tickers file: {e}")
        return []

    def _save_selected_tickers(self, tickers: List[str]):
        """Save selected tickers to a file for C++ to read."""
        try:
            selected_file = "./models/selected_tickers.txt"
            with open(selected_file, 'w') as f:
                f.write(",".join(tickers))
            logger.info(f"Saved selected tickers to {selected_file}")
        except Exception as e:
            logger.error(f"Failed to save selected tickers: {e}")

    def _create_reload_signal(self):
        """Create a signal file to notify C++ to reload models."""
        try:
            signal_file = "./models/reload.signal"
            with open(signal_file, 'w') as f:
                f.write(datetime.now().isoformat())
            logger.info(f"Created reload signal file: {signal_file}")
        except Exception as e:
            logger.error(f"Failed to create reload signal: {e}")

    def _copy_model_to_cpp(self):
        """Copy trained ONNX model to C++ directory for use by trading bot."""
        try:
            import shutil

            # Paths
            python_models_dir = Path("./models")
            cpp_models_dir = Path("../Trading_cpp/models")

            # Check if model exists
            model_source = python_models_dir / "trading_model.onnx"
            if not model_source.exists():
                # Try alternative name
                model_source = python_models_dir / "stock_predictor.onnx"

            if not model_source.exists():
                logger.warning(f"No ONNX model found to copy to C++: {model_source}")
                return

            # Create C++ models directory if needed
            cpp_models_dir.mkdir(parents=True, exist_ok=True)

            # Copy model with standardized name for C++
            model_dest = cpp_models_dir / "stock_predictor.onnx"
            shutil.copy2(model_source, model_dest)
            logger.info(f"Copied ONNX model to C++: {model_dest}")

            # Also copy cluster models if they exist
            cluster_dir = python_models_dir / "clusters"
            if cluster_dir.exists():
                cpp_cluster_dir = cpp_models_dir / "clusters"
                cpp_cluster_dir.mkdir(parents=True, exist_ok=True)

                for cluster_file in cluster_dir.glob("*.onnx"):
                    dest_file = cpp_cluster_dir / cluster_file.name
                    shutil.copy2(cluster_file, dest_file)
                    logger.info(f"Copied cluster model to C++: {dest_file}")

        except Exception as e:
            logger.warning(f"Failed to copy model to C++ directory: {e}")

    def select_and_train_tickers(self) -> Dict:
        """
        Select top 7 actionable tickers and train models for them.

        This is called at system startup to ensure dynamic ticker selection.
        It only runs once per trading day to avoid unnecessary retraining.

        Returns:
            Dict with selection and training results
        """
        logger.info("=" * 50)
        logger.info("Running startup ticker selection and training...")
        logger.info("=" * 50)

        try:
            from src.service.ticker_manager import get_ticker_manager

            # Check if already run today (to avoid re-running on reload)
            selection_date_file = Path("./data/last_ticker_selection.txt")
            today = datetime.now().strftime("%Y-%m-%d")

            if selection_date_file.exists():
                last_date = selection_date_file.read_text().strip()
                if last_date == today:
                    logger.info(f"Ticker selection already ran today ({today}), skipping")
                    return {
                        "status": "skipped",
                        "reason": "already_ran_today",
                        "date": today
                    }

            # 1. Load candidate tickers from C++
            from src.data import load_cpp_tickers
            all_tickers = load_cpp_tickers()
            logger.info(f"Loaded {len(all_tickers)} candidate tickers from C++")

            if not all_tickers:
                logger.warning("No tickers available from C++")
                return {
                    "status": "error",
                    "reason": "no_tickers_available"
                }

            # 2. Select top 7 actionable tickers
            selected_tickers = self._select_top_tickers(all_tickers, max_tickers=7, days=30)

            if not selected_tickers:
                logger.warning("No actionable tickers selected")
                return {
                    "status": "error",
                    "reason": "no_tickers_selected"
                }

            # 3. Save selected tickers for C++ to read
            self._save_selected_tickers(selected_tickers)

            # 4. Train models for selected tickers
            manager = get_ticker_manager()
            result = manager.set_selected_tickers(selected_tickers)

            # 5. Record today's date to prevent re-running
            selection_date_file.parent.mkdir(parents=True, exist_ok=True)
            selection_date_file.write_text(today)

            logger.info("=" * 50)
            logger.info(f"Startup ticker selection complete: {selected_tickers}")
            logger.info(f"Trained models: {result.get('trained', [])}")
            logger.info("=" * 50)

            return {
                "status": "success",
                "tickers": selected_tickers,
                "trained": result.get('trained', []),
                "date": today
            }

        except Exception as e:
            logger.error(f"Startup ticker selection failed: {e}")
            return {
                "status": "error",
                "reason": str(e)
            }

    def start(self):
        """Start the scheduler"""
        logger.info("Starting background scheduler...")
        self.scheduler.start()
        logger.info("Scheduler started successfully")

    def shutdown(self):
        """Stop the scheduler"""
        logger.info("Stopping background scheduler...")
        self.scheduler.shutdown()
        logger.info("Scheduler stopped")

    def _write_notification(self, ticker: str, old_signal: dict, new_signal: dict,
                          is_portfolio: bool = False, is_selected: bool = False):
        """Write notification to file for C++ listener to send."""
        import json
        from pathlib import Path

        try:
            notification_file = Path("./data/pending_notifications.json")
            notifications = []

            # Load existing if present
            if notification_file.exists():
                with open(notification_file, 'r') as f:
                    notifications = json.load(f)

            # Format message based on ticker type
            new_type = new_signal.get('signal', 'hold')
            price = new_signal.get('price')
            confidence = new_signal.get('confidence', 0) * 100
            sentiment = new_signal.get('sentiment_score', 0)
            old_type = old_signal.get('signal', 'hold') if old_signal else 'none'

            if is_portfolio and is_selected:
                # Overlap: portfolio holding that's also a promising candidate
                emoji = "📊"
                msg = f"{emoji} <b>Portfolio Rebalance: {new_type.upper()} {ticker}</b>\n"
                msg += f"⭐ Also a top promising candidate\n"
            elif is_portfolio:
                emoji = "📊"
                msg = f"{emoji} <b>Portfolio Rebalance: {new_type.upper()} {ticker}</b>\n"
            else:
                emoji = "💡"
                msg = f"{emoji} <b>New Opportunity: {new_type.upper()} {ticker}</b>\n"

            msg += f"Price: ${price:.2f}\n" if price else ""
            msg += f"Confidence: {confidence:.0f}%\n"
            msg += f"Sentiment: {sentiment:+.2f}\n"
            msg += f"Changed from: {old_type.upper()}"

            notifications.append({
                "ticker": ticker,
                "message": msg,
                "timestamp": datetime.now().isoformat(),
                "is_portfolio": is_portfolio,
                "is_selected": is_selected
            })

            with open(notification_file, 'w') as f:
                json.dump(notifications, f, indent=2)

            logger.info(f"Wrote notification for {ticker} to pending_notifications.json (portfolio={is_portfolio}, selected={is_selected})")

        except Exception as e:
            logger.warning(f"Failed to write notification: {e}")

    def get_jobs(self):
        """Get list of scheduled jobs"""
        jobs = []
        for job in self.scheduler.get_jobs():
            jobs.append({
                "id": job.id,
                "name": job.name,
                "next_run": str(job.next_run_time) if job.next_run_time else None
            })
        return jobs


# Global scheduler instance
_scheduler: SchedulerService = None


def get_scheduler() -> SchedulerService:
    """Get the global scheduler instance"""
    global _scheduler
    if _scheduler is None:
        _scheduler = SchedulerService()
    return _scheduler


def start_scheduler():
    """Start the background scheduler"""
    scheduler = get_scheduler()
    scheduler.start()
    return scheduler


def stop_scheduler():
    """Stop the background scheduler"""
    global _scheduler
    if _scheduler:
        _scheduler.shutdown()
        _scheduler = None


if __name__ == "__main__":
    # Test the scheduler
    logging.basicConfig(level=logging.INFO)

    scheduler = get_scheduler()
    scheduler.start()

    print("Scheduler started. Jobs:")
    for job in scheduler.get_jobs():
        print(f"  - {job['name']}: {job['next_run']}")

    print("\nPress Ctrl+C to stop")

    try:
        import time
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        scheduler.shutdown()
