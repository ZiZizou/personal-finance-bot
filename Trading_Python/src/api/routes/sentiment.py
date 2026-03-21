#!/usr/bin/env python
"""Sentiment endpoints for providing news sentiment to Trading-CPP"""

import os
import sys
from datetime import datetime, timedelta
from typing import List, Optional, Dict, Any
from fastapi import APIRouter, Query, HTTPException
from pydantic import BaseModel

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

from src.news.database import NewsDatabase
from src.news.scraper import NewsScraper

logger = None


def get_logger():
    global logger
    if logger is None:
        import logging
        logging.basicConfig(level=logging.INFO)
        logger = logging.getLogger(__name__)
    return logger


router = APIRouter()


# Sentiment data models
class Sentiment(BaseModel):
    ticker: str
    timestamp: str
    sentiment_score: float  # -1.0 to 1.0
    confidence: float  # 0.0 to 1.0
    article_count: int
    headline: Optional[str] = None
    source: str = "python_api"


class SentimentHistory(BaseModel):
    ticker: str
    sentiments: List[Sentiment]
    average_sentiment: float
    count: int


# Sentiment cache
_sentiment_cache: Dict[str, Sentiment] = {}

# Database path - configurable via environment variable
NEWS_DB_PATH = os.environ.get("NEWS_DB_PATH", "./data/news.db")


def _get_real_sentiment(ticker: str, days: int = 7) -> Optional[Sentiment]:
    """
    Get real sentiment from news database.

    Args:
        ticker: Stock ticker symbol
        days: Number of days to look back

    Returns:
        Sentiment object if articles found, None otherwise
    """
    try:
        # Query news database for recent articles
        db = NewsDatabase(NEWS_DB_PATH)
        articles = db.get_ticker_history(ticker.upper(), days=days)

        if not articles:
            return None

        # Calculate average sentiment from article scores
        # Article scores range from approximately -30 to +30
        # Normalize to -1.0 to 1.0 range
        scores = [a.score for a in articles if a.score is not None]

        if not scores:
            return None

        avg_score = sum(scores) / len(scores)

        # Normalize to -1.0 to 1.0 (assuming max score of 30)
        normalized_score = max(-1.0, min(1.0, avg_score / 30.0))

        # Calculate confidence based on article count
        # More articles = higher confidence, max at 10 articles
        confidence = min(len(articles) / 10, 1.0)

        # Get the most recent headline for reference
        headline = articles[0].title if articles else None

        # Determine source based on data availability
        source = "news_db"

        return Sentiment(
            ticker=ticker.upper(),
            timestamp=datetime.now().isoformat(),
            sentiment_score=normalized_score,
            confidence=confidence,
            article_count=len(articles),
            headline=headline[:200] if headline else None,  # Truncate long headlines
            source=source
        )

    except Exception as e:
        log = get_logger()
        log.warning(f"Error getting real sentiment for {ticker}: {e}")
        return None


def _fetch_fresh_news_sentiment(ticker: str, days: int = 7) -> Optional[Sentiment]:
    """
    Fetch fresh news and calculate sentiment.

    Args:
        ticker: Stock ticker symbol
        days: Number of days to look back

    Returns:
        Sentiment object if articles found, None otherwise
    """
    try:
        log = get_logger()
        log.info(f"Fetching fresh news for {ticker}")

        scraper = NewsScraper(db_path=NEWS_DB_PATH)
        articles = scraper.fetch_all()

        # Filter articles for this ticker
        ticker_articles = [a for a in articles if a.ticker and a.ticker.upper() == ticker.upper()]

        if not ticker_articles:
            return None

        # Calculate average sentiment
        scores = [a.score for a in ticker_articles if a.score is not None]

        if not scores:
            return None

        avg_score = sum(scores) / len(scores)
        normalized_score = max(-1.0, min(1.0, avg_score / 30.0))
        confidence = min(len(ticker_articles) / 10, 1.0)

        headline = ticker_articles[0].title if ticker_articles else None

        return Sentiment(
            ticker=ticker.upper(),
            timestamp=datetime.now().isoformat(),
            sentiment_score=normalized_score,
            confidence=confidence,
            article_count=len(ticker_articles),
            headline=headline[:200] if headline else None,
            source="fresh_news"
        )

    except Exception as e:
        log = get_logger()
        log.warning(f"Error fetching fresh news for {ticker}: {e}")
        return None


