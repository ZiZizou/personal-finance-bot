"""
Main news scraper that orchestrates fetching, classifying, and storing news.
"""

import time
import logging
from datetime import datetime
from typing import List, Optional
from concurrent.futures import ThreadPoolExecutor, as_completed

try:
    import feedparser
except ImportError:
    raise ImportError("feedparser is required. Install with: uv pip install feedparser")

from .sources import RSSSource, get_all_sources, get_sources_by_industry
from .classifier import ArticleClassifier, Classification
from .extractor import CompanyExtractor
from .ticker_resolver import TickerResolver
from .database import NewsDatabase, Article


logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class NewsScraper:
    """
    Main orchestrator for news scraping pipeline.

    Flow:
    1. Fetch RSS feeds
    2. Classify articles (promising/bad/neutral)
    3. Extract company names
    4. Resolve to tickers
    5. Store in database
    """

    def __init__(
        self,
        db_path: str = "./data/news.db",
        retention_days: int = 90,
        max_articles_per_source: int = 20,
        min_score_threshold: int = 1,
        verify_tickers: bool = True,
        known_companies: Optional[set] = None
    ):
        """
        Initialize news scraper.

        Args:
            db_path: Path to SQLite database
            retention_days: Days to retain articles
            max_articles_per_source: Max articles to fetch per RSS feed
            min_score_threshold: Minimum score to store article
            verify_tickers: Whether to verify tickers exist on NYSE/TSX
            known_companies: Optional set of known company names
        """
        self.db = NewsDatabase(db_path, retention_days)
        self.classifier = ArticleClassifier()
        self.extractor = CompanyExtractor(known_companies)
        self.resolver = TickerResolver()

        self.max_articles = max_articles_per_source
        self.min_score = min_score_threshold
        self.verify_tickers = verify_tickers

    def fetch_feed(self, source: RSSSource) -> List[Article]:
        """
        Fetch and parse a single RSS feed.

        Args:
            source: RSS source to fetch

        Returns:
            List of raw articles (unclassified)
        """
        articles = []

        try:
            logger.info(f"Fetching {source.name}...")
            feed = feedparser.parse(source.url, timeout=10)

            for entry in feed.entries[:self.max_articles]:
                # Parse published date
                published = None
                if hasattr(entry, 'published'):
                    try:
                        published = entry.published
                    except Exception:
                        pass
                elif hasattr(entry, 'updated'):
                    try:
                        published = entry.updated
                    except Exception:
                        pass

                # Get title and description/summary
                title = entry.get('title', '').strip()
                description = entry.get('summary', '') or entry.get('description', '')
                url = entry.get('link', '').strip()

                if title and url:
                    articles.append(Article(
                        title=title,
                        url=url,
                        source=source.name,
                        industry=source.industry,
                        published_date=published,
                        description=description[:500] if description else None  # Truncate long descriptions
                    ))

            logger.info(f"  -> Found {len(articles)} articles from {source.name}")

        except Exception as e:
            logger.warning(f"Failed to fetch {source.name}: {e}")

        return articles

    def process_article(self, article: Article) -> Article:
        """
        Process a single article: classify, extract company, resolve ticker.

        Args:
            article: Raw article from RSS

        Returns:
            Processed article with classification and ticker
        """
        # 1. Classify
        result = self.classifier.classify(article.title, article.description or "")
        article.classification = result.classification.value
        article.score = result.score

        # Skip if score is too low
        if abs(article.score) < self.min_score:
            return article

        # 2. Extract company name
        companies = self.extractor.extract(
            article.title,
            article.description or ""
        )
        if companies:
            article.company_name = companies[0]  # Use first/primary company

        # 3. Resolve to ticker
        if article.company_name:
            ticker = self.resolver.resolve(
                article.company_name,
                verify=self.verify_tickers
            )
            if ticker:
                article.ticker = ticker

        return article

    def fetch_all(self, industries: Optional[List[str]] = None) -> List[Article]:
        """
        Fetch and process all RSS feeds.

        Args:
            industries: Optional list of industries to filter (e.g., ['semiconductors', 'biotech'])

        Returns:
            List of processed articles
        """
        # Get sources
        if industries:
            sources = []
            for ind in industries:
                sources.extend(get_sources_by_industry(ind))
        else:
            sources = get_all_sources()

        logger.info(f"Fetching from {len(sources)} sources...")

        all_articles = []

        # Fetch feeds (sequentially to avoid rate limiting)
        for source in sources:
            raw_articles = self.fetch_feed(source)

            # Process each article
            for article in raw_articles:
                processed = self.process_article(article)
                # Only keep articles with score above threshold OR have a ticker
                if abs(processed.score) >= self.min_score or processed.ticker:
                    all_articles.append(processed)

            # Small delay between feeds
            time.sleep(0.5)

        logger.info(f"Total processed articles: {len(all_articles)}")
        return all_articles

    def run(
        self,
        industries: Optional[List[str]] = None,
        update_db: bool = True,
        cleanup: bool = True
    ) -> List[Article]:
        """
        Full pipeline: fetch, process, store.

        Args:
            industries: Filter by industries
            update_db: Whether to store in database
            cleanup: Whether to run cleanup after

        Returns:
            List of processed articles
        """
        # Fetch and process
        articles = self.fetch_all(industries)

        # Store in database
        if update_db:
            logger.info("Storing articles in database...")
            count = self.db.add_articles(articles)
            logger.info(f"Stored {count} new articles")

        # Cleanup old articles
        if cleanup:
            logger.info("Cleaning up old articles...")
            removed = self.db.cleanup_old_articles()
            logger.info(f"Removed {removed} old articles")

        return articles

    def get_ticker_sentiment(self, ticker: str, days: int = 30) -> dict:
        """
        Get comprehensive sentiment for a ticker.

        Args:
            ticker: Stock ticker
            days: Days to look back

        Returns:
            Dictionary with sentiment breakdown
        """
        details = self.db.get_ticker_details(ticker, days)
        if not details:
            return {
                "ticker": ticker.upper(),
                "articles": 0,
                "score": 0,
                "sentiment": "neutral"
            }

        return {
            "ticker": details.ticker,
            "articles": details.promising_count + details.bad_count + details.neutral_count,
            "promising": details.promising_count,
            "bad": details.bad_count,
            "neutral": details.neutral_count,
            "score": details.total_score,
            "avg_score": details.avg_score,
            "latest_promising": details.latest_promising,
            "latest_bad": details.latest_bad,
            "sentiment": "positive" if details.total_score > 0 else "negative" if details.total_score < 0 else "neutral"
        }


def main():
    """Test the scraper."""
    scraper = NewsScraper(min_score_threshold=1)
    articles = scraper.run()

    print(f"\nFetched {len(articles)} articles")

    # Show breakdown
    promising = sum(1 for a in articles if a.classification == "promising")
    bad = sum(1 for a in articles if a.classification == "bad")
    neutral = sum(1 for a in articles if a.classification == "neutral")
    with_ticker = sum(1 for a in articles if a.ticker)

    print(f"  Promising: {promising}")
    print(f"  Bad: {bad}")
    print(f"  Neutral: {neutral}")
    print(f"  With Ticker: {with_ticker}")

    # Show top tickers
    print("\nTop tickers by score:")
    db = NewsDatabase()
    top = db.get_top_tickers(limit=5)
    for ts in top:
        print(f"  {ts.ticker}: {ts.total_score:.1f} ({ts.promising_count} good, {ts.bad_count} bad)")


if __name__ == "__main__":
    main()
