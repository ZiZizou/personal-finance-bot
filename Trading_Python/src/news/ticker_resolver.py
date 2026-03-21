"""
Company name to stock ticker resolution for NYSE and TSX markets.
"""

import os
import json
from typing import Optional, Dict, List, Tuple
from difflib import SequenceMatcher


# Default company name to ticker mappings for key industries
DEFAULT_COMPANY_MAPPINGS = {
    # Semiconductors
    "nvidia": "NVDA",
    "nvidia corp": "NVDA",
    "advanced micro devices": "AMD",
    "amd": "AMD",
    "intel": "INTC",
    "intel corporation": "INTC",
    "texas instruments": "TXN",
    "micron": "MU",
    "micron technology": "MU",
    "broadcom": "AVGO",
    "broadcom inc": "AVGO",
    "qualcomm": "QCOM",
    "applied materials": "AMAT",
    "lam research": "LRCX",
    "kla": "KLAC",
    "synopsys": "SNPS",
    "cadence design": "CDNS",
    "marvell": "MRVL",
    "marvell technology": "MRVL",
    "globalfoundries": "GFS",
    "samsung": "SSNLF",
    "tsmc": "TSM",
    "台积电": "TSM",
    "asml": "ASML",
    "arm holdings": "ARM",
    "arm": "ARM",

    # Big Tech
    "apple": "AAPL",
    "microsoft": "MSFT",
    "google": "GOOG",
    "alphabet": "GOOG",
    "amazon": "AMZN",
    "meta": "META",
    "facebook": "META",
    "netflix": "NFLX",
    "tesla": "TSLA",
    "adobe": "ADBE",
    "salesforce": "CRM",
    "oracle": "ORCL",
    "ibm": "IBM",
    "cisco": "CSCO",

    # Pharma
    "pfizer": "PFE",
    "merck": "MRK",
    "johnson & johnson": "JNJ",
    "jnj": "JNJ",
    "eli lilly": "LLY",
    "abbvie": "ABBV",
    "bristol myers squibb": "BMY",
    "bms": "BMY",
    "gilead": "GILD",
    "gilead sciences": "GILD",
    "amgen": "AMGN",
    "regeneron": "REGN",
    "biogen": "BIIB",
    "vertex": "VRTX",
    "alexion": "ALXN",
    "alexion pharmaceuticals": "ALXN",
    "iqvia": "IQV",
    "mckesson": "MCK",
    "cardinal health": "CAH",
    "cvs health": "CVS",
    "walgreens": "WBA",
    "rite aid": "RAD",

    # Biotech
    "moderna": "MRNA",
    "biotech": "XBI",
    "genentech": "DNA",
    "illumina": "ILMN",
    "Thermo Fisher": "TMO",
    "danaher": "DHR",
    "agilent": "A",
    "perkinelmer": "PKI",
    "bruker": "BRKR",

    # Industrial
    "caterpillar": "CAT",
    "deere": "DE",
    "john deere": "DE",
    "3m": "MMM",
    "emerson": "EMR",
    "rockwell automation": "ROK",
    "honeywell": "HON",
    "general electric": "GE",
    "ge": "GE",
    "siemens": "SIE",
    "bosch": "BSWGY",
    " ABB ": "ABB",
    "schneider electric": "SBGSY",

    # Supply Chain / Logistics
    "fedex": "FDX",
    "ups": "UPS",
    "usps": "USPS",
    "amazon": "AMZN",
    "dhl": "DHLGY",
    "xpo": "XPO",
    "old dominion": "ODFL",
    "j.b. hunt": "JBHT",
    "swift transportation": "SWFT",
    "union pacific": "UNP",
    "csx": "CSX",
    "norfolk southern": "NSC",
    "berkshire hathaway": "BRK-B",

    # EVs and Battery
    "rivian": "RIVN",
    "lucid": "LCID",
    "nikola": "NKLA",
    "fisker": "FSR",
    "arrival": "ARVL",
    "quantumscape": "QS",
    "solid power": "SLDP",
    "panasonic": "PCRFY",
    "LG energy": "373220.KS",
    "samsung sdi": "006400.KS",
    "BYD": "002594.SZ",

    # Key startups (may not be public yet, but keep for future)
    "openai": None,
    "anthropic": None,
    "scale ai": None,
    "cruise": None,
    "waymo": None,
}


class TickerResolver:
    """
    Resolves company names to stock tickers on NYSE or TSX.
    """

    def __init__(self, mappings_path: Optional[str] = None):
        """
        Initialize ticker resolver.

        Args:
            mappings_path: Optional path to custom mappings JSON file
        """
        self.mappings = DEFAULT_COMPANY_MAPPINGS.copy()

        # Load custom mappings if provided
        if mappings_path and os.path.exists(mappings_path):
            with open(mappings_path, 'r') as f:
                custom = json.load(f)
                self.mappings.update(custom)

        # Try to load additional mappings
        default_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(__file__))),
            "data", "ticker_mapping.json"
        )
        if os.path.exists(default_path):
            try:
                with open(default_path, 'r') as f:
                    custom = json.load(f)
                    self.mappings.update(custom)
            except Exception:
                pass

    def resolve(self, company_name: str, verify: bool = True) -> Optional[str]:
        """
        Resolve a company name to its ticker symbol.

        Args:
            company_name: Company name to resolve
            verify: Whether to verify the ticker exists on NYSE/TSX

        Returns:
            Ticker symbol if found, None otherwise
        """
        if not company_name:
            return None

        name_lower = company_name.lower().strip()

        # 1. Direct lookup
        if name_lower in self.mappings:
            ticker = self.mappings[name_lower]
            if ticker and (not verify or self._verify_exchange(ticker)):
                return ticker

        # 2. Try partial match
        for known_name, ticker in self.mappings.items():
            if known_name in name_lower or name_lower in known_name:
                if ticker and (not verify or self._verify_exchange(ticker)):
                    return ticker

        # 3. Fuzzy match for close names
        best_match = None
        best_ratio = 0
        for known_name, ticker in self.mappings.items():
            ratio = SequenceMatcher(None, name_lower, known_name).ratio()
            if ratio > 0.8 and ratio > best_ratio:
                if ticker and (not verify or self._verify_exchange(ticker)):
                    best_match = ticker
                    best_ratio = ratio

        if best_match:
            return best_match

        # 4. If verification not required, try Yahoo Finance lookup
        if not verify:
            ticker = self._search_yahoo(company_name)
            if ticker:
                return ticker

        return None

    def _verify_exchange(self, ticker: str) -> bool:
        """
        Verify ticker exists on NYSE or TSX using Yahoo Finance.
        """
        return self.check_ticker_exists(ticker)

    def check_ticker_exists(self, ticker: str) -> bool:
        """
        Check if a ticker exists on NYSE or TSX.

        Args:
            ticker: Ticker symbol to check

        Returns:
            True if ticker is valid on NYSE or TSX
        """
        try:
            import yfinance as yf
            info = yf.Ticker(ticker).info
            exchange = info.get('exchange', '')
            return exchange in ['NYSE', 'TSX']
        except Exception:
            # Try a different approach - check if we can get quotes
            try:
                import yfinance as yf
                ticker_obj = yf.Ticker(ticker)
                # Check if fast info is available
                if hasattr(ticker_obj, 'fast_info'):
                    exchange = ticker_obj.fast_info.get('exchange', '')
                    return exchange in ['NYSE', 'TSX']
                # Try historical
                hist = ticker_obj.history(period="1d", max_threads=True)
                return len(hist) > 0
            except Exception:
                return False

    def _search_yahoo(self, company_name: str) -> Optional[str]:
        """
        Search Yahoo Finance for a company name.
        """
        try:
            import yfinance as yf
            # Use Yahoo's search API indirectly
            search = yf.Search(company_name)
            if search.quotes:
                for quote in search.quotes:
                    if quote.get('quoteType') == 'EQUITY':
                        exchange = quote.get('exchange', '')
                        if exchange in ['NYSE', 'TSX']:
                            return quote.get('symbol')
        except Exception:
            pass
        return None

    def resolve_batch(self, company_names: List[str]) -> Dict[str, Optional[str]]:
        """
        Resolve multiple company names to tickers.

        Args:
            company_names: List of company names

        Returns:
            Dictionary mapping company name to ticker
        """
        return {name: self.resolve(name) for name in company_names}


# Convenience function
_resolver = None


def resolve_ticker(company_name: str, verify: bool = True) -> Optional[str]:
    """Resolve company name to ticker (uses cached resolver)."""
    global _resolver
    if _resolver is None:
        _resolver = TickerResolver()
    return _resolver.resolve(company_name, verify=verify)
