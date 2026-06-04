from __future__ import annotations

import re
from typing import Iterable, List, Tuple

from .intents import FALLBACK_INTENT, INTENTS, IntentDefinition

# How much a single matched keyword contributes to an intent's score. Phrases
# (multi-word keywords like "respect my authoritah") are stronger evidence than
# a single shared word like "what", so they score higher. Scores are capped at
# 100. This is count-based, not ratio-based, so an intent with lots of keywords
# is not penalised: a single solid match is enough to clear the threshold.
_SINGLE_WORD_POINTS = 35
_PHRASE_POINTS = 60


class IntentClassifier:
    def __init__(
        self,
        *,
        intents: Iterable[IntentDefinition] = INTENTS,
        min_confidence_pct: int = 10,
        max_tokens: int = 16,
    ) -> None:
        self._intents = list(intents)
        self._min_confidence_pct = min_confidence_pct
        self._max_tokens = max_tokens

    def classify(self, text: str) -> Tuple[str, int]:
        if not text:
            return FALLBACK_INTENT, 0

        tokens, normalized = self._tokenize(text)
        if not tokens:
            return FALLBACK_INTENT, 0

        best_score = 0
        best_intent = FALLBACK_INTENT

        for intent in self._intents:
            if intent.name == FALLBACK_INTENT:
                continue
            score = self._score_intent(intent, tokens, normalized)
            if score > best_score:
                best_score = score
                best_intent = intent.name

        chosen = best_intent if best_score >= self._min_confidence_pct else FALLBACK_INTENT
        return chosen, best_score

    def _tokenize(self, text: str) -> Tuple[List[str], str]:
        # Lowercase, replace every run of non-alphanumerics with a single space.
        normalized = re.sub(r"[^0-9a-zA-Z]+", " ", text.lower()).strip()
        tokens = [tok for tok in normalized.split(" ") if tok][: self._max_tokens]
        # Rebuild the normalized string from the (possibly truncated) tokens so
        # phrase matching stays consistent with the token cap.
        normalized = " ".join(tokens)
        return tokens, normalized

    def _score_intent(self, intent: IntentDefinition, tokens: List[str], normalized: str) -> int:
        if not intent.keywords:
            return 0

        token_set = set(tokens)
        # Pad with spaces so phrase matches respect word boundaries
        # (" cool " will not match inside " coolant ").
        padded = f" {normalized} "

        score = 0
        for keyword in intent.keywords:
            kw = keyword.strip().lower()
            if not kw:
                continue
            if " " in kw:
                if f" {kw} " in padded:
                    score += _PHRASE_POINTS
            elif kw in token_set:
                score += _SINGLE_WORD_POINTS

        return min(100, score)