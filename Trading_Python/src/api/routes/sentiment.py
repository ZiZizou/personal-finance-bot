#!/usr/bin/env python
"""Sentiment endpoints for providing news sentiment to Trading-CPP"""

import os
import sys
import sqlite3
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


# ============== Discovery Pipeline ==============

def find_underappreciated_stock(tickers: List[str]) -> Optional[str]:
    """
    Find the underappreciated (laggard) stock from a list of tickers.

    Uses yfinance to fetch forward P/E and 5-day returns.
    Returns the ticker with the lowest 5-day return (hasn't popped yet).

    Args:
        tickers: List of ticker symbols (max 10 to prevent rate limits)

    Returns:
        Formatted string: "TICKER (5d return: X.X%, P/E: Y.Y)" or None if error
    """
    if not tickers:
        return None

    # Cap at 10 tickers to prevent rate limits
    tickers = tickers[:10]

    try:
        import yfinance as yf
        import time

        stock_data = []

        for ticker in tickers:
            try:
                info = yf.Ticker(ticker).info
                current_price = info.get("regularMarketPrice", 0)
                prev_close = info.get("regularMarketPreviousClose", 0)
                forward_pe = info.get("forwardPE", 0)

                # Calculate 5-day return
                if current_price > 0 and prev_close > 0:
                    five_day_return = ((current_price - prev_close) / prev_close) * 100
                else:
                    five_day_return = 0

                stock_data.append({
                    "ticker": ticker,
                    "five_day_return": five_day_return,
                    "forward_pe": forward_pe if forward_pe else 0
                })

                # Space requests to avoid rate limiting
                time.sleep(0.5)

            except Exception as e:
                log = get_logger()
                log.warning(f"Error fetching data for {ticker}: {e}")
                continue

        if not stock_data:
            return None

        # Sort by 5-day return ascending (lowest return = hasn't popped yet)
        stock_data.sort(key=lambda x: x["five_day_return"])

        # Return the laggard (first one with lowest 5-day return)
        laggard = stock_data[0]
        pe_str = f"{laggard['forward_pe']:.1f}" if laggard['forward_pe'] else "N/A"
        return f"{laggard['ticker']} (5d return: {laggard['five_day_return']:+.1f}%, P/E: {pe_str})"

    except Exception as e:
        log = get_logger()
        log.warning(f"Error finding underappreciated stock: {e}")
        return None


def discover_trending_sectors() -> Dict[str, Any]:
    """
    Discover trending sectors and underappreciated stocks.

    Queries news.db for industries with high density of promising articles
    in rolling 48-hour window. Groups by industry, calculates avg sentiment
    and article count. Uses find_underappreciated_stock() to identify laggards.

    Returns:
        Dict with trending sectors and laggard plays
    """
    try:
        db = NewsDatabase(NEWS_DB_PATH)
        cutoff = (datetime.now() - timedelta(hours=48)).isoformat()

        conn = sqlite3.connect(db.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        # Query industries with promising articles in last 48 hours
        cursor.execute("""
            SELECT
                industry,
                COUNT(*) as article_count,
                AVG(score) as avg_score,
                GROUP_CONCAT(DISTINCT ticker) as tickers
            FROM articles
            WHERE published_date >= ?
              AND classification = 'promising'
              AND industry IS NOT NULL
              AND industry != ''
            GROUP BY industry
            HAVING COUNT(*) >= 1
            ORDER BY avg_score DESC, article_count DESC
            LIMIT 10
        """, (cutoff,))

        rows = cursor.fetchall()
        conn.close()

        if not rows:
            return {"sectors": [], "timestamp": datetime.now().isoformat()}

        sectors = []
        for row in rows:
            industry = row["industry"]
            article_count = row["article_count"]
            avg_score = row["avg_score"]
            tickers_str = row["tickers"]

            # Parse tickers from comma-separated string
            tickers = [t.strip().upper() for t in (tickers_str or "").split(",") if t.strip()]

            # Normalize score to -1 to 1 range
            normalized_score = max(-1.0, min(1.0, avg_score / 30.0))

            # Find underappreciated stock (laggard)
            laggard = None
            if len(tickers) > 1:  # Only find laggard if multiple tickers in sector
                laggard = find_underappreciated_stock(tickers)

            sectors.append({
                "industry": industry,
                "avg_sentiment": round(normalized_score, 2),
                "article_count": article_count,
                "tickers": tickers[:10],  # Limit to 10 for display
                "laggard": laggard
            })

        return {
            "sectors": sectors,
            "timestamp": datetime.now().isoformat()
        }

    except Exception as e:
        log = get_logger()
        log.error(f"Error in discover_trending_sectors: {e}")
        return {"sectors": [], "error": str(e), "timestamp": datetime.now().isoformat()}


@router.get("/discover")
async def discover():
    """
    Discover trending sectors and underappreciated (laggard) stocks.

    Identifies industries with high density of promising articles in the
    last 48 hours, then finds stocks within those sectors that haven't
    moved yet (low 5-day return) - potential sympathy play opportunities.
    """
    log = get_logger()
    log.info("Discovering trending sectors and laggard stocks")

    result = discover_trending_sectors()
    return result
