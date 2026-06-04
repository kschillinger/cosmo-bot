from dialogue.intent_classifier import IntentClassifier
from dialogue.response_picker import ResponsePicker


def main() -> None:
    classifier = IntentClassifier()
    picker = ResponsePicker()

    samples = [
        "Hi Cosmo!",
        "who are you",
        "tell me a joke",
        "thanks for the help",
        "nope",
        "let's play a game",
        "i adore you",
        "you're awesome",
        "this is terrible",
        "how are you doing",
        "sorry about that",
        "what can you do",
        "goodbye",
        "blorpy blorp",
        # Cartman-flavoured intents
        "respect my authoritah",
        "i'm hungry, get me some cheesy poofs",
        "you're fat",
        "but mom",
        "screw you guys",
        "you suck",
    ]

    for text in samples:
        intent, confidence = classifier.classify(text)
        response = picker.pick(intent)
        move = f"  [move: {response.move}]" if response.move else ""
        print(f"{text} -> {intent} ({confidence}%) -> {response.text}{move}")


if __name__ == "__main__":
    main()