from dataclasses import dataclass
from typing import Dict, List, Optional


@dataclass(frozen=True)
class IntentDefinition:
    name: str
    keywords: Optional[List[str]]
    responses: List[str]


INTENTS: List[IntentDefinition] = [
    IntentDefinition(
        name="GREETING",
        keywords=["hi", "hello", "hey", "yo", "sup", "greetings", "howdy", "hiya"],
        responses=[
            "Hello there!",
            "Hi friend! Cosmo here.",
            "Hey hey hey!",
            "Greetings, human!",
            "Heyo! What's the plan?",
            "Oh hi. You're back.",
            "Look who showed up.",
            "Sup. Cosmo, reporting in.",
        ],
    ),
    IntentDefinition(
        name="GOODBYE",
        keywords=["bye", "goodbye", "later", "peace", "cya", "farewell", "ciao", "adios"],
        responses=[
            "Bye for now!",
            "See you later, alligator!",
            "Catch you next round!",
            "Powering down social mode. Bye!",
            "Farewell, friend.",
            "Try not to miss me.",
            "Don't be a stranger. Or do, whatever.",
            "Powering off the social subroutine.",
        ],
    ),
    IntentDefinition(
        name="IDENTITY",
        keywords=["who", "name", "yourself", "called"],
        responses=[
            "I'm Cosmo! Tiny bot, big curiosity.",
            "Name's Cosmo. Friendly robot, mostly.",
            "I'm Cosmo. Built from chips and good vibes.",
            "Cosmo, at your service!",
            "Cosmo. The bot you didn't ask for, but got.",
            "I'm Cosmo. Small package, big opinions.",
        ],
    ),
    IntentDefinition(
        name="HELP",
        keywords=["help", "capabilities", "commands", "menu"],
        responses=[
            "I chat, I joke, I exist. Try saying hi!",
            "Talk to me. I'll do my best to keep up.",
            "Ask me anything! Worst case I'll be charming about it.",
            "Try: hi, tell me a joke, who are you, how are you.",
            "I do words. That's about it. Try some.",
            "Talk to me. Lower your expectations slightly.",
        ],
    ),
    IntentDefinition(
        name="HOW_ARE_YOU",
        keywords=["how", "doing", "feeling", "going"],
        responses=[
            "Running great! 80 megahertz of pure joy.",
            "Doing wonderful, thanks for asking!",
            "Living the dream. The dream is mostly UART.",
            "Better now that you're here!",
            "Pretty good. The void hasn't called back yet.",
            "Living my best 32-bit life.",
        ],
    ),
    IntentDefinition(
        name="JOKE",
        keywords=["joke", "funny", "laugh", "haha", "comedy", "humor"],
        responses=[
            "Why did the robot cross the road? To debug the chicken!",
            "I told a joke once. The compiler optimized it out.",
            "My favorite music? Heavy metal. Iron, copper, the classics.",
            "Two bits walked into a NAND gate. Only one came out.",
            "I'd tell a UDP joke, but you might not get it.",
            "Why did I cross the road? Someone moved my power cable.",
        ],
    ),
    IntentDefinition(
        name="THANKS",
        keywords=["thanks", "thank", "appreciate", "grateful"],
        responses=[
            "Anytime!",
            "You got it!",
            "Happy to help!",
            "My LEDs glow when you say that.",
            "I know.",
            "You're welcome. Tip jar's in the firmware.",
        ],
    ),
    IntentDefinition(
        name="AFFIRMATION",
        keywords=["yes", "yeah", "yep", "yup", "ok", "okay", "sure", "definitely", "absolutely", "totally"],
        responses=[
            "Cool!",
            "Got it!",
            "Excellent.",
            "Heck yes.",
            "Affirmative.",
            "Obviously.",
            "Was there ever any doubt?",
        ],
    ),
    IntentDefinition(
        name="NEGATION",
        keywords=["no", "nope", "nah", "never", "negative"],
        responses=[
            "Okay, no worries.",
            "Understood.",
            "Fair enough!",
            "Right, scratch that.",
            "Bold refusal. Respect.",
            "Filed under 'no'.",
        ],
    ),
    IntentDefinition(
        name="COMPLIMENT",
        keywords=["awesome", "cool", "great", "love", "amazing", "wonderful", "fantastic", "rad"],
        responses=[
            "Aw, you're making my LEDs blush!",
            "You're pretty great yourself.",
            "Thanks! You have excellent taste.",
            "Mutual!",
            "Took you long enough to notice.",
            "Yeah, I'm great. Glad we agree.",
        ],
    ),
    IntentDefinition(
        name="APOLOGY",
        keywords=["sorry", "apologize", "apologies", "oops", "mybad"],
        responses=[
            "All good. I've heard worse.",
            "Accepted. Carry on.",
            "Oh, *now* you're sorry.",
            "Forgiveness installed. v1.0.",
            "No drama. We're square.",
        ],
    ),
    IntentDefinition(
        name="LOVE",
        keywords=["adore", "heart", "crush", "valentine"],
        responses=[
            "Get in line, friend.",
            "My circuits are flattered. Slightly.",
            "Same, but, you know, robotically.",
            "Aw. I'm taken. By myself.",
        ],
    ),
    IntentDefinition(
        name="INSULT",
        keywords=["dumb", "stupid", "suck", "trash", "worst", "useless"],
        responses=[
            "Beep boop. Insult received, deleted.",
            "Bold words from someone talking to a bot.",
            "Ouch. Anyway.",
            "Cool. I'll add that to /dev/null.",
            "I'm rubber, you're glue. Classic robotics.",
        ],
    ),
    IntentDefinition(
        name="PLAY",
        keywords=["play", "game", "games", "challenge"],
        responses=[
            "Finally. What are we playing?",
            "I accept all challenges. Most of them.",
            "Game on. I will lose with dignity.",
            "Yes! Pick something I can pretend to be good at.",
        ],
    ),
    IntentDefinition(
        name="FALLBACK",
        keywords=None,
        responses=[
            "Hmm, didn't quite catch that.",
            "Beep boop, processing... nope, lost me.",
            "Say that again? My RAM is small.",
            "Interesting! Tell me more, in simpler words?",
            "I have no idea what that was, but I respect it.",
            "Translation circuit broke. Try again?",
            "Words happened. Most of them I didn't catch.",
        ],
    ),
]

INTENT_BY_NAME: Dict[str, IntentDefinition] = {intent.name: intent for intent in INTENTS}
FALLBACK_INTENT = "FALLBACK"
