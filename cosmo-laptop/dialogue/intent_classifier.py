from __future__ import annotations

import re
from typing import Iterable, List, Tuple

from .intents import FALLBACK_INTENT, INTENTS, IntentDefinition


class IntentClassifier:
    def __init__(
        self,
        *,
        intents: Iterable[IntentDefinition] = INTENTS,
        min_confidence_pct: int = 25,
        max_tokens: int = 16,
    ) -> None:
        self._intents = list(intents)
        self._min_confidence_pct = min_confidence_pct
        self._max_tokens = max_tokens

    def classify(self, text: str) -> Tuple[str, int]:
        if not text:
            return FALLBACK_INTENT, 0

        tokens = self._tokenize(text)
        if not tokens:
            return FALLBACK_INTENT, 0

        best_score = 0
        best_intent = FALLBACK_INTENT

        for intent in self._intents:
            if intent.name == FALLBACK_INTENT:
                continue
            score = self._score_intent(intent, tokens)
            if score > best_score:
                best_score = score
                best_intent = intent.name

        chosen = best_intent if best_score >= self._min_confidence_pct else FALLBACK_INTENT
        return chosen, best_score

    def _tokenize(self, text: str) -> List[str]:
        normalized = re.sub(r"[^0-9a-zA-Z]+", " ", text.lower())
        tokens = [tok for tok in normalized.split(" ") if tok]
        return tokens[: self._max_tokens]

    def _score_intent(self, intent: IntentDefinition, tokens: List[str]) -> int:
        if not intent.keywords:
            return 0
        matched = 0
        total = 0
        token_set = set(tokens)
        for keyword in intent.keywords:
            total += 1
            if keyword in token_set:
                matched += 1
        if total == 0:
            return 0
        return int((matched * 100) / total)
