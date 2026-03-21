#!/usr/bin/env python
"""
News scraper CLI for Trading-Python.
Usage: python scripts/scrape_news.py [options]
"""

import argparse
import os
import sys
from datetime import datetime

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.news import (
    NewsScraper,
    NewsDatabase,
    save_to_csv,
    export_watchlist,
    export_avoidlist,
    INDUSTRIES
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Scrape and analyze industry news for trading signals"
    )

    # Scraping options
    parser.add_argument(
        "--update", "-u",
        action="store_true",
        help="Fetch and update news database"
    )
    parser.add_argument(
        "--min-score",
        type=int,
        default=1,
        help="Minimum article score to store (default: 1)"
    )
    parser.add_argument(
        "--max-articles",
        type=int,
        default=20,
        help="Max articles per source (default: 20)"
    )
    parser.add_argument(
        "--industries",
        type=str,
        default=None,
        help="Comma-separated list of industries to scrape"
    )

    # Query options
    parser.add_argument(
        "--top-promising",
        action="store_true",
        help="Show top promising tickers"
    )
    parser.add_argument(
        "--avoid",
        action="store_true",
        help="Show tickers to avoid"
    )
    parser.add_argument(
        "--ticker",
        type=str,
        help="Show news history for a specific ticker"
    )
    parser.add_argument(
        "--days",
        type=int,
        default=30,
        help="Days to look back (default: 30)"
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Max number of tickers to show (default: 20)"
    )
    parser.add_argument(
        "--min-articles",
        type=int,
        default=1,
        help="Minimum articles for watchlist/avoidlist (default: 1)"
    )

    # Export options
    parser.add_argument(
        "--export",
        type=str,
        help="Export all articles to CSV"
    )
    parser.add_argument(
        "--export-watchlist",
        action="store_true",
        help="Export watchlist to CSV"
    )
    parser.add_argument(
        "--export-avoidlist",
        action="store_true",
        help="Export avoidlist to CSV"
    )

    # Database options
    parser.add_argument(
        "--db-path",
        type=str,
        default="./data/news.db",
        help="Path to news database"
    )

    # Utility options
    parser.add_argument(
        "--stats",
        action="store_true",
        help="Show database statistics"
    )
    parser.add_argument(
        "--cleanup",
        action="store_true",
        help="Clean up old articles from database"
    )

    return parser.parse_args()


def print_header(text: str):
    print(f"\n{'='*60}")
    print(f"  {text}")
    print('='*60)


