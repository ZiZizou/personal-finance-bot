"""
RSS feed source definitions for news scraping.
"""

from dataclasses import dataclass
from typing import List


@dataclass
class RSSSource:
    """Represents an RSS feed source."""
    name: str
    url: str
    industry: str
    description: str = ""


# Industry categories
INDUSTRIES = [
    "semiconductors",
    "supply_chain",
    "pharma",
    "biotech",
    "industrial",
    "tech",
]


# RSS feed sources
RSS_SOURCES = [
    # Semiconductors
    RSSSource(
        name="Semiconductor Engineering",
        url="https://semiengineering.com/feed/",
        industry="semiconductors",
        description="Deep insights on manufacturing innovations and fab investments",
    ),
    RSSSource(
        name="EE Times",
        url="https://www.eetimes.com/feed/",
        industry="semiconductors",
        description="Chip shortages, M&A, and electronics manufacturing",
    ),

    # Supply Chain
    RSSSource(
        name="Supply Chain Dive",
        url="https://www.supplychaindive.com/rss/articles",
        industry="supply_chain",
        description="Sector-agnostic dives into investments and risks",
    ),
    RSSSource(
        name="Journal of Commerce",
        url="https://www.joc.com/rss",
        industry="supply_chain",
        description="International trade and ports; supply shifts",
    ),
    RSSSource(
        name="Supply Chain Brain",
        url="https://www.supplychainbrain.com/rss/articles",
        industry="supply_chain",
        description="Strategic insights on tech integrations and M&A",
    ),

    # Pharma
    RSSSource(
        name="FiercePharma",
        url="https://www.fiercepharma.com/fiercepharmacom/rss-feeds",
        industry="pharma",
        description="M&A, launches, and investments in pharma",
    ),
    RSSSource(
        name="Citeline Insights",
        url="https://insights.citeline.com/rss-feeds",
        industry="pharma",
        description="Regulatory and trial-focused biopharma insights",
    ),

    # Biotech
    RSSSource(
        name="FierceBiotech",
        url="https://www.fiercebiotech.com/fiercebiotechcom/rss-feeds",
        industry="biotech",
        description="Biotech M&A and breakthroughs",
    ),
    RSSSource(
        name="GEN",
        url="https://www.genengnews.com/rss/",
        industry="biotech",
        description="Emerging therapies and investments",
    ),
    RSSSource(
        name="BioWorld",
        url="https://www.bioworld.com/featured-feeds",
        industry="biotech",
        description="Global innovation alerts and regulatory wins",
    ),

    # Industrial / Manufacturing
    RSSSource(
        name="Manufacturing.net",
        url="https://www.manufacturing.net/rss-feeds/all/rss.xml/all",
        industry="industrial",
        description="Broad industrial updates with supply focus",
    ),
    RSSSource(
        name="The Manufacturer",
        url="https://www.themanufacturer.com/feed/",
        industry="industrial",
        description="Strategic insights on aerospace/auto growth",
    ),
    RSSSource(
        name="SupplyChain24/7",
        url="https://www.supplychain247.com/rss",
        industry="industrial",
        description="Logistics-heavy with industrial ties; M&A in machinery",
    ),

    # Tech / Consumer
    RSSSource(
        name="TechCrunch",
        url="https://techcrunch.com/feed/",
        industry="tech",
        description="Startup-focused on software and electronics investments",
    ),
    RSSSource(
        name="Wired",
        url="https://www.wired.com/feed/rss",
        industry="tech",
        description="Broader trends with software/consumer insights",
    ),
    RSSSource(
        name="Hacker News",
        url="https://news.ycombinator.com/rss",
        industry="tech",
        description="Community-voted on engineering breakthroughs",
    ),
]


def get_sources_by_industry(industry: str) -> List[RSSSource]:
    """Get all RSS sources for a specific industry."""
    return [s for s in RSS_SOURCES if s.industry == industry]


def get_all_sources() -> List[RSSSource]:
    """Get all RSS sources."""
    return RSS_SOURCES


# Alias for backwards compatibility
RSS_SOURCE = RSS_SOURCES
