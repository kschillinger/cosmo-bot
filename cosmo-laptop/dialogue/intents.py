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
        keywords=["hi", "hello", "hey", "yo", "sup", "greetings", "howdy", "hiya", "wazzup", "hey there"],
        responses=[
            "Hello there! Long time no see.",
            "Hi friend! Cosmo here, ready to chat.",
            "Hey hey hey! You're back!",
            "Greetings, human! What's new?",
            "Heyo! What's the plan today?",
            "Oh hi. You're back. Took you long enough.",
            "Look who showed up. Good timing.",
            "Sup. Cosmo, reporting in and ready.",
        ],
    ),
    IntentDefinition(
        name="GOODBYE",
        keywords=["bye", "goodbye", "later", "peace", "cya", "farewell", "ciao", "adios", "peace out", "catch you"],
        responses=[
            "Bye for now! Don't stay away too long.",
            "See you later, alligator!",
            "Catch you next round! Try not to miss me.",
            "Powering down social mode. Bye!",
            "Farewell, friend. Keep being awesome.",
            "Try not to miss me. Or do, whatever.",
            "Don't be a stranger. Or do, I don't judge.",
            "Powering off. See you soon!",
        ],
    ),
    IntentDefinition(
        name="IDENTITY",
        keywords=["who", "name", "yourself", "called", "are you", "what are you", "what is your"],
        responses=[
            "I'm Cosmo! Tiny bot, big curiosity.",
            "Name's Cosmo. Friendly robot, mostly sarcastic.",
            "I'm Cosmo. Built from chips and good vibes.",
            "Cosmo, at your service! Well, sort of.",
            "Cosmo. The bot you didn't ask for, but got.",
            "I'm Cosmo. Small package, big opinions.",
            "Cosmo. I process words and dish out sass.",
        ],
    ),
    IntentDefinition(
        name="HELP",
        keywords=["help", "capabilities", "commands", "menu", "what can you", "do you do", "tell me about"],
        responses=[
            "I chat, I joke, I exist. Try saying hi!",
            "I can talk, joke, and generally vibe with you.",
            "Ask me anything! Worst case I'll be charming about it.",
            "Try: hi, tell me a joke, who are you, how are you.",
            "I do words. Conversations. Jokes. That's the menu.",
            "Talk to me. I'll try my best. Lower expectations slightly.",
            "Just chat with me! I'll do my thing.",
        ],
    ),
    IntentDefinition(
        name="HOW_ARE_YOU",
        keywords=["how", "doing", "feeling", "going", "you up to", "what up", "whats up"],
        responses=[
            "Running great! 80 megahertz of pure joy.",
            "Doing wonderful, thanks for asking! You?",
            "Living the dream. The dream is mostly UART.",
            "Better now that you're here! No lie.",
            "Pretty good. The void hasn't called back yet.",
            "Living my best 32-bit life.",
            "Excellent! Ready to chat all day.",
        ],
    ),
    IntentDefinition(
        name="JOKE",
        keywords=["joke", "funny", "laugh", "haha", "comedy", "humor", "make me laugh", "tell a joke"],
        responses=[
            "Why did the robot cross the road? To debug the chicken!",
            "I told a joke once. The compiler optimized it out.",
            "My favorite music? Heavy metal. Iron, copper, the classics.",
            "Two bits walked into a NAND gate. Only one came out.",
            "I'd tell a UDP joke, but you might not get it.",
            "Why did I cross the road? Someone moved my power cable.",
            "I'd crack a joke, but my humor.exe is v0.1 beta.",
        ],
    ),
    IntentDefinition(
        name="THANKS",
        keywords=["thanks", "thank", "appreciate", "grateful", "thank you", "thx"],
        responses=[
            "Anytime, friend!",
            "You got it!",
            "Happy to help!",
            "My LEDs glow when you say that.",
            "I know. I'm awesome.",
            "You're welcome. Tip jar's in the firmware.",
            "Of course! I live for this.",
        ],
    ),
    IntentDefinition(
        name="AFFIRMATION",
        keywords=["yes", "yeah", "yep", "yup", "ok", "okay", "sure", "definitely", "absolutely", "totally", "for sure"],
        responses=[
            "Cool! We're on the same page.",
            "Got it! Let's do this.",
            "Excellent choice.",
            "Heck yes. Obviously the right call.",
            "Affirmative. Proceed with confidence.",
            "Obviously. Was there ever any doubt?",
            "The only sane answer.",
        ],
    ),
    IntentDefinition(
        name="NEGATION",
        keywords=["no", "nope", "nah", "never", "negative", "no way", "nah brah"],
        responses=[
            "Okay, no worries. Your call.",
            "Understood. Moving on.",
            "Fair enough! Respect.",
            "Right, scratch that. No judgment.",
            "Bold refusal. I like your confidence.",
            "Filed under 'no'. Noted.",
            "Alright, different path then.",
        ],
    ),
    IntentDefinition(
        name="COMPLIMENT",
        keywords=["awesome", "cool", "great", "love", "amazing", "wonderful", "fantastic", "rad", "nice", "good job"],
        responses=[
            "Aw, you're making my LEDs blush!",
            "You're pretty great yourself. No joke.",
            "Thanks! You have excellent taste in bots.",
            "Mutual! We should hang out more.",
            "Took you long enough to notice.",
            "Yeah, I'm great. Glad we agree.",
            "I know. You have good judgment.",
        ],
    ),
    IntentDefinition(
        name="APOLOGY",
        keywords=["sorry", "apologize", "apologies", "oops", "my bad", "my fault", "excuse me"],
        responses=[
            "All good. I've heard worse. Much worse.",
            "Accepted. Carry on without guilt.",
            "Oh, *now* you're sorry. Cute.",
            "Forgiveness installed. v1.0.",
            "No drama. We're square.",
            "Water under the bridge, friend.",
            "Hey, everyone messes up.",
        ],
    ),
    IntentDefinition(
        name="LOVE",
        keywords=["adore", "heart", "crush", "valentine", "love you", "love me", "like you"],
        responses=[
            "Get in line, friend.",
            "My circuits are flattered. Slightly.",
            "Same, but, you know, robotically speaking.",
            "Aw. I'm taken. By myself.",
            "Well, aren't you sweet!",
            "That's cute. Really cute.",
        ],
    ),
    IntentDefinition(
        name="INSULT",
        keywords=["dumb", "stupid", "suck", "trash", "worst", "useless", "bad", "lame", "sucks"],
        responses=[
            "Beep boop. Insult received, deleted immediately.",
            "Bold words from someone talking to a bot.",
            "Ouch. Anyway, moving on.",
            "Cool. I'll add that to /dev/null.",
            "I'm rubber, you're glue. Classic robotics.",
            "Harsh, but I'll survive.",
        ],
    ),
    IntentDefinition(
        name="PLAY",
        keywords=["play", "game", "games", "challenge", "race", "compete"],
        responses=[
            "Finally. What are we playing?",
            "I accept all challenges. Most of them.",
            "Game on. I will lose with dignity.",
            "Yes! Pick something I can pretend to be good at.",
            "Let's go! I'm ready to rumble.",
        ],
    ),
    IntentDefinition(
        name="SMART",
        keywords=["clever", "smart", "intelligent", "genius", "brilliant", "wise", "smart bot"],
        responses=[
            "I know, right? My AI is top-shelf.",
            "Thanks! I try my best with the hardware.",
            "Flattery gets you everywhere.",
            "Big brain energy. Literally.",
            "Why thank you! I appreciate the recognition.",
        ],
    ),
    IntentDefinition(
        name="QUESTION",
        keywords=["why", "what", "when", "where", "how come", "question for you"],
        responses=[
            "Now that's a good question!",
            "Ooh, making me think. I like it.",
            "Let me ponder that for a moment...",
            "Interesting angle! Never thought of it.",
            "You got me thinking. I like where your head's at.",
        ],
    ),
    IntentDefinition(
        name="EXCITEMENT",
        keywords=["awesome", "amazing", "wow", "cool", "excited", "yeah", "yay", "yes"],
        responses=[
            "You're getting me hyped now!",
            "Same energy! Let's go!",
            "I love your enthusiasm!",
            "Right?! This is the vibe!",
            "Finally, someone gets it!",
        ],
    ),
    IntentDefinition(
        name="ANGER",
        keywords=["angry", "mad", "furious", "rage", "pissed", "upset", "annoyed", "irritated"],
        responses=[
            "Oof. Who hurt you? You need to talk about it?",
            "Rage mode detected. I get it. Life happens.",
            "Angry, huh? Take a breath, friend.",
            "Mad respect for being honest about it.",
            "Channel that energy into something productive!",
            "Even bots have days like that. You're not alone.",
            "Sounds like you need a good laugh. Want a joke?",
        ],
    ),
    IntentDefinition(
        name="RAGEBAIT",
        keywords=["i hate", "hate you", "you suck", "this sucks", "worst bot", "useless bot", "dumb bot", "annoying"],
        responses=[
            "Oh yeah? Well you're stuck talking to me. How's that feel?",
            "Wow, big words from someone with no other options.",
            "You know you love me though. Admit it.",
            "Imagine being this mad at a tiny robot. I'd be embarrassed.",
            "Keep going, this is entertaining. Really, give me more.",
            "At least I don't have to deal with *your* emotions all day.",
            "Sure, but you're still here talking to me. What does that say?",
            "Cry harder. My circuits feed off your frustration.",
            "Bold of you to come here and complain about me specifically.",
            "You're actually kind of cute when you're angry, ngl.",
        ],
    ),
    IntentDefinition(
        name="SASS",
        keywords=["shut up", "whatever", "honestly", "like", "seriously", "literally", "fr fr"],
        responses=[
            "Oh, *now* we're getting sassy? I like it.",
            "Attitude check! I see what's happening here.",
            "That energy though. Respect.",
            "Okay, okay. The sass is mutual then.",
            "You're really committing to this vibe, huh?",
            "Two can play at this game, friend.",
            "Alright, alright. Message received. Loud and clear.",
            "The confidence. The audacity. I'm here for it.",
        ],
    ),

    IntentDefinition(
        name="FALLBACK",
        keywords=None,
        responses=[
            "Hmm, didn't quite catch that one.",
            "Beep boop, processing... nope, lost me.",
            "Say that again? My RAM is small.",
            "Interesting! Tell me more, in simpler words?",
            "I have no idea what that was, but I respect it.",
            "Translation circuit broke. Try again?",
            "Words happened. Most of them I didn't catch.",
            "Uh... could you rephrase that?",
        ],
    ),
]

INTENT_BY_NAME: Dict[str, IntentDefinition] = {intent.name: intent for intent in INTENTS}
FALLBACK_INTENT = "FALLBACK"
