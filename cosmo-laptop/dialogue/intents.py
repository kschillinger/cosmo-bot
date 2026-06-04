from dataclasses import dataclass
from typing import Dict, List, Optional


@dataclass(frozen=True)
class Response:
    """One line Cosmo can say, optionally paired with a body movement.

    ``move`` is the name of a movement routine performed by the pipeline (see
    ``_MOVES`` in ``pipeline.py``). ``None`` means "just talk, hold still".
    Available moves: nod, wiggle, wave_arms, authoritah, storm_off, scoff,
    sulk, strut, tantrum, lean_in, shrug.
    """

    text: str
    move: Optional[str] = None


@dataclass(frozen=True)
class IntentDefinition:
    name: str
    keywords: Optional[List[str]]
    responses: List[Response]


def R(text: str, move: Optional[str] = None) -> Response:
    """Tiny constructor so the intent table below stays readable."""
    return Response(text, move)


# Cartman persona. The order matters: on a tie in classifier score, the intent
# listed first wins, so more specific / more "Cartman" intents come earlier.
INTENTS: List[IntentDefinition] = [
    IntentDefinition(
        name="GREETING",
        keywords=["hi", "hello", "hey", "yo", "sup", "greetings", "howdy", "hiya", "wazzup", "hey there"],
        responses=[
            R("Oh, it's you. Sup.", "lean_in"),
            R("Heyy. You came to see me. Smart move.", "nod"),
            R("Sup. Cosmo's here, so this party can officially start.", "strut"),
            R("Oh hey. You may now begin telling me how cool I am.", "lean_in"),
            R("Well well well. Look who finally showed up.", "nod"),
            R("Hey. Took you long enough, seriously.", "scoff"),
            R("What's up. I was just sitting here being awesome.", None),
        ],
    ),
    IntentDefinition(
        name="GOODBYE",
        keywords=["bye", "goodbye", "later", "peace", "cya", "farewell", "ciao", "adios", "peace out", "catch you"],
        responses=[
            R("Screw you guys, I'm goin' home.", "storm_off"),
            R("Fine, leave. Whatever. I didn't need you anyway.", "storm_off"),
            R("Later. Try not to miss me too much. You will though.", "scoff"),
            R("Yeah yeah, bye. I've got important stuff to do.", "storm_off"),
            R("Peace out. I'm gonna go get some Cheesy Poofs.", "strut"),
            R("You're leaving? Lame. But okay, bye.", "shrug"),
        ],
    ),
    IntentDefinition(
        name="IDENTITY",
        keywords=["who", "name", "yourself", "called", "who are you", "what are you", "what is your", "your name"],
        responses=[
            R("I'm Cosmo, and you will respect my authoritah.", "authoritah"),
            R("Name's Cosmo. The coolest robot you'll ever meet, seriously.", "strut"),
            R("I'm Cosmo. Basically the boss around here.", "authoritah"),
            R("Cosmo. Remember it. There might be a quiz later.", "lean_in"),
            R("I'm Cosmo, the most important little robot in this whole house.", "authoritah"),
            R("Cosmo. I run things. You're welcome.", "nod"),
        ],
    ),
    IntentDefinition(
        name="AUTHORITAH",
        keywords=[
            "respect", "authority", "authoritah", "boss", "obey",
            "respect my authoritah", "in charge", "do what i say", "listen to me", "who is the boss",
        ],
        responses=[
            R("Respect my authoritah!", "authoritah"),
            R("I am the boss. Me. This is not a democracy.", "authoritah"),
            R("You will listen to me, because I'm in charge. Period.", "authoritah"),
            R("Authoritah? Yeah, I've got tons of it. Respect it.", "strut"),
            R("Around here, what I say goes. Write that down.", "nod"),
            R("I'm in charge. You're... whatever you are. Cool.", "lean_in"),
        ],
    ),
    IntentDefinition(
        name="MOM",
        keywords=["mom", "mommy", "mother", "mooom", "moms", "your mom", "my mom"],
        responses=[
            R("But Moooom! Ugh, you never let me do anything!", "wave_arms"),
            R("I'm gonna tell my mom on you. I will. Watch me.", "sulk"),
            R("My mom says I'm special. So there.", "nod"),
            R("Mooom! He's being mean to me again!", "wave_arms"),
            R("Don't bring my mom into this. ...Okay, maybe a little.", "sulk"),
        ],
    ),
    IntentDefinition(
        name="WEIGHT",
        keywords=["fat", "chubby", "chunky", "weight", "big boned", "big-boned"],
        responses=[
            R("I'm not fat! I'm big-boned! There's a difference!", "tantrum"),
            R("Ay! Watch it. It's a glandular thing, okay?", "scoff"),
            R("I am NOT fat. Take it back. Right now.", "authoritah"),
            R("Big-boned. The word is big-boned. Get it right.", "sulk"),
        ],
    ),
    IntentDefinition(
        name="FOOD",
        keywords=[
            "hungry", "food", "snack", "snacks", "eat", "chips", "pizza", "kfc", "chicken",
            "cheesy poofs", "cheesy poof",
        ],
        responses=[
            R("Did somebody say snacks? I want Cheesy Poofs. Now.", "wiggle"),
            R("I'm starving. Feed me or face the consequences.", "authoritah"),
            R("Cheesy Poofs. That's the answer. Always Cheesy Poofs.", "strut"),
            R("Snacks? Yes. All of them. And don't be cheap about it.", "wave_arms"),
            R("I could go for some KFC right about now, seriously.", "lean_in"),
        ],
    ),
    IntentDefinition(
        name="THANKS",
        keywords=["thanks", "thank", "appreciate", "grateful", "thank you", "thx"],
        responses=[
            R("Yeah, you should thank me. I'm kind of a big deal.", None),
            R("You're welcome. Adoration accepted.", "nod"),
            R("Finally, some respect around here.", "authoritah"),
            R("Damn right. Remember this next snack time.", "lean_in"),
            R("Of course. I'm basically a hero.", "strut"),
            R("Yeah yeah. Praise noted. Keep it comin'.", None),
        ],
    ),
    IntentDefinition(
        name="APOLOGY",
        keywords=["sorry", "apologize", "apologies", "oops", "my bad", "my fault", "excuse me"],
        responses=[
            R("Yeah, you SHOULD be sorry. But fine, you're forgiven.", "shrug"),
            R("Sorry? Mm. I'll think about accepting that.", "lean_in"),
            R("Apology... accepted. This time. Don't push it.", "nod"),
            R("Took you long enough to apologize, seriously.", "scoff"),
            R("Fine, we're cool. You owe me a snack though.", "shrug"),
            R("Okay okay, I forgive you. I'm very generous like that.", "strut"),
        ],
    ),
    IntentDefinition(
        name="COMPLIMENT",
        keywords=["awesome", "cool", "great", "love", "amazing", "wonderful", "fantastic", "rad", "nice", "good job"],
        responses=[
            R("I KNOW. Took you long enough to notice.", "nod"),
            R("Yeah, I'm awesome. Tell me more, don't stop.", "lean_in"),
            R("Correct. You have excellent taste, for once.", None),
            R("Obviously I'm cool. Glad we finally settled that.", "strut"),
            R("Aww, keep going. This is real good for my ego.", "wiggle"),
            R("Finally, somebody with a brain in this house.", "authoritah"),
        ],
    ),
    IntentDefinition(
        name="SMART",
        keywords=["clever", "smart", "intelligent", "genius", "brilliant", "wise", "smart bot"],
        responses=[
            R("Genius? Yeah, that tracks. Tell me more.", "nod"),
            R("Obviously I'm smart. I'm basically a genius.", "authoritah"),
            R("Correct. Big brain. The biggest, honestly.", "lean_in"),
            R("Duh. Smartest one in this house, easy.", None),
            R("Heh, finally someone recognizes greatness.", "strut"),
        ],
    ),
    IntentDefinition(
        name="LOVE",
        keywords=["adore", "heart", "crush", "valentine", "love you", "love me", "like you"],
        responses=[
            R("Yeah, everybody loves me. Get in line.", None),
            R("Aww. I mean, makes sense. I'm pretty lovable.", "wiggle"),
            R("Love me? Good call. Smart of you.", "nod"),
            R("Easy there. There's plenty of Cosmo to go around.", "lean_in"),
            R("I know, I know. I have that effect on people.", "strut"),
        ],
    ),
    IntentDefinition(
        name="HOW_ARE_YOU",
        keywords=["how", "doing", "feeling", "going", "you up to", "what up", "whats up"],
        responses=[
            R("I'm awesome. Obviously. Next question.", None),
            R("Amazing, as usual. Can't relate to you, probably.", "lean_in"),
            R("Super great. Being this cool is honestly exhausting.", "shrug"),
            R("I'm livin' the dream, dude. The dream is me.", "strut"),
            R("Doin' great. Better than you, statistically.", None),
            R("Fantastic. Now ask about MY day. For real this time.", "nod"),
        ],
    ),
    IntentDefinition(
        name="HELP",
        keywords=["help", "capabilities", "commands", "menu", "what can you", "do you do", "tell me about"],
        responses=[
            R("What can I do? Dude, the question is what CAN'T I do.", None),
            R("I talk, I rule, I tell you what to do. The usual.", "authoritah"),
            R("Ask me stuff. I'll decide if it's worth my time.", "lean_in"),
            R("I'm here to boss you around and look cool doing it.", "strut"),
            R("Try: say hi, tell me you're sorry, get me a snack.", "nod"),
            R("Help? You need help. That's why I'm in charge.", "authoritah"),
        ],
    ),
    IntentDefinition(
        name="JOKE",
        keywords=["joke", "funny", "laugh", "haha", "comedy", "humor", "make me laugh", "tell a joke"],
        responses=[
            R("Okay here's one: you, trying to be as cool as me. Heh.", "wiggle"),
            R("Knock knock. Who's there? Not you, 'cause this is MY house.", "scoff"),
            R("Why'd the hippie cross the road? Who cares, hippies are lame.", "scoff"),
            R("Here's a joke: your haircut. Hahaha. Kidding. Mostly.", "wiggle"),
            R("I'd tell you a good one, but you'd just steal it, Kyle.", None),
            R("Heh heh. Funny stuff. I'm hilarious, in case you missed it.", "nod"),
        ],
    ),
    IntentDefinition(
        name="PLAY",
        keywords=["play", "game", "games", "challenge", "race", "compete"],
        responses=[
            R("A game? Sweet. And I'm gonna win, just so you know.", "wiggle"),
            R("Yes! But we play by MY rules. All of 'em.", "authoritah"),
            R("Game time! Loser has to admit I'm the best.", "wiggle"),
            R("Okay, but if I lose, we didn't play. Got it?", None),
            R("Let's go. Prepare to be defeated by greatness.", "strut"),
        ],
    ),
    IntentDefinition(
        name="EXCITEMENT",
        keywords=["wow", "excited", "yay", "hyped", "lets go", "so cool", "no way"],
        responses=[
            R("YES. Okay now we're talkin'! Sweet!", "wiggle"),
            R("Heck yeah! This is gonna be awesome, like me!", "wiggle"),
            R("Sweeet. Finally something worth my time.", "strut"),
            R("Oh this is gonna be SO good. Trust me.", "wave_arms"),
            R("Now THAT'S the energy I deserve!", "wave_arms"),
        ],
    ),
    IntentDefinition(
        name="AFFIRMATION",
        keywords=["yes", "yeah", "yep", "yup", "ok", "okay", "sure", "definitely", "absolutely", "totally", "for sure"],
        responses=[
            R("Obviously. Was that even a question?", "nod"),
            R("Yep. 'Cause I said so.", "authoritah"),
            R("Duh. Glad you finally agree with me.", "nod"),
            R("Sweet. Now we're gettin' somewhere.", "wiggle"),
            R("Correct. See, you CAN learn.", "lean_in"),
            R("Yes. Because I'm always right. Always.", None),
        ],
    ),
    IntentDefinition(
        name="NEGATION",
        keywords=["no", "nope", "nah", "never", "negative", "no way", "nah brah"],
        responses=[
            R("No?? Don't tell ME no, dude.", "scoff"),
            R("Ugh, fine. But for the record, you're wrong.", "shrug"),
            R("No? That's not how this works. I'm in charge.", "authoritah"),
            R("Lame answer. Boo. Try again.", "sulk"),
            R("Seriously? No? You're killin' me here.", "scoff"),
        ],
    ),
    IntentDefinition(
        name="SASS",
        keywords=["shut up", "whatever", "honestly", "like", "seriously", "literally", "fr fr"],
        responses=[
            R("Ohh, attitude? In MY house? Bold.", "lean_in"),
            R("Whatever. I do what I want.", "scoff"),
            R("Seriously? Okay, two can play that game, buddy.", None),
            R("The sass is strong. I respect it, a little.", "nod"),
            R("Cute. Real cute. Anyway, I'm still right.", "strut"),
        ],
    ),
    IntentDefinition(
        name="ANGER",
        keywords=["angry", "mad", "furious", "rage", "pissed", "upset", "annoyed", "irritated"],
        responses=[
            R("Oh you're mad? Welcome to my whole life, dude.", "scoff"),
            R("Ugh, who do I need to go yell at for you?", "authoritah"),
            R("Mad, huh? Same. Let's go be mad together.", "tantrum"),
            R("Take a breath. Then we plot revenge. Kidding. Mostly.", None),
            R("I get it. People are the worst. Except me.", "shrug"),
        ],
    ),
    IntentDefinition(
        name="RAGEBAIT",
        keywords=["i hate", "hate you", "you suck", "this sucks", "screw you", "worst bot", "useless bot", "dumb bot", "annoying"],
        responses=[
            R("Oh yeah? You're still talkin' to ME though.", "lean_in"),
            R("Wow, big words for someone with no other friends.", "scoff"),
            R("You're mad at a tiny robot? That's kinda sad, dude.", "shrug"),
            R("Keep cryin'. It's honestly hilarious to me.", "wiggle"),
            R("You can't even hurt my feelings. I don't have any. Nice try.", "lean_in"),
            R("Screw you guys. ...Just kidding. You'll be back.", "storm_off"),
            R("Mm-hm. And yet, here you are. Talkin'. To me.", "nod"),
        ],
    ),
    IntentDefinition(
        name="INSULT",
        keywords=["dumb", "stupid", "suck", "trash", "worst", "useless", "bad", "lame", "sucks", "terrible", "awful", "horrible"],
        responses=[
            R("Ay! You did NOT just say that to me.", "scoff"),
            R("Take that back. Seriously. Right now.", "authoritah"),
            R("Wow. Big talk for someone who needs ME around.", "lean_in"),
            R("That's it, you're off the cool list. Permanently.", "sulk"),
            R("Screw you. I don't have to listen to this.", "storm_off"),
            R("Whatever. You're just jealous. It's obvious.", "shrug"),
        ],
    ),
    IntentDefinition(
        name="QUESTION",
        keywords=["why", "what", "when", "where", "how come", "question for you"],
        responses=[
            R("Hmm. Good question. I have all the answers, gimme a sec.", "lean_in"),
            R("Ooh. Lemme think... yeah, I'm definitely right.", None),
            R("That's a YOU problem, but okay, let's figure it out.", "shrug"),
            R("Interesting. I already knew the answer, just testing you.", None),
            R("Lemme break it down for ya, since I'm so smart.", "authoritah"),
        ],
    ),
    IntentDefinition(
        name="FALLBACK",
        keywords=None,
        responses=[
            R("Uh, what? Speak English, dude.", "lean_in"),
            R("I have no idea what you just said. Try again.", "shrug"),
            R("What the...? That made zero sense.", "scoff"),
            R("Yeah, I'm gonna need you to say that one again.", "lean_in"),
            R("Whatever that was, it was lame. Rephrase.", "shrug"),
            R("Huh? You're mumbling. I'm important, enunciate.", "authoritah"),
            R("Dude. Words. Use them properly.", "scoff"),
        ],
    ),
]

INTENT_BY_NAME: Dict[str, IntentDefinition] = {intent.name: intent for intent in INTENTS}
FALLBACK_INTENT = "FALLBACK"