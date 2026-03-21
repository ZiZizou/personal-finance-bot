"""
News scraping module for trading research.
Fetches RSS feeds, classifies articles, extracts companies, and maps to tickers.
"""

from .sources import RSS_SOURCE, get_sources_by_industry, INDUSTRIES
from .classifier import ArticleClassifier, classify_article
from .extractor import CompanyExtractor, extract_companies
from .ticker_resolver import TickerResolver, resolve_ticker
from .database import NewsDatabase, Article
from .scraper import NewsScraper
from .storage import save_to_csv, load_from_csv, export_watchlist, export_avoidlist

__all__ = [
    "RSS_SOURCE",
    "INDUSTRIES",
    "get_sources_by_industry",
    "ArticleClassifier",
    "classify_article",
    "CompanyExtractor",
    "extract_companies",
    "TickerResolver",
    "resolve_ticker",
    "NewsDatabase",
    "Article",
    "NewsScraper",
    "save_to_csv",
    "load_from_csv",
    "export_watchlist",
    "export_avoidlist",
]
