"""
CSV storage utilities for news articles.
"""

import csv
import os
from typing import List, Optional
from pathlib import Path

from .database import Article, NewsDatabase


def save_to_csv(
    articles: List[Article],
    path: str,
    include_raw_fields: bool = True
) -> int:
    """
    Save articles to CSV file.

    Args:
        articles: List of articles to save
        path: Output CSV path
        include_raw_fields: Include title, url, description

    Returns:
        Number of articles saved
    """
    if not articles:
        return 0

    # Ensure directory exists
    Path(path).parent.mkdir(parents=True, exist_ok=True)

    # Define columns
    if include_raw_fields:
        columns = [
            "ticker",
            "company",
            "classification",
            "score",
            "industry",
            "source",
            "date",
            "headline",
            "url",
            "description"
        ]
    else:
        columns = [
            "ticker",
            "company",
            "classification",
            "score",
            "industry",
            "source",
            "date"
        ]

    with open(path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()

        for article in articles:
            row = {
                "ticker": article.ticker or "",
                "company": article.company_name or "",
                "classification": article.classification or "",
                "score": article.score,
                "industry": article.industry or "",
                "source": article.source or "",
                "date": article.published_date or ""
            }

            if include_raw_fields:
                row["headline"] = article.title
                row["url"] = article.url
                row["description"] = article.description or ""

            writer.writerow(row)

    return len(articles)


def load_from_csv(path: str) -> List[Article]:
    """
    Load articles from CSV file.

    Args:
        path: CSV file path

    Returns:
        List of articles
    """
    if not os.path.exists(path):
        return []

    articles = []

    with open(path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)

        for row in reader:
            article = Article(
                title=row.get("headline", ""),
                url=row.get("url", ""),
                source=row.get("source", ""),
                industry=row.get("industry", ""),
                published_date=row.get("date") or None,
                classification=row.get("classification", ""),
                score=float(row.get("score", 0)),
                company_name=row.get("company") or None,
                ticker=row.get("ticker") or None,
                description=row.get("description") or None
            )
            articles.append(article)

    return articles


def export_watchlist(db_path: str = "./data/news.db", output_path: str = "./data/watchlist.csv"):
    """
    Export watchlist (promising tickers) to CSV.

    Args:
        db_path: Path to news database
        output_path: Output CSV path
    """
    db = NewsDatabase(db_path)
    watchlist = db.get_watchlist(min_promising=1, days=30)

    if not watchlist:
        print("No tickers in watchlist")
        return

    # Get details for each ticker
    rows = []
    for ticker in watchlist:
        details = db.get_ticker_details(ticker, days=30)
        if details:
            rows.append({
                "ticker": ticker,
                "company": details.company,
                "promising_articles": details.promising_count,
                "bad_articles": details.bad_count,
                "total_score": details.total_score,
                "avg_score": details.avg_score,
                "latest_positive": details.latest_promising or "",
                "latest_negative": details.latest_bad or ""
            })

    # Save
    if rows:
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)

        print(f"Watchlist saved to {output_path} ({len(rows)} tickers)")
    else:
        print("No watchlist data to export")


def export_avoidlist(db_path: str = "./data/news.db", output_path: str = "./data/avoidlist.csv"):
    """
    Export avoidlist (bad tickers) to CSV.

    Args:
        db_path: Path to news database
        output_path: Output CSV path
    """
    db = NewsDatabase(db_path)
    avoidlist = db.get_avoidlist(min_bad=1, days=30)

    if not avoidlist:
        print("No tickers in avoidlist")
        return

    # Get details for each ticker
    rows = []
    for ticker in avoidlist:
        details = db.get_ticker_details(ticker, days=30)
        if details:
            rows.append({
                "ticker": ticker,
                "company": details.company,
                "promising_articles": details.promising_count,
                "bad_articles": details.bad_count,
                "total_score": details.total_score,
                "avg_score": details.avg_score,
                "latest_positive": details.latest_promising or "",
                "latest_negative": details.latest_bad or ""
            })

    # Save
    if rows:
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)

        print(f"Avoidlist saved to {output_path} ({len(rows)} tickers)")
    else:
        print("No avoidlist data to export")