def main():
    args = parse_args()

    # Initialize scraper
    scraper = NewsScraper(
        db_path=args.db_path,
        min_score_threshold=args.min_score,
        max_articles_per_source=args.max_articles
    )

    # Parse industries
    industries = None
    if args.industries:
        industries = [i.strip() for i in args.industries.split(',')]

    # === UPDATE DATABASE ===
    if args.update:
        print_header("Fetching News")

        if industries:
            print(f"Industries: {', '.join(industries)}")
        else:
            print("All industries")

        articles = scraper.run(
            industries=industries,
            update_db=True,
            cleanup=args.cleanup
        )

        # Summary
        promising = sum(1 for a in articles if a.classification == "promising")
        bad = sum(1 for a in articles if a.classification == "bad")
        with_ticker = sum(1 for a in articles if a.ticker)

        print(f"\nFetched {len(articles)} articles:")
        print(f"  - Promising: {promising}")
        print(f"  - Bad: {bad}")
        print(f"  - With valid ticker: {with_ticker}")

    # === SHOW TOP PROMISING ===
    if args.top_promising:
        print_header(f"Top Promising Tickers (last {args.days} days)")

        db = NewsDatabase(args.db_path)
        top = db.get_top_tickers(
            min_score=0,
            days=args.days,
            limit=args.limit,
            classification="promising"
        )

        if not top:
            print("No promising tickers found")
        else:
            print(f"{'Ticker':<10} {'Score':>8} {'Promising':>10} {'Bad':>6} {'Company'}")
            print("-" * 60)
            for ts in top:
                details = db.get_ticker_details(ts.ticker, args.days)
                company = details.company[:20] if details else ""
                promising = details.promising_count if details else 0
                bad = details.bad_count if details else 0
                print(f"{ts.ticker:<10} {ts.total_score:>8.1f} {promising:>10} {bad:>6} {company}")

    # === SHOW AVOID LIST ===
    if args.avoid:
        print_header(f"Tickers to Avoid (last {args.days} days)")

        db = NewsDatabase(args.db_path)
        avoid = db.get_avoidlist(
            min_bad=args.min_articles,
            days=args.days
        )

        if not avoid:
            print("No tickers to avoid found")
        else:
            # Get details for each
            print(f"{'Ticker':<10} {'Score':>8} {'Bad':>10} {'Promising':>10} {'Company'}")
            print("-" * 60)
            for ticker in avoid[:args.limit]:
                details = db.get_ticker_details(ticker, args.days)
                if details:
                    company = details.company[:20] if details.company else ""
                    promising = details.promising_count
                    bad = details.bad_count
                    print(f"{ticker:<10} {details.total_score:>8.1f} {bad:>10} {promising:>10} {company}")

    # === TICKER HISTORY ===
    if args.ticker:
        print_header(f"News History for {args.ticker.upper()} (last {args.days} days)")

        db = NewsDatabase(args.db_path)
        details = db.get_ticker_details(args.ticker.upper(), args.days)

        if not details or not details.articles:
            print(f"No news found for {args.ticker.upper()}")
        else:
            print(f"\nSummary:")
            print(f"  Score: {details.total_score:.1f}")
            print(f"  Promising: {details.promising_count}")
            print(f"  Bad: {details.bad_count}")
            print(f"  Neutral: {details.neutral_count}")
            print(f"\nRecent Articles:")

            for article in details.articles[:10]:
                score_str = f"+{article.score:.0f}" if article.score > 0 else f"{article.score:.0f}"
                print(f"\n  [{score_str:>4}] {article.title[:70]}")
                print(f"       {article.source} | {article.published_date or 'N/A'}")

    # === EXPORT TO CSV ===
    if args.export:
        print_header("Exporting Articles")

        db = NewsDatabase(args.db_path)

        # Get all articles from last N days
        from datetime import timedelta
        cutoff = (datetime.now() - timedelta(days=args.days)).isoformat()

        conn = sqlite3.connect(args.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        cursor.execute(
            "SELECT * FROM articles WHERE published_date >= ? ORDER BY published_date DESC",
            (cutoff,)
        )
        rows = cursor.fetchall()
        conn.close()

        from src.news.database import Article
        articles = []
        for row in rows:
            articles.append(Article(
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
            ))

        count = save_to_csv(articles, args.export)
        print(f"Exported {count} articles to {args.export}")

    # === EXPORT WATCHLIST ===
    if args.export_watchlist:
        print_header("Exporting Watchlist")
        export_watchlist(args.db_path, "./data/watchlist.csv")

    # === EXPORT AVOIDLIST ===
    if args.export_avoidlist:
        print_header("Exporting Avoidlist")
        export_avoidlist(args.db_path, "./data/avoidlist.csv")

    # === STATS ===
    if args.stats:
        print_header("Database Statistics")

        db = NewsDatabase(args.db_path)
        stats = db.get_stats()

        print(f"Total articles: {stats['total_articles']}")
        print(f"Unique tickers: {stats['unique_tickers']}")
        print(f"Date range: {stats['date_range'][0] or 'N/A'} to {stats['date_range'][1] or 'N/A'}")
        print(f"\nBy classification:")
        for cls, count in stats['by_classification'].items():
            print(f"  {cls}: {count}")

    # === CLEANUP ===
    if args.cleanup and not args.update:
        print_header("Cleaning up old articles")
        db = NewsDatabase(args.db_path)
        removed = db.cleanup_old_articles()
        print(f"Removed {removed} old articles")

    # === NO OPTIONS ===
    if not any([
        args.update,
        args.top_promising,
        args.avoid,
        args.ticker,
        args.export,
        args.export_watchlist,
        args.export_avoidlist,
        args.stats,
        args.cleanup
    ]):
        print("No action specified. Use --help for options.")
        print("\nExamples:")
        print("  python scripts/scrape_news.py --update")
        print("  python scripts/scrape_news.py --top-promising")
        print("  python scripts/scrape_news.py --ticker NVDA --days 30")
        print("  python scripts/scrape_news.py --stats")


if __name__ == "__main__":
    import sqlite3  # For export function
    main()
