"""
Article classification for promising/bad news detection.
"""

import re
from dataclasses import dataclass
from typing import List, Dict, Optional
from enum import Enum


class Classification(Enum):
    """News article classification."""
    PROMISING = "promising"
    BAD = "bad"
    NEUTRAL = "neutral"


# High-value positive keywords (score: 10 each)
PROMISING_KEYWORDS = [
    # Funding/Investment
    r"\bfunding\b",
    r"\binvestment\b",
    r"\bSeries A\b",
    r"\bSeries B\b",
    r"\bSeries C\b",
    r"\bSeries D\b",
    r"\braised\b",
    r"\braised\s+\$\d+",
    r"\bclosed\b.*\bfunding\b",
    r"\bseed\b.*\bround\b",
    r"\bventure\b.*\bcapital\b",
    r"\bVC\b.*\bfunding\b",

    # M&A
    r"\bmerger\b",
    r"\bacquisition\b",
    r"\bM&A\b",
    r"\bacqui-hire\b",
    r"\bbuyout\b",
    r"\btakeover\b",

    # Expansion/Growth
    r"\bexpansion\b",
    r"\bnew fab\b",
    r"\bnew\s+facility\b",
    r"\bmanufacturing\s+facility\b",
    r"\bplant\b.*\bnew\b",
    r"\bbuild\b.*\bfactory\b",

    # Breakthroughs/Approvals
    r"\bbreakthrough\b",
    r"\bclinical\s+trial\b",
    r"\bFDA\s+approval\b",
    r"\bregulatory\s+approval\b",
    r"\bphase\s+[123]\b",
    r"\btrial\s+results\b",

    # Partnerships
    r"\bpartnership\b",
    r"\bjoint\s+venture\b",
    r"\bstrategic\s+alliance\b",
    r"\bcollaboration\b",
    r"\bco-development\b",

    # IPO/Public
    r"\bIPO\b",
    r"\bwent\s+public\b",
    r"\bpublic\s+offering\b",
    r"\blisting\b",
    r"\bdebut\b.*\bstock\b",
]

# High-value negative keywords (score: -10 each)
BAD_KEYWORDS = [
    # Layoffs/Cuts
    r"\blayoff\b",
    r"\blayoffs\b",
    r"\bjob\s+cuts\b",
    r"\bdownsizing\b",
    r"\bcuts?\s+jobs\b",
    r"\breduction\s+in\s+force\b",
    r"\bRIF\b",
    r"\bheadcount\s+cut\b",

    # Bankruptcy
    r"\bbankruptcy\b",
    r"\binsolvent\b",
    r"\bChapter\s+11\b",
    r"\bChapter\s+7\b",
    r"\bdeclares?\s+bankruptcy\b",
    r"\bfiles?\s+for\s+bankruptcy\b",

    # Scandals
    r"\bscandal\b",
    r"\bfraud\b",
    r"\binvestigation\b",
    r"\bSEC\s+investigation\b",
    r"\b DOJ\b",
    r"\ballegations?\b",
    r"\bsubpoena\b",

    # Product Issues
    r"\brecall\b",
    r"\bdefect\b",
    r"\bsafety\s+issue\b",
    r"\bproduct\s+fail(ure|ed)?\b",
    r"\bmalfunction\b",

    # Legal
    r"\blawsuit\b",
    r"\blitigation\b",
    r"\bsued\b",
    r"\bsettlement\b",
    r"\bfine\b",
    r"\bpenalty\b",

    # Missed Expectations
    r"\bmissed\b",
    r"\bguidance\s+cut\b",
    r"\bwarns?\b",
    r"\brevenue\s+miss\b",
    r"\bearnings\s+miss\b",
    r"\blower\s+guidance\b",
    r"\brevised\s+down\b",
    r"\bcut\s+outlook\b",
]

# Medium-value keywords (+5 or -5)
MEDIUM_POSITIVE_KEYWORDS = [
    r"\bstartup\b",
    r"\blaunch(ed)?\b",
    r"\bunveiled\b",
    r"\bannounced\b",
    r"\bgrowth\b",
    r"\brevenue\b.*\bincrease\b",
    r"\bprofit\b",
    r"\binnovation\b",
    r"\btech\b",
    r"\bAI\b",
    r"\bmachine\s+learning\b",
    r"\bhiring\b",
    r"\bjobs\b.*\bcreate\b",
    r"\bexpands?\b",
    r"\bnew\s+product\b",
    r"\brelease[d]?\b",
]

MEDIUM_NEGATIVE_KEYWORDS = [
    r"\bdelay\b",
    r"\bpostpone[d]?\b",
    r"\bcancel(led)?\b",
    r"\bterminat(ed)?\b",
    r"\bslowdown\b",
    r"\bdecline\b",
    r"\bloss\b",
    r"\blosses\b",
    r"\bshared?\s+drop\b",
    r"\bplunge[d]?\b",
    r"\btumble[d]?\b",
    r"\bconcern[s]?\b",
    r"\bworried\b",
    r"\buncertain\b",
]


@dataclass
class ClassificationResult:
    """Result of article classification."""
    classification: Classification
    score: float
    matched_keywords: List[str]
    details: Dict[str, any]


class ArticleClassifier:
    """
    Classifies news articles as promising, bad, or neutral
    based on keyword matching.
    """

    def __init__(self):
        # Compile regex patterns for efficiency
        self._promising_patterns = [
            re.compile(p, re.IGNORECASE) for p in PROMISING_KEYWORDS
        ]
        self._bad_patterns = [
            re.compile(p, re.IGNORECASE) for p in BAD_KEYWORDS
        ]
        self._medium_positive_patterns = [
            re.compile(p, re.IGNORECASE) for p in MEDIUM_POSITIVE_KEYWORDS
        ]
        self._medium_negative_patterns = [
            re.compile(p, re.IGNORECASE) for p in MEDIUM_NEGATIVE_KEYWORDS
        ]

    def classify(self, title: str, description: str = "") -> ClassificationResult:
        """
        Classify an article based on its title and description.

        Args:
            title: Article title
            description: Article description/summary

        Returns:
            ClassificationResult with classification, score, and matched keywords
        """
        text = f"{title} {description}".lower()
        matched = []

        # Check high-value positive keywords
        score = 0
        for pattern in self._promising_patterns:
            if pattern.search(text):
                score += 10
                matched.append(pattern.pattern)

        # Check high-value negative keywords
        for pattern in self._bad_patterns:
            if pattern.search(text):
                score -= 10
                matched.append(pattern.pattern)

        # Check medium-value positive keywords
        for pattern in self._medium_positive_patterns:
            if pattern.search(text):
                score += 5
                matched.append(pattern.pattern)

        # Check medium-value negative keywords
        for pattern in self._medium_negative_patterns:
            if pattern.search(text):
                score -= 5
                matched.append(pattern.pattern)

        # Determine classification
        if score > 0:
            classification = Classification.PROMISING
        elif score < 0:
            classification = Classification.BAD
        else:
            classification = Classification.NEUTRAL

        return ClassificationResult(
            classification=classification,
            score=score,
            matched_keywords=list(set(matched)),
            details={"text_length": len(text)}
        )

    def is_promising(self, title: str, description: str = "", threshold: int = 1) -> bool:
        """Quick check if article is promising."""
        result = self.classify(title, description)
        return result.classification == Classification.PROMISING and result.score >= threshold

    def is_bad(self, title: str, description: str = "", threshold: int = 1) -> bool:
        """Quick check if article is bad."""
        result = self.classify(title, description)
        return result.classification == Classification.BAD and result.score <= -threshold


# Convenience function
_classifier = None


def classify_article(title: str, description: str = "") -> ClassificationResult:
    """Classify an article (uses cached classifier)."""
    global _classifier
    if _classifier is None:
        _classifier = ArticleClassifier()
    return _classifier.classify(title, description)
