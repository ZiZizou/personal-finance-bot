"""
Company name extraction from news article titles.
"""

import re
from typing import List, Optional, Set


# Common company name suffixes to look for
COMPANY_SUFFIXES = [
    r"Inc\.?",
    r"Corp\.?",
    r"Corporation",
    r"Company",
    r"Co\.?",
    r"Ltd\.?",
    r"LLC",
    r"PLC",
    r"Group",
    r"Holdings?",
    r"Technologies?",
    r"Tech",
    r"Pharmaceuticals?",
    r"Pharma",
    r"Biotech",
    r"Bio",
    r"Semiconductor",
    r"Manufacturing",
    r"Energy",
    r"Systems",
    r"Solutions",
    r"Services",
    r"International",
]

# Build company suffix pattern
SUFFIX_PATTERN = re.compile(
    r"\b(" + "|".join(COMPANY_SUFFIXES) + r")\b",
    re.IGNORECASE
)

# Words that typically indicate NOT a company name
STOP_WORDS = {
    "the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for",
    "of", "with", "by", "from", "as", "is", "was", "are", "were", "been",
    "be", "have", "has", "had", "do", "does", "did", "will", "would",
    "could", "should", "may", "might", "must", "shall", "can", "need",
    "dare", "ought", "used", "it", "its", "this", "that", "these", "those",
    "i", "you", "he", "she", "we", "they", "what", "which", "who", "whom",
    "new", "first", "second", "third", "latest", "recent", "today", "week",
    "month", "year", "says", "said", "according", "report", "reports",
    "announces", "announcement", "unveils", "unveiled", "launches", "launched",
    "raises", "raised", "funding", "investment", "merger", "acquisition",
}

# Patterns for quoted company names
QUOTE_PATTERN = re.compile(r'"([^"]+)"')
# Pattern for "Company X" said/announces etc
CONTEXT_PATTERN = re.compile(
    r'\b([A-Z][a-zA-Z]*(?:\s+[A-Z][a-zA-Z]*){0,3})\s+(?:said|announces?|unveils?|launches?|reveals?|reports?|confirms?|acquires?|merges?|partners?|invests?|raises?|hires?|expands?|opens?|plans?|to|with)\b'
)

# Parentheses pattern for (Company) or (NYSE: X)
PAREN_PATTERN = re.compile(r'\(([^)]+)\)')


class CompanyExtractor:
    """
    Extract company names from news article titles and descriptions.
    """

    def __init__(self, known_companies: Optional[Set[str]] = None):
        """
        Initialize extractor with optional set of known company names.

        Args:
            known_companies: Set of known company names to prioritize
        """
        self.known_companies = known_companies or set()

    def extract(self, title: str, description: str = "") -> List[str]:
        """
        Extract company names from title and description.

        Args:
            title: Article title
            description: Article description/summary

        Returns:
            List of extracted company names
        """
        companies = set()

        # 1. Look for quoted names: "Company Name"
        quoted = QUOTE_PATTERN.findall(title)
        companies.update(self._clean_names(quoted))

        # 2. Look for context patterns: "Company X announces"
        context_matches = CONTEXT_PATTERN.findall(title)
        companies.update(self._clean_names(context_matches))

        # 3. Look for company suffixes
        suffix_matches = SUFFIX_PATTERN.findall(title)
        if suffix_matches:
            # Try to get the full company name before the suffix
            for suffix in suffix_matches:
                pattern = re.compile(
                    rf'\b([A-Z][a-zA-Z]*(?:\s+[A-Z][a-zA-Z]*)*)\s+{re.escape(suffix)}\b',
                    re.IGNORECASE
                )
                matches = pattern.findall(title)
                companies.update(self._clean_names(matches))

        # 4. Check parentheses: (Company) or (NYSE: ABC)
        paren_matches = PAREN_PATTERN.findall(title)
        for match in paren_matches:
            # Skip if it looks like an exchange or ticker
            if ":" in match or match.upper() in ["NYSE", "NASDAQ", "TSX", "AMEX"]:
                continue
            if len(match) > 2 and len(match) < 50:
                companies.add(match.strip())

        # 5. Check known companies
        title_lower = title.lower()
        for known in self.known_companies:
            if known.lower() in title_lower:
                companies.add(known)

        # 6. Try capitalized words pattern
        capitalized = self._extract_capitalized(title)
        companies.update(capitalized)

        return list(companies)

    def _clean_names(self, names: List[str]) -> Set[str]:
        """Clean and filter extracted names."""
        cleaned = set()
        for name in names:
            # Skip short or empty names
            if not name or len(name) < 3:
                continue
            # Skip if all stop words
            words = name.lower().split()
            if all(w in STOP_WORDS for w in words):
                continue
            # Skip if looks like a date or number
            if re.match(r'^\d+$', name):
                continue
            # Clean up
            name = " ".join(w for w in name.split() if w)
            if name:
                cleaned.add(name)
        return cleaned

    def _extract_capitalized(self, text: str) -> Set[str]:
        """Extract capitalized word sequences."""
        # Pattern for sequences of capitalized words
        pattern = re.compile(r'\b([A-Z][a-z]+(?:\s+[A-Z][a-z]+){0,2})\b')
        matches = pattern.findall(text)
        return self._clean_names(matches)

    def extract_first(self, title: str, description: str = "") -> Optional[str]:
        """Extract the most likely primary company name."""
        companies = self.extract(title, description)
        if not companies:
            return None
        # Return shortest (usually the core name, not full legal name)
        return min(companies, key=len)


# Convenience function
_extractor = None


def extract_companies(title: str, description: str = "") -> List[str]:
    """Extract company names from article (uses cached extractor)."""
    global _extractor
    if _extractor is None:
        _extractor = CompanyExtractor()
    return _extractor.extract(title, description)


def extract_first_company(title: str, description: str = "") -> Optional[str]:
    """Extract the primary company name."""
    global _extractor
    if _extractor is None:
        _extractor = CompanyExtractor()
    return _extractor.extract_first(title, description)
