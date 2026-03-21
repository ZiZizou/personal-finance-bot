"""
Historical news database using SQLite for caching and cumulative scoring.
"""

import os
import sqlite3
import json
from datetime import datetime, timedelta
from dataclasses import dataclass, asdict
from typing import List, Optional, Dict
from pathlib import Path


@dataclass
class Article:
    """Represents a news article."""
    title: str
    url: str
    source: str
    industry: str
    published_date: Optional[str]
    classification: str  # promising, bad, neutral
    score: float
    company_name: Optional[str] = None
    ticker: Optional[str] = None
    description: Optional[str] = None
    id: Optional[int] = None

    def to_dict(self) -> dict:
        """Convert to dictionary."""
        return {k: v for k, v in asdict(self).items() if v is not None}

    @classmethod
    def from_dict(cls, data: dict) -> 'Article':
        """Create from dictionary."""
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class TickerScore:
    """Aggregated score for a ticker."""
    ticker: str
    company: str
    promising_count: int
    bad_count: int
    neutral_count: int
    total_score: float
    avg_score: float
    latest_promising: Optional[str]
    latest_bad: Optional[str]
    articles: List[Article]


class NewsDatabase:
    """
    SQLite-based historical news storage with cumulative scoring.
    """

    def __init__(self, db_path: str = "./data/news.db", retention_days: int = 90):
        """
        Initialize the news database.

        Args:
            db_path: Path to SQLite database file
            retention_days: Number of days to retain articles
        """
        self.db_path = db_path
        self.retention_days = retention_days

        # Ensure directory exists
        Path(db_path).parent.mkdir(parents=True, exist_ok=True)

        self._init_db()

    def _init_db(self):
        """Initialize the database schema."""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cursor.execute("""
            CREATE TABLE IF NOT EXISTS articles (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                url TEXT UNIQUE NOT NULL,
                source TEXT,
                industry TEXT,
                published_date TEXT,
                classification TEXT,
                score REAL,
                company_name TEXT,
                ticker TEXT,
                description TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)

        # Create indexes
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_ticker ON articles(ticker)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_date ON articles(published_date)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_class ON articles(classification)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_source ON articles(source)")

        conn.commit()
        conn.close()

    def add_article(self, article: Article) -> int:
        """
        Add a new article to the database.

        Args:
            article: Article to add

        Returns:
            Article ID if added, -1 if already exists
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        try:
            cursor.execute("""
                INSERT OR IGNORE INTO articles
                (title, url, source, industry, published_date, classification,
                 score, company_name, ticker, description)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                article.title,
                article.url,
                article.source,
                article.industry,
                article.published_date,
                article.classification,
                article.score,
                article.company_name,
                article.ticker,
                article.description
            ))

            conn.commit()
            result = cursor.lastrowid if cursor.lastrowid else -1
        except sqlite3.Error:
            result = -1
        finally:
            conn.close()

        return result

    def add_articles(self, articles: List[Article]) -> int:
        """
        Add multiple articles to the database.

        Args:
            articles: List of articles to add

        Returns:
            Number of articles added
        """
        count = 0
        for article in articles:
            if self.add_article(article) > 0:
                count += 1
        return count

    def get_article_by_url(self, url: str) -> Optional[Article]:
        """Get article by URL."""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        cursor.execute("SELECT * FROM articles WHERE url = ?", (url,))
        row = cursor.fetchone()
        conn.close()

        if row:
            return Article(
                id=row['id'],
                title=row['title'],
                url=row['url'],
                source=row['source'],
                industry=row['industry'],
                published_date=row['published_date'],
                classification=row['classification'],
                score=row['score'],
                company_name=row['company_name'],
                ticker=row['ticker'],
                description=row['description']
            )
        return None

    def get_ticker_history(self, ticker: str, days: int = 30) -> List[Article]:
        """
        Get news history for a specific ticker.

        Args:
            ticker: Stock ticker
            days: Number of days to look back

        Returns:
            List of articles for the ticker
        """
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

        cursor.execute("""
            SELECT * FROM articles
            WHERE ticker = ? AND published_date >= ?
            ORDER BY published_date DESC
        """, (ticker.upper(), cutoff))

        rows = cursor.fetchall()
        conn.close()

        return [
            Article(
                id=row['id'],
                title=row['title'],
                url=row['url'],
                source=row['source'],
                industry=row['industry'],
                published_date=row['published_date'],
                classification=row['classification'],
                score=row['score'],
                company_name=row['company_name'],
                ticker=row['ticker'],
                description=row['description']
            )
            for row in rows
        ]

    def get_cumulative_score(self, ticker: str, days: int = 30) -> float:
        """
        Calculate cumulative news score for a ticker.

        Args:
            ticker: Stock ticker
            days: Number of days to look back

        Returns:
            Cumulative score (sum of all article scores)
        """
        articles = self.get_ticker_history(ticker, days)
        return sum(a.score for a in articles)

    def get_ticker_details(self, ticker: str, days: int = 30) -> Optional[TickerScore]:
        """
        Get detailed score breakdown for a ticker.

        Args:
            ticker: Stock ticker
            days: Number of days to look back

        Returns:
            TickerScore with breakdown, or None if no articles
        """
        articles = self.get_ticker_history(ticker, days)
        if not articles:
            return None

        promising = [a for a in articles if a.classification == "promising"]
        bad = [a for a in articles if a.classification == "bad"]
        neutral = [a for a in articles if a.classification == "neutral"]

        total_score = sum(a.score for a in articles)
        avg_score = total_score / len(articles) if articles else 0

        latest_promising = max((a.published_date for a in promising), default=None)
        latest_bad = max((a.published_date for a in bad), default=None)

        # Get company name from most recent article
        company = articles[0].company_name if articles else None

        return TickerScore(
            ticker=ticker.upper(),
            company=company or "",
            promising_count=len(promising),
            bad_count=len(bad),
            neutral_count=len(neutral),
            total_score=total_score,
            avg_score=avg_score,
            latest_promising=latest_promising,
            latest_bad=latest_bad,
            articles=articles
        )

    def get_top_tickers(
        self,
        min_score: float = 0,
        days: int = 30,
        limit: int = 20,
        classification: Optional[str] = None
    ) -> List[TickerScore]:
        """
        Get top tickers by cumulative score.

        Args:
            min_score: Minimum cumulative score
            days: Number of days to look back
            limit: Maximum number of tickers to return
            classification: Filter by classification (promising/bad)

        Returns:
            List of TickerScore sorted by total score
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

        if classification:
            query = """
                SELECT ticker,
                       SUM(score) as total_score,
                       COUNT(*) as count,
                       MAX(CASE WHEN classification = 'promising' THEN published_date END) as latest_promising,
                       MAX(CASE WHEN classification = 'bad' THEN published_date END) as latest_bad,
                       MAX(company_name) as company
                FROM articles
                WHERE published_date >= ? AND classification = ?
                GROUP BY ticker
                HAVING total_score >= ?
                ORDER BY total_score DESC
                LIMIT ?
            """
            cursor.execute(query, (cutoff, classification, min_score, limit))
        else:
            query = """
                SELECT ticker,
                       SUM(score) as total_score,
                       COUNT(*) as count,
                       MAX(CASE WHEN classification = 'promising' THEN published_date END) as latest_promising,
                       MAX(CASE WHEN classification = 'bad' THEN published_date END) as latest_bad,
                       MAX(company_name) as company
                FROM articles
                WHERE published_date >= ?
                GROUP BY ticker
                HAVING total_score >= ?
                ORDER BY total_score DESC
                LIMIT ?
            """
            cursor.execute(query, (cutoff, min_score, limit))

        rows = cursor.fetchall()
        conn.close()

        results = []
        for row in rows:
            if row[0]:  # ticker
                results.append(TickerScore(
                    ticker=row[0],
                    company=row[5] or "",
                    promising_count=0,  # Would need separate query
                    bad_count=0,
                    neutral_count=0,
                    total_score=row[1],
                    avg_score=row[1] / row[2] if row[2] else 0,
                    latest_promising=row[3],
                    latest_bad=row[4],
                    articles=[]
                ))

        return results

    def get_watchlist(self, min_promising: int = 2, days: int = 30) -> List[str]:
        """
        Get tickers with multiple promising articles.

        Args:
            min_promising: Minimum number of promising articles
            days: Number of days to look back

        Returns:
            List of ticker symbols
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

        query = """
            SELECT ticker
            FROM articles
            WHERE published_date >= ? AND classification = 'promising'
            GROUP BY ticker
            HAVING COUNT(*) >= ?
            ORDER BY SUM(score) DESC
        """
        cursor.execute(query, (cutoff, min_promising))

        tickers = [row[0] for row in cursor.fetchall() if row[0]]
        conn.close()
        return tickers

    def get_avoidlist(self, min_bad: int = 2, days: int = 30) -> List[str]:
        """
        Get tickers with multiple negative articles.

        Args:
            min_bad: Minimum number of bad articles
            days: Number of days to look back

        Returns:
            List of ticker symbols to avoid
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cutoff = (datetime.now() - timedelta(days=days)).isoformat()

        query = """
            SELECT ticker
            FROM articles
            WHERE published_date >= ? AND classification = 'bad'
            GROUP BY ticker
            HAVING COUNT(*) >= ?
            ORDER BY SUM(score) ASC
        """
        cursor.execute(query, (cutoff, min_bad))

        tickers = [row[0] for row in cursor.fetchall() if row[0]]
        conn.close()
        return tickers

    def cleanup_old_articles(self) -> int:
        """
        Remove articles older than retention period.

        Returns:
            Number of articles removed
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cutoff = (datetime.now() - timedelta(days=self.retention_days)).isoformat()

        cursor.execute("DELETE FROM articles WHERE published_date < ?", (cutoff,))
        count = cursor.rowcount

        conn.commit()
        conn.close()

        return count

    def get_stats(self) -> Dict:
        """Get database statistics."""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        cursor.execute("SELECT COUNT(*) FROM articles")
        total = cursor.fetchone()[0]

        cursor.execute("SELECT COUNT(DISTINCT ticker) FROM articles WHERE ticker IS NOT NULL")
        unique_tickers = cursor.fetchone()[0]

        cursor.execute("""
            SELECT classification, COUNT(*) as count
            FROM articles
            GROUP BY classification
        """)
        by_class = {row[0]: row[1] for row in cursor.fetchall()}

        cursor.execute("SELECT MIN(published_date), MAX(published_date) FROM articles")
        date_range = cursor.fetchone()

        conn.close()

        return {
            "total_articles": total,
            "unique_tickers": unique_tickers,
            "by_classification": by_class,
            "date_range": date_range
        }

    def export_to_csv(self, path: str, days: Optional[int] = None):
        """Export articles to CSV."""
        import csv

        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()

        if days:
            cutoff = (datetime.now() - timedelta(days=days)).isoformat()
            cursor.execute("SELECT * FROM articles WHERE published_date >= ? ORDER BY published_date DESC", (cutoff,))
        else:
            cursor.execute("SELECT * FROM articles ORDER BY published_date DESC")

        rows = cursor.fetchall()
        conn.close()

        # Get column names
        columns = [
            "id", "title", "url", "source", "industry", "published_date",
            "classification", "score", "company_name", "ticker", "description"
        ]

        with open(path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=columns)
            writer.writeheader()
            for row in rows:
                writer.writerow(dict(zip(columns, row)))