def _generate_mock_sentiment(ticker: str) -> Sentiment:
    """Generate mock sentiment data as fallback"""
    import random

    score = random.uniform(-0.8, 0.8)

    headlines = [
        f"{ticker} reports strong quarterly earnings",
        f"Analysts upgrade {ticker} to buy rating",
        f"{ticker} announces new product launch",
        f"Market volatility affects {ticker} stock",
        f"{ticker} faces regulatory scrutiny",
    ]

    return Sentiment(
        ticker=ticker.upper(),
        timestamp=datetime.now().isoformat(),
        sentiment_score=score,
        confidence=random.uniform(0.5, 0.95),
        article_count=random.randint(1, 20),
        headline=random.choice(headlines),
        source="mock_fallback"
    )


@router.get("/sentiment/{ticker}", response_model=Sentiment)
async def get_sentiment(ticker: str, days: int = Query(7, ge=1, le=30)):
    """
    Get current sentiment for a ticker.

    Returns the aggregated sentiment from recent news articles.
    """
    log = get_logger()
    ticker = ticker.upper()

    log.info(f"Fetching sentiment for {ticker} (last {days} days)")

    # Check cache
    cache_key = f"{ticker}_{days}"
    if cache_key in _sentiment_cache:
        cached = _sentiment_cache[cache_key]
        # Check if cache is fresh (within 1 hour)
        cached_time = datetime.fromisoformat(cached.timestamp)
        if (datetime.now() - cached_time).total_seconds() < 3600:
            return cached

    # Try to get real sentiment from database
    sentiment = _get_real_sentiment(ticker, days)

    # Fallback: try fetching fresh news
    if sentiment is None:
        log.info(f"No cached articles for {ticker}, trying fresh news fetch")
        sentiment = _fetch_fresh_news_sentiment(ticker, days)

    # Final fallback: generate mock sentiment
    if sentiment is None:
        log.warning(f"No news available for {ticker}, using mock data")
        sentiment = _generate_mock_sentiment(ticker)

    # Cache it
    _sentiment_cache[cache_key] = sentiment

    return sentiment


@router.get("/sentiment/{ticker}/history", response_model=SentimentHistory)
async def get_sentiment_history(
    ticker: str,
    days: int = Query(30, ge=1, le=90),
    limit: int = Query(10, ge=1, le=50)
):
    """
    Get historical sentiment data for a ticker.

    Returns a list of past sentiment readings.
    """
    log = get_logger()
    ticker = ticker.upper()

    log.info(f"Fetching sentiment history for {ticker} (last {days} days)")

    # Generate mock historical data
    sentiments = []
    for i in range(min(limit, days)):
        date = datetime.now() - timedelta(days=i)
        s = _generate_mock_sentiment(ticker)
        s.timestamp = date.isoformat()
        sentiments.append(s)

    # Calculate average
    avg_sentiment = sum(s.sentiment_score for s in sentiments) / len(sentiments)

    return SentimentHistory(
        ticker=ticker,
        sentiments=sentiments,
        average_sentiment=avg_sentiment,
        count=len(sentiments)
    )


@router.get("/batch/sentiment")
async def get_batch_sentiment(
    symbols: str = Query(..., description="Comma-separated list of tickers"),
    days: int = Query(7, ge=1, le=30)
):
    """
    Get sentiment for multiple tickers in a single request.
    """
    log = get_logger()

    # Parse symbols
    tickers = [s.strip().upper() for s in symbols.split(",") if s.strip()]

    if not tickers:
        raise HTTPException(status_code=400, detail="No symbols provided")

    if len(tickers) > 50:
        raise HTTPException(status_code=400, detail="Maximum 50 symbols per request")

    log.info(f"Fetching batch sentiment for {len(tickers)} tickers")

    # Try to get real sentiment for each ticker
    sentiments = []
    for t in tickers:
        sentiment = _get_real_sentiment(t, days)
        if sentiment is None:
            sentiment = _fetch_fresh_news_sentiment(t, days)
        if sentiment is None:
            sentiment = _generate_mock_sentiment(t)
        sentiments.append(sentiment)

    return {
        "sentiments": sentiments,
        "timestamp": datetime.now().isoformat(),
        "count": len(sentiments)
    }
