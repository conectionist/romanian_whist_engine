# AI strategies

The engine ships six `IStrategy` implementations. This document covers what each one
does, how they compare, and where they fall down. For the interface itself, see
[README.md](README.md#supplying-moves--imoveprovider).

| Strategy | Header | Bids | Plays |
|---|---|---|---|
| [`RandomCardStrategy`](#randomcardstrategy) | `strategies/RandomCardStrategy.h` | A random legal number | A random legal card |
| [`FirstCardStrategy`](#firstcardstrategy) | `strategies/FirstCardStrategy.h` | 0, or 1 when 0 is barred | The first legal card in hand order |
| [`DuckingStrategy`](#duckingstrategy) | `strategies/DuckingStrategy.h` | 0, come what may | Sheds its highest cards whenever they cannot win |
| [`LowRiskStrategy`](#lowriskstrategy) | `strategies/LowRiskStrategy.h` | The tricks its hand will take anyway — usually 0 | Wins cheaply while it owes tricks, then ducks |
| [`CyborgStrategy`](#cyborgstrategy) | `strategies/CyborgStrategy.h` | The count with the best expected score, from exact odds on every card | Keeps a plan of which cards win and which go, and re-plans every trick |
| [`ReckonerStrategy`](#reckonerstrategy) | `strategies/ReckonerStrategy.h` | What its sampled deals say the hand will take | Searches imagined deals, and plays to hit its bid or break yours |

The first four are the subject of most of this document: they are stateless, they decide
from the context they are handed, and they are short enough to read in one sitting. The
last two are none of those things. Both remember the whole round, and both have to be
**registered as an observer** to work at all. Cyborg reasons by rule and exact arithmetic;
the Reckoner searches. Their sections are [below](#cyborgstrategy), and each has its own
design document: [romanian-whist-cyborg-ai.md](romanian-whist-cyborg-ai.md) and
[romanian-whist-reckoner-ai.md](romanian-whist-reckoner-ai.md).

---

## Why they all want to bid zero

The scoring rule shapes every decision here. From `Scoreboard::calculateRoundScore`:

```
hit your bid   ->  +5 + bid
miss your bid  ->  -|bid - actual|
```

plus a ±10 swing for five consecutive hits or misses.

Two things follow. First, **the reward for accuracy dwarfs the reward for ambition**: bidding
0 and taking 0 scores +5, while bidding 3 and taking 3 scores +8. Tripling your bid buys
you 60% more points for far more risk. Second, **missing is cheap** — usually just -1. A
strategy that quietly hits a small bid every round beats one that occasionally hits a big
one, and the streak bonus rewards it again for the consistency.

So "aim for zero" is not timidity, it is what the scoring asks for.

---

## RandomCardStrategy

The baseline. Bids uniformly over `[0, hand.size()]`, drawing from a range one short and
stepping over `*forbiddenBet` so the result is uniform across what is legal without
rejecting and redrawing. Plays a uniformly random legal card.

Useful as a control when measuring another strategy, and as a stand-in for an unpredictable
opponent. It seeds a fresh `std::random_device` on every call, so it is not reproducible
and cannot be unit tested as written.

## FirstCardStrategy

Bids 0, or 1 when the final-bidder rule bars 0. Plays `legalCards[0]` — the first legal
card in hand order, which is deal order, so it is arbitrary but deterministic.

It bids as well as `DuckingStrategy` does and still scores half as much, which makes it a
clean measurement of what the play alone is worth. Its problem is that hand order
correlates with nothing: it will drop an ace under a seven and then be stuck holding a
king it can no longer get rid of safely.

## DuckingStrategy

Bids 0 and means it. Never chases a trick, not even one it has been forced to bid for —
when 0 is barred it bids 1, takes the resulting -1, and carries on ducking.

Play is `chooseDuckingCard` (below) on every trick. The single-minded version of the idea,
and the one to reach for when you want a predictable opponent.

Worth knowing: **0 is almost never actually barred**. `getForbiddenBet()` returns 0 only
when the earlier bids already add up to exactly the trick count, which is rare — in a
four-ducker table it never happened once in 200 games. The usual barred value is the trick
count itself, not 0. So this strategy's "come what may" is less of a sacrifice than it
sounds.

## LowRiskStrategy

Counts the tricks its hand will take whether it wants them or not, bids exactly that, then
plays to the bid.

**Bidding** — `countLikelyWinners` is deliberately crude:

```
winners  = (aces outside the trump suit)
         + (trump cards ranked King or Ace)
if trump length > 2:
    winners += trump length - 2      // the tail of a long trump holding wins by force
return min(winners, hand.size())
```

In a no-trump round (an empty `trump`, the 8-card rounds) only aces count.

If that number is the one the final-bidder rule bars, it steps **down** rather than up.
Shedding one trick it expected to win is something a ducking hand can usually manage;
forcing an extra trick out of a hand that has none is a coin flip. Stepping down from 0 is
not possible, so 0 becomes 1 — and a round always has at least one trick, so 1 is safe.

**Playing** — while `tricksWon < bet` it uses `chooseWinningCard`; once the bid is met or
overshot, every further trick is a penalty, so it switches to `chooseDuckingCard` for the
rest of the round.

---

## CyborgStrategy

The rule-based remembering player. Where the four above decide from the `PlayContext` in
front of them, Cyborg keeps a record of the whole round — every card played and by whom,
every bid, every void a seat has shown — and prices each card in its hand against the cards
still live. It never samples or searches, so a decision costs about half a microsecond, and it
is deterministic: two Cyborgs dealt the same game play it identically.

**Bidding** depends on the round size:

| Tricks in the round | How it bids |
|---|---|
| 1 | Bid 1 if the card wins more than 6 times in 13 — where the expected scores of bidding 1 and 0 cross. Seats in the middle also read the bids before them; the last seat bids 0 unless 0 is barred |
| 2 | A table of hand shapes when leading; otherwise summed card values, rounded |
| 3–7 | Each card's chance of winning, combined into the chance of each trick count, and the bid with the best expected score |
| 8 (no trump) | Aces and unbroken runs from the top count in full; cards just below them count in part |

Forehead and Hidden rounds bid 0 — the hand may not be read — and every bid steps off the
barred value.

**Playing** follows a plan made from the bid and remade every trick: which cards are meant to
win and which are meant to go. The plan's mode decides the card —

| Mode | When | Plays for |
|---|---|---|
| `DUCK` | the bid is met | never taking another trick |
| `TAKE` | it owes every remaining trick, or is behind | winning what it can, as cheaply as it can |
| `SHED` | the hand will take more than it bid | losing its surplus winners early |
| `BALANCE` | on schedule | taking tricks in the order that keeps the bid reachable |

Whenever its memory disagrees with the position it is handed, a decision falls back to
`LowRiskStrategy`'s heuristics rather than to anything arbitrary. `getFallbacksTaken()` counts
those, and in healthy play it stays at zero — the tests assert it.

### It has to be registered as an observer, too

The same requirement as the Reckoner's below, for the same reason, and the same answer:

```cpp
SeatSetup seat = makeCyborgSeat("Ana", engine);
```

No seed: there is nothing random in it. Both decisions throw `std::logic_error` if the strategy
was never registered, and `start()` throws if its name matches no seat at the table.

`cyborg::CyborgKnobs` holds the numbers behind the rules. They are development tuning, not a
difficulty dial — Cyborg ships as one strength. The design document's §9 lists them.

### How it scores

From the tournament tests in `tests/CyborgTournamentTests.cpp`, which pin these standings
(points a game, the Cyborg rotated through every seat):

| Table | Cyborg | Others |
|---|---|---|
| 1 Cyborg, 3 LowRisk (80 games) | **75.3** | LowRisk 60.9 |
| 1 Cyborg, 1 Reckoner `medium()`, 2 LowRisk (15 games) | 72.7 | Reckoner **87.5**, LowRisk 60.2 |

Against LowRisk it is ahead at every table size — by 16.2, 10.9, 13.1 and 9.4 points a game
at 2, 3, 5 and 6 players, and by 3.7 in the 8-1-8 structure (smaller samples). The Reckoner is
stronger, and pays for it in search.

Where it falls down is listed in the design document's §8. In short: it has no model of
opponents; it treats their hands as independent draws; its odds say little in the no-trump
rounds, where every card is dealt; and it does not solve the last few tricks exactly, which
the design (§6.5) leaves for later.

---

## ReckonerStrategy

The strong one. Where the four stateless strategies decide from the `PlayContext` in front
of them, the Reckoner keeps a model of the whole round — every card played, who has shown
void in which suit, what everyone bid — and searches over deals consistent with it before
each decision.

Four presets, which differ only in the numbers behind them:

| Preset | Memory | Deals sampled per card | Notable |
|---|---|---|---|
| `ReckonerKnobs::easy()` | the current trick only | none | Plays a random legal card 15% of the time |
| `ReckonerKnobs::medium()` | the full round | 60 | Beats `LowRiskStrategy` clearly |
| `ReckonerKnobs::hard()` | the full round | 200 | Weighs breaking the score leader's bid at double |
| `ReckonerKnobs::brutal()` | the full round | 400 | Solves the last two tricks exactly |

Every knob behind them is tabulated in
[romanian-whist-reckoner-ai.md](romanian-whist-reckoner-ai.md) §9. `Easy` samples nothing,
so it costs about what `LowRiskStrategy` costs; `Brutal` is the only strategy here whose
decisions are measurable in milliseconds rather than microseconds.

### It has to be registered as an observer

This is the one thing to get right, and the type system cannot help you with it.
`ReckonerStrategy` derives from `IStrategy` **and** `IGameObserver`. Everything it plays
from arrives through the observer callbacks, and `onGameStarted` is also where it resolves
its own seat by matching its name against the table — so it must reach the engine before
`start()`, not merely before `run()`.

`makeReckonerSeat()` does the whole dance and is what you should use:

```cpp
SeatSetup seat = makeReckonerSeat("Ana", engine, reckoner::ReckonerKnobs::hard(), seed);
```

If you build one by hand instead, all three steps are yours: `setPlayerName()`,
`engine.addObserver()` before `engine.start()`, and keeping the engine and the seat
together. Skip the registration and both decisions throw `std::logic_error` naming the
omission — deliberately, because the alternative is a bot that plays legal, weak cards
forever out of a memory it never received, with nothing anywhere to say why.

A client that builds seats away from its engine — a GUI whose setup screen runs on another
thread, say — has to carry the observer pointer to wherever the engine is constructed and
register it there. That is a real constraint on such clients and not merely a convention.

### Seeding

Like `RandomCardStrategy`, it draws: the deal sampler, the `Easy` preset's random-card roll
and the bid noise all come from one `std::mt19937` seeded once at construction. Pass a seed
for a reproducible game; without one it takes `std::random_device` once. Note that
reproducibility holds for the same deal against the same opponents — the generator is
consumed per decision, so a different card upstream shifts everything after it.

---

## The shared machinery

Both low-risk strategies are about a dozen lines each, because the judgement calls live in
`strategies/TrickHeuristics.h`. Reuse it rather than re-deriving any of this.

### What makes "safe" exact

**A card that does not beat the trick's current winner cannot take the trick** — no matter
how many players are still to come, since they can only push the winner higher.

That turns "is this risky?" from a probability estimate into a lookup. No opponent
modelling, no counting seats, no card tracking. It is the reason these strategies are short.

The ranking comes from `CardValidator::beats` and `CardValidator::getWinningCard`, which
are the same functions `GameEngine::determineTrickWinner` uses. A strategy asking "would
this win?" reasons with the very rule that will later declare the winner, so the two cannot
drift apart.

### Danger ordering

`isMoreDangerous` ranks cards by how much trouble they are to a player who would rather not
win: **any trump outranks any plain card**, and rank decides within that. A low trump is
more dangerous than a high plain card, because it takes tricks its rank alone would never
justify.

Everything else is built from that ordering:

| Function | When leading | When following |
|---|---|---|
| `chooseDuckingCard` | Nothing is safe, so lead the **least** dangerous card | Dump the **most** dangerous card that still cannot win. If every legal card would win, take the trick with the **least** dangerous one — cheapest possible, and a later player may still overtake |
| `chooseWinningCard` | Lead the **most** dangerous card | Take it with the **least** dangerous card that wins. If none can win, discard the **least** dangerous and keep the good ones for a trick still worth having |

The ducking case is the one that matters — see [Worked examples](#worked-examples) below for
what it does in practice.

---

## Worked examples

Every choice below is real output from the strategies, not an illustration written by hand.
`Random` is left out because its answer is a coin toss.

### Single decisions

**1. A queen is led, holding jack and nine.** No trump.

```
hand   J♥ 9♥        table  Q♥        winning  Q♥
    LowRisk  J♥      Ducking  J♥      FirstCard  J♥
```

The jack cannot beat the queen, so it is safe — shed the biggest card that cannot win. All
three happen to agree.

**2. The same hand, but a ten is led.**

```
hand   J♥ 9♥        table  10♥       winning  10♥
    LowRisk  9♥      Ducking  9♥      FirstCard  J♥
```

Now the jack *would* win. Duck with the nine and keep the jack for a trick where something
bigger has already been played. `FirstCardStrategy` plays the jack and takes a trick it
bid against — this single position is the whole difference between them.

**3. Void in the lead suit and in trump — a free discard.**

```
hand   A♦ 8♦ 7♣     table  Q♥        winning  Q♥        trump  none
    LowRisk  A♦      Ducking  A♦      FirstCard  A♦
```

An off-suit discard can never win, so *everything* is safe. That makes it the best possible
moment to get rid of the ace, and both low-risk strategies take it.

**4. Void in the lead suit, but holding trumps — forced to trump.**

```
hand   A♠ 7♠        table  Q♥        winning  Q♥        trump  ♠
    LowRisk  7♠      Ducking  7♠      FirstCard  A♠
```

The rules force a trump when you cannot follow suit, so this trick is being won whether the
bot likes it or not. It wins with the cheapest card it can. `FirstCardStrategy` throws the
ace of trumps away on the same trick.

**5. Somebody has already trumped in.**

```
hand   A♥ 9♥        table  10♥ 7♠     winning  7♠        trump  ♠
    LowRisk  A♥      Ducking  A♥      FirstCard  A♥
```

The seven of trumps is beating the ten of hearts, so the ace of hearts is now *safe* — it
cannot take a trick that has already been trumped. Out it goes.

**6. Every legal card would win — cornered.**

```
hand   A♥ K♥        table  9♥        winning  9♥        trump  none
    LowRisk  K♥      Ducking  K♥      FirstCard  A♥
```

No safe card exists. Take the trick as cheaply as possible: the king still leaves a player
behind us able to overtake with the ace, and keeps our own ace for a hopeless trick later.

**7. Leading, with a trump in hand.**

```
hand   A♥ 7♠ 8♦     table  (leads)              trump  ♠
    LowRisk  8♦      Ducking  8♦      FirstCard  A♥
```

Nothing is safe when leading, so lead the least dangerous card. Note the ordering: the
**seven** of trumps is considered more dangerous than the **ace** of hearts, because a low
trump takes tricks its rank alone would never justify.

### Where the two low-risk strategies part company

The same position, changing only what `LowRiskStrategy` still owes:

```
hand   J♥ 9♥        table  10♥       winning  10♥

  bid 1, won 0  ->  LowRisk  J♥      Ducking  9♥
  bid 1, won 1  ->  LowRisk  9♥      Ducking  9♥
```

While it still owes a trick, LowRisk takes this one with the jack. Once the bid is paid it
ducks exactly like the ducker. `DuckingStrategy` never switches.

### Bidding

```
hand                    trump  barred   LowRisk  Ducking  FirstCard
7♥ 8♦ 9♣                  ♠      -          0        0        0
7♥ 8♦ 9♣                  ♠      0          1        1        1
A♥ 8♦ 9♣                  ♠      -          1        0        0
A♥ A♦ K♠                  ♠      -          3        0        0
A♥ A♦ K♠                  ♠      3          2        0        0
7♠ 8♠ 9♠ 10♠ J♠           ♠      -          3        0        0
A♥ K♣ 9♥                 none    -          1        0        0
```

Row 4 counts two aces plus the trump king. Row 5 is the same hand with 3 barred, stepping
**down** to 2. Row 6 is five small trumps and no honours at all — `5 - 2 = 3`, because the
tail of a long trump holding wins by force once everyone else is out. Row 7 is a no-trump
round, where only the ace counts.

### A full hand

One `LowRiskStrategy` seat through a complete 7-card round, trump ♦, played against the
other three strategies. It bid **1** — one ace outside trump, no trump honours, no length —
and made exactly 1.

```
hand   10♠ 8♥ 8♠ 9♦ 9♥ 9♠ A♥        trump ♦        bids 1

trick 1   table  7♣ J♣ A♣     winning A♣    owes yes   plays 9♦
trick 2   table  (leads)                    owes no    plays 8♥
trick 3   table  Q♠ J♠        winning Q♠    owes no    plays 10♠
trick 4   table  8♣ 9♣ 10♣    winning 10♣   owes no    plays A♥
trick 5   table  8♦           winning 8♦    owes no    plays 9♥
trick 6   table  J♥ 7♠ 10♥    winning J♥    owes no    plays 9♠
trick 7   table  K♣ A♠ K♦     winning K♦    owes no    plays 8♠
```

- **Trick 1** — void in clubs and holding a trump, so the rules force the 9♦, which trumps
  the ace of clubs and takes the trick. The bid is paid on the very first trick, by
  accident rather than design, and the bot spends the rest of the round ducking.
- **Trick 2** — leading with no trumps left. Leads its lowest.
- **Trick 3** — must follow spades. The queen is winning, so the 10♠ is safe: dump the
  highest spade that cannot win.
- **Trick 4** — the payoff. Void in clubs, no trumps left, so this is a free discard and
  *nothing* can win. The ace of hearts, the most dangerous card in the hand, goes for free.
- **Tricks 5–6** — more free discards, shedding 9♥ then 9♠ in danger order.
- **Trick 7** — one card left, no decision.

Note what did **not** happen: the ace of hearts was never led, never played into a trick it
could win, and never got stranded as the last card. That is the entire strategy in one hand.

---

## Measured behaviour

> **Measured on engine 3.0.0, and not re-run against 4.0.0.** The numbers below predate the
> Forehead and Hidden rounds, in which `LowRiskStrategy` now bids 0 rather than counting
> winners. That moves two rounds per game at most, and only the appended ones — but it does
> move them, so treat these as the shape of each strategy rather than as figures that
> reproduce exactly.

200 full 1-8-1 games per line-up, four bots, played headless. "tricks/round" is pinned by
arithmetic: the four seats always share out the round's tricks between them, so across the
1-8-1 schedule the table average is ~0.92 whatever anyone does. Only the spread is a
strategy's doing.

**One of each**

| seat | avg score | hit bid | bid 0 | tricks/round |
|---|---|---|---|---|
| LowRisk | **60.6** | 54.8% | 54.2% | 0.96 |
| Ducking | 40.5 | 50.2% | 93.6% | 0.76 |
| FirstCard | 21.8 | 43.8% | 93.1% | 1.00 |
| Random | 0.8 | 33.0% | 35.5% | 0.97 |

Ducking and FirstCard bid zero about equally often, so the 19-point gap between them is
the *play* alone.

**LowRisk against three Random** — 65.0 against -6.5, -5.3, -7.6.

**LowRisk against three Ducking** — 53.7 against roughly 33. Its trick count rises to 1.08
per round: the duckers are all shedding, so somebody has to take those tricks, and LowRisk
is the only one at the table willing to.

**Mirror matches** are where it gets interesting:

| line-up | avg score each | hit bid | bid 0 |
|---|---|---|---|
| Four LowRisk | ~65 | 57% | 56% |
| Four Ducking | ~30 | 48% | **100%** |

Four duckers all bid 0, and the tricks still have to go somewhere — so they collectively
guarantee that most of them miss. Four LowRisk bots score more than twice as much against
each other, because bidding for the tricks you are going to be handed anyway is the whole
point. **The ducking strategy is parasitic: it needs someone else at the table willing to
take tricks.**

---

## Known weaknesses

- **`countLikelyWinners` over-bids.** LowRisk bids 0 only ~55% of the time against
  Ducking's ~94%, and the `trump length - 2` term is the most suspect part. It still scores
  best, so it has been left alone — but it is the first knob to turn.
- **No memory.** Neither strategy tracks which cards have been played in earlier tricks, so
  it cannot know its king has become the highest card left in a suit. `PlayContext` carries
  only the current trick.
- **Leading is guesswork.** With no card tracking there is no way to know whether a low
  card is actually safe to lead, so `chooseDuckingCard` just leads its smallest and hopes.
- **No opponent modelling.** Bids are visible via `Round::getBet`, but nothing reads them.
  A bot could duck far more confidently knowing the player behind it has bid 0 too.
- **Ducking needs a victim**, as the mirror match above shows.

---

## Writing your own

Implement `IStrategy` and add the source to the library's explicit source list in
`CMakeLists.txt`.

```cpp
#include <romanian_whist/strategies/IStrategy.h>
#include <romanian_whist/strategies/TrickHeuristics.h>

class MyStrategy : public romanian_whist::IStrategy
{
public:
    unsigned int getBestBet(const romanian_whist::BetContext& context) override
    {
        // A Forehead or Hidden round says this hand may not be read to bid on,
        // so there is nothing to count and the bid is 0.
        unsigned int bet =
            context.roundType == romanian_whist::RoundType::Normal
                ? romanian_whist::heuristics::countLikelyWinners(context.hand, context.trump)
                : 0u;

        // The barred bid applies to every round, blind ones included, and the
        // engine throws std::logic_error on it - so step off it last, after
        // every other consideration has had its say. Stepping down from 0 is
        // not an option, and a round always has at least one trick, so 1 is
        // always legal there.
        if(context.forbiddenBet && bet == *context.forbiddenBet)
            bet = (bet == 0u) ? 1u : bet - 1u;

        return bet;
    }

    std::optional<romanian_whist::Card> getBestChoice(
        const romanian_whist::PlayContext& context) override
    {
        // cardValidator is inherited from IStrategy.
        const auto legal = cardValidator.getLegalCards(context.hand, context.trump, context.leadSuit);

        if(legal.empty())
            return std::nullopt;

        return romanian_whist::heuristics::chooseDuckingCard(context, legal);
    }
};
```

Three things to get right:

1. **Return a card, not a position.** Strategies reason in cards; `AiMoveProvider` finds the
   chosen card in the hand and hands the engine the index. You never see that index, and you
   never need to — but it does mean the card you return must be one that is actually in
   `context.hand`.
2. **Handle an empty `context.trump`.** The 8-card rounds have no trump, and `std::optional`
   is how that is said. Everything in `TrickHeuristics` takes it in that form already.
3. **Guard the empty case** and return `std::nullopt`, which the engine treats as "no legal
   play" — an error, not a pass.

And one that is easy to miss:

4. **Check `context.roundType` in `getBestBet`.** In a Forehead or Hidden round the engine
   still hands you the **real** hand — it has no other one to give — but the round says the
   bidder cannot see it. A strategy that reads it anyway is bidding on information it is not
   supposed to have. `LowRiskStrategy::getBestBet` (`src/strategies/LowRiskStrategy.cpp`) is
   the one bundled strategy this affects, and it bids 0 rather than counting winners — then
   still steps to 1 if 0 is the barred bid, because a blind round bars a bid like any other.
   The other three never read the hand to bid, so they need no guard.

Both `getBestBet` and `getBestChoice` are pure functions of their arguments, so a
deterministic strategy can be tested without standing up a `GameEngine` at all.
