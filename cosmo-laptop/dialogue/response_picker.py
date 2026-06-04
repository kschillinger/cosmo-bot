from __future__ import annotations

import random
from typing import Dict, Iterable, Optional

from .intents import INTENTS, IntentDefinition, Response


class ResponsePicker:
    def __init__(
        self,
        *,
        intents: Iterable[IntentDefinition] = INTENTS,
        max_attempts: int = 4,
        rng: Optional[random.Random] = None,
    ) -> None:
        self._intents: Dict[str, IntentDefinition] = {intent.name: intent for intent in intents}
        self._last_idx: Dict[str, int] = {intent.name: -1 for intent in intents}
        self._max_attempts = max_attempts
        self._rng = rng or random.SystemRandom()

    def pick(self, intent_name: str) -> Response:
        """Pick a Response (line + movement) for the intent, avoiding immediate repeats."""
        intent = self._intents.get(intent_name)
        if intent is None:
            raise KeyError(f"Unknown intent: {intent_name}")
        responses = intent.responses
        if not responses:
            raise ValueError(f"Intent {intent_name} has no responses.")

        if len(responses) == 1:
            idx = 0
        else:
            idx = 0
            last = self._last_idx[intent_name]
            for _ in range(self._max_attempts):
                idx = self._rng.randrange(len(responses))
                if idx != last:
                    break

        self._last_idx[intent_name] = idx
        return responses[idx]