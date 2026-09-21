# "Cyborg" — a half-point, hand-shape AI for Romanian Whist

A rule-based strategy built on one idea: **split every suit down the middle**. The top half of the
ranks are *big cards*, the bottom half are *small cards*, and almost every judgement the bot makes
is a count of big cards, small cards and trumps, resolved with half-points.

Where [Reckoner](romanian-whist-reckoner-ai.md) answers "what does simulation say?", Cyborg answers
"what would a decent human say, made exact" — which is where the name comes from. The rules are the
ones a club player would recognise; the arithmetic underneath them is machine-exact, with perfect
recall of every card played. Half human heuristic, half calculator, and neither half hidden.

It has no rollouts and no sampler. Its bid comes from a
lookup table and an arithmetic sum; its play comes from a plan it forms at bid time and re-checks
every trick. That makes it cheap (microseconds, not milliseconds), deterministic, and — the reason
to want it — **explainable**: every decision can be printed as one sentence a player will recognise.

Two properties are load-bearing and neither is optional:

1. **It knows the whole round.** Not just the trick in front of it — every card played by every
   seat, every bid, every void it has proved. `PlayContext` cannot supply that, so Cyborg is an
   `IGameObserver` as well as an `IStrategy` (§2).
2. **It plays to its bid.** The bid is not a guess it then abandons. Bidding produces a *plan* —
   which cards are meant to win, which are meant to be thrown — and card play exists to execute and
   repair that plan (§5, §6).

Numbers that are judgement rather than derivation are collected as knobs in §9.

---

## 0. Architecture

Four components. Only the first is new state; the rest are pure functions over it.

| Component | Role | Cost |
|---|---|---|
| **Memory** (§2) | Everything seen this round: cards played and by whom, bids, proved voids, the dead turn-up | O(1) per observed card |
| **Odds kit** (§3) | `beaters()`, `pLeadWins()`, `pHolds()` and the two exact certainty tests, over the live-card set | ~1 µs |
| **Bidding** (§4) | Round-size-specific rules: probability for 1 trick, a shape table for 2, an expected-score bid over per-card odds for 3–7, aces-and-runs for 8 | ~5 µs |
| **Plan + play** (§5, §6) | Intended winners / intended losers, a DUCK / TAKE / BALANCE mode, and a card choice per mode | ~10 µs |

The odds kit is the piece worth building first and building well: the same `pLeadWins()` that decides
a one-card round's bid also decides which junk card to throw on trick six of an eight-card round.
One function, used everywhere, is what keeps the bidding and the play honest with each other.

---

## 1. Vocabulary: the halves

The deck holds two ranks per player per suit ([RULES §2](RULES.md#2-deck-size)). **The big cards are
the top half of those ranks — exactly `playerCount` of them per suit.**

| Players | Ranks in the deck | Big cards | Small cards |
|---|---|---|---|
| 2 | A K Q J | A K | Q J |
| 3 | A K Q J 10 9 | A K Q | J 10 9 |
| 4 | A K Q J 10 9 8 7 | A K Q J | 10 9 8 7 |
| 5 | A K Q J 10 … 5 | A K Q J 10 | 9 8 7 6 5 |
| 6 | A K Q J 10 … 3 | A K Q J 10 9 | 8 7 6 5 4 3 |

The engine already computes the offset this needs. `common::DeckProfile` (`strategies/common/CardMask.h`)
carries `ranksPerSuit = 2N` and `minRankInt = 1 + 2·(6 − N)`, and `cardRank(cardToId(c, N), 2N)`
returns the 0-based rank index within the deck. So:

```
rankIndex(card) = int(card.rank) − profile.minRankInt        // 0 .. 2N−1
isBig(card)     = rankIndex(card) >= N
isSmall(card)   = !isBig(card)
```

Reuse `CardMask.h` rather than re-deriving this. It is already tested (`tests/CardMaskTests.cpp`),
it throws on a rank that is not in the deck for that player count, and its bitmask helpers
(`maskAbove`, `maskBelow`, `maskSuit`) are exactly what §3 needs.

Four categories name every card, and the whole of §4 is written in them:

| | Trump | Non-trump |
|---|---|---|
| **Big** | `BT` | `BN` |
| **Small** | `ST` | `SN` |

In an 8-trick round there is no trump ([RULES §4](RULES.md#4-trump)), so only `BN` and `SN` exist.

---

## 2. Round memory

### 2.1 Why an observer is required

`PlayContext` shows the current trick and nothing else, so a card played *after* Cyborg's in a trick
is never seen through that seam. `BetContext` is worse for bidding: it carries `isFirstPlayer` and
`forbiddenBet`, but **not the bids already placed** — and the rounding rule in §4.1 needs their sum
at every seat, not just the last one.

Neither is derivable. `forbiddenBet` yields `R − Σ(previous bids)`, but only for the final bidder,
and it is empty even for them once the bids already exceed `R`
([RULES §5](RULES.md#5-bidding-restriction)). So a middle bidder cannot reconstruct the table.

Two ways out; take the first.

1. **Implement `IGameObserver` alongside `IStrategy`**, the way `ReckonerStrategy` already does. No
   engine change, and the observer callbacks hand you a `const GameEngine&` with everything on it.
2. Grow `BetContext` with a `bidsSoFar` vector. Cleaner to consume, but it changes a public struct
   every provider sees.

### 2.2 State

Use **`common::RoundMemory`** (`strategies/common/RoundMemory.h`). It is exactly this state and
these inferences, extracted from what was `reckoner::ReckonerTracker` so that both AIs count
cards through one implementation — `ReckonerTracker` now derives from it and adds only its soft
constraints and its cross-round opponent model, neither of which Cyborg wants. The fields:

```
playerCount N, roundTrickCount R, mySeat, opener
trumpCard, trumpSuit         // both empty in 8-trick rounds
live      : Mask             // not yet played AND not the turned-up card
playedBy[p], handSize[p]
voidMask[p]                  // suits p has proved it does not hold
bids[p]                      // UNBID until placed
won[p]
```

Derived, per decision: `unseen = live & ~myHand`, and `H = Σ_{p ≠ me} handSize[p]`, the number of
unseen cards that are actually in somebody's hand. `|unseen| − H` is the dead pile — the undealt
remainder, which is zero in 8-trick rounds.

**The turn-up is dead, and its rank matters.** `live` excludes it, so a turned-up trump ace makes
your trump king the top trump for the entire round, and every test in §3 picks that up for free.

### 2.3 Updates

`RoundMemory::recordCardPlayed()` already does all of this — it is repeated here because it is
what the rest of the document reasons from, not because it needs writing. The two hard inferences
(also [Reckoner §1.3](romanian-whist-reckoner-ai.md#13-update-rules-and-hard-inferences)) are the
whole of what Cyborg infers about opponents; it deliberately does **not** carry Reckoner's soft
rank-gap constraints, because those only pay off inside a sampler:

```
on (p plays c) in a trick with lead suit L:
    live &= ~bit(c);  playedBy[p] |= bit(c);  handSize[p]--
    if L exists and suit(c) != L:
        voidMask[p] |= L                            // failed to follow
        if trumpSuit exists and suit(c) != trumpSuit:
            voidMask[p] |= trumpSuit                // must-trump rule: no trump either
```

That second line is worth as much as everything in §3. One off-suit discard proves a player holds
neither the lead suit nor a single trump, which turns a pile of maybes in your hand into certainties
for the rest of the round.

### 2.4 Hooks

| Callback | What Cyborg does with it |
|---|---|
| `onRoundStarted` | `initRound(N, R, mySeat, engine.getRoundLeaderSeat(), engine.getCurrentTrumpCard())` |
| `onBetPlaced(seat, bet)` | record it; the running sum feeds §4.1 |
| `onCardPlayed(seat, card)` | the update above |
| `onTrickWon(winner, n)` | `won[winner]++`, clear the trick, **re-plan** (§5) |
| `onRoundScored` | clear the plan; nothing carries across rounds |

Cyborg keeps no cross-round opponent model. It is stateless between rounds by design — that is a
large part of why it is testable.

**Knowing which seat you are.** A strategy that does not know its own seat cannot read `bids[]`
or `handSize[]`, so this has to be settled before the first bid. Note that `makeReckonerSeat()`
does *not* call `setSeat()` — it cannot, because a seat index does not exist until `start()` has
built the table. What it does is store a name, and `onGameStarted` matches that name against
`engine.getPlayers()` to resolve the seat (`src/strategies/ReckonerStrategy.cpp:210-234`).

Copy that, with one change: **throw if the name matches nothing.** Reckoner silently leaves its
seat disengaged and later behaves as though it were seat 0 in a four-player game, which is a
worse failure than an exception — `onGameStarted` is the last thing `start()` does, so a throw
there surfaces from the caller's `start()` before a card is dealt.

---

## 3. The odds kit

Everything below operates on masks over the live set. Four functions; §4 and §6 are written in
terms of them and contain no probability of their own.

### 3.1 Who can beat this card

```
beaters(c, pool):                                   // pool is a Mask, usually `unseen`
    m = maskAbove(c, profile) & pool                // maskAbove is already suit-restricted
    if trumpSuit exists and suit(c) != trumpSuit:
        m |= pool & maskSuit(trumpSuit, profile)    // any trump beats any plain card
    return m
```

The trump clause is the one people forget. A non-trump card is only unbeatable once **both** the
higher cards of its suit and every trump are gone.

### 3.2 The two exact tests

Before reaching for a probability, ask whether the answer is already certain. These are cheap and
they are right, which is more than an estimate can claim.

**Sure winner (leading).** `beaters(c, unseen) == 0`. Nothing left in the game beats it; leading it
takes the trick, full stop.

In practice, test this against `reachableBeaters()` (§3.3) rather than `beaters()`. A beater that no
live opponent may legally hold is in the dead pile, so it is not a beater — and that distinction is
what turns a high probability into a certainty in the late tricks.

**Cannot win (following).** A card that does not beat the trick's current best card cannot take the
trick, no matter how many players are still to come, because later players can only push the winner
higher. This is the exactness `TrickHeuristics` is built on — use `CardValidator::getWinningCard()`
and `CardValidator::beats()`, the same functions `GameEngine::determineTrickWinner()` uses, so the
strategy and the referee cannot drift apart.

There is a third, narrower certainty that pays for itself in the endgame:

**Sure loser (leading).** Three conditions, and **the first is the one that is easy to forget**:

1. **A beater exists at all** — `reachableBeaters(c, unseen) != 0`.
2. Nothing is dead (`|unseen| == H`, which is every 8-trick round and the late tricks of any round).
3. No live card of `suit(c)` ranks below `c`.

Then whoever holds the top live card of the suit is *forced* to play it, a trump held by a player
out of the suit is *forced* to ruff, and `c` loses. That is the exact form of the reasoning in §7.5.

Condition 1 is not decoration. Without it the other two are both **vacuously true** for a card
nothing can beat — hold `A♥` in an 8-trick round with no lower hearts live and conditions 2–3 both
hold, so the test reports a sure loser for a card that always wins. Check it first.

Test condition 1 with **`reachableBeaters()`**, the same set the sure-winner test uses, not with
`beaters()`. In a consistent deal they agree here: nothing is dead, so every beater is in a hand, and
its holder is not void in its suit. They disagree only when the memory contradicts itself — every
opponent recorded out of hearts while the ace of hearts is still unplayed, which only a missed
callback can produce — and with `beaters()` that position makes the king a sure winner **and** a sure
loser at once. The play rules in §6 rely on those two excluding each other, so both must ask the same
question.

**Do not add "no opponent is void in `suit(c)`".** An earlier version of this section listed it as a
fourth condition and called it redundant. It is not redundant, it is wrong: a void in a third player's
hand changes nothing about the beater's holder. With `K♥` led, `A♥` in X's hand and Y known void in
hearts, X must play the ace or Y ruffs — `c` loses either way, and the extra condition would report
it as a possible winner. Worse, when only trumps beat `c`, nobody holds the suit at all, so the
condition fails in every such position.

### 3.3 P(this wins if I lead it)

```
pLeadWins(c):
    U = unseen
    if H == 0 or |U| == 0: return 1.0                       // nobody holds a card
    B = reachableBeaters(c, U)                              // §3.1, void-corrected
    if B == 0: return 1.0                                   // §3.2, sure winner
    pNoneOut = hyper0(|B|, |U|, H)                          // exact: no beater is in any hand

    // A beater is out there. I still win if its holder chooses to duck rather than
    // play it — which they can only do if they hold a lower card of the same suit.
    Lo   = U & maskBelow(c, profile)                        // maskBelow is already suit-restricted
    hbar = max(1, H / (N − 1))                              // mean opponent hand size, floored at 1
    pCanDuck = (|Lo| == 0) ? 0 : 1 − hyper0(|Lo|, |U| − 1, hbar − 1)
    return pNoneOut + (1 − pNoneOut) · DUCK_PROPENSITY · pCanDuck
```

`hyper0(k, n, h)` — "P that `h` cards drawn without replacement from `n` contain none of the `k`
marked ones" — is already implemented and tested at `common::hyper0`
(`strategies/common/Hypergeometric.h`, `tests/EvaluatorTests.cpp`).

Three things to understand about this function, because it is the load-bearing one:

- **`pNoneOut` is exact, not an estimate.** The opponents' hands together form a uniform `H`-subset
  of the `|unseen|` cards, so one hypergeometric answers "is a beater out there at all". No
  independence assumption is involved.
- **The duck term is the approximation.** It asks whether *some* opponent holding a beater also
  holds a card they would rather throw, using the mean hand size. It ignores that the beater and the
  low card must be in the *same* hand for the duck to happen, and it ignores that a player who needs
  tricks will take yours regardless. `DUCK_PROPENSITY` (default 0.6) absorbs the second of those.
  Upgrade path if it matters: sample opponent hands the way `reckoner::Sampler` does and count.
- **When nothing is dead, `pNoneOut` collapses to 0 or 1.** In an 8-trick round every unseen card is
  in somebody's hand, so the only question left is whether its holder will play it — the duck term
  becomes the entire answer. That is the right shape: in the no-trump rounds, card *counting* beats
  card *odds*, and this formula degrades into counting on its own.

**Void correction — restrict the pool, not the draw.** If a player is known void in `suit(c)` they
cannot hold a beater of that suit, though they *can* still ruff. The correction is to drop from `B`
any card that **no live opponent may legally hold**, because such a card is necessarily in the dead
pile:

```
reachableBeaters(c, U):
    m = 0
    for each opponent o != me with handSize[o] > 0:
        if not void(o, suit(c)):                    m |= maskAbove(c, profile) & U
        if trumpSuit and suit(c) != trumpSuit and not void(o, trumpSuit):
                                                    m |= maskSuit(trumpSuit) & U
    return m
```

With no voids proved this is exactly `beaters(c, U)`, so nothing in §7 changes. Late in a round it
produces the *certainty* that is the point of tracking voids at all: when every seat that could beat
you is void, `pLeadWins` is exactly `1.0` — not merely high.

> **Do not split `H` instead.** An earlier version of this section said to compute the suit and trump
> halves of `B` against different `H` values. That makes `pNoneOut` a product of two
> hypergeometrics, which is **not** the single one even when no void is known: at `|U| = 30`,
> `H = 12`, `|B_suit| = 2`, `|B_trump| = 8` the correct value is `0.001456` and the product is
> `0.002630` — 1.81× too high, with no void involved at all. The two draws are not independent; they
> come from one deal. Restricting the pool has no such problem, because it changes *what is marked*
> rather than *how many cards are drawn*.

### 3.4 P(this holds up if I play it now)

The following-seat version. Identical, except the pool of people who can still hurt you is only the
seats yet to act in this trick, and follow-suit restricts them:

```
pHolds(c):                                  // c must already beat the current best card
    remaining = seats after me in this trick
    B = beaters(c, unseen restricted to what `remaining` could legally play)
    H' = Σ_{o in remaining} handSize[o]
    return hyper0(|B|, |unseen|, H')
```

A seat known void in the lead suit contributes only trumps to `B`. A seat void in both contributes
nothing — it cannot beat you at all, and that is a certainty, not an estimate.

A seat with **no** void shown contributes the lead suit **and** trumps. "Not proved void" is not "must
follow": it may be out of the suit without having shown it yet, and then it must ruff. Counting only
the lead suit for such a seat reports an unbeatable ace as a certain winner while trumps are live
behind it.

---

## 4. Bidding

Four rules, chosen by round size. They do not interpolate into one another and are not meant to: a
one-card round is a probability question, an eight-card round is a counting question, and the rounds
in between are shape questions.

### 4.1 The rounding rule (§4.3's non-leader rule and §4.5)

§4.4 used this rule too until the re-calibration after Phase 3; it now chooses its bid by expected
score instead.

Three of the four rules produce a fractional point total. Resolve it by asking whether the table has
already claimed more tricks than exist.

```
resolveFraction(raw, previousBidSum, R):
    if raw is an integer: return clamp(raw, 0, R)
    if previousBidSum + raw > R:  return clamp(floor(raw), 0, R)   // over-subscribed: round down
    else:                         return clamp(ceil(raw),  0, R)   // under-subscribed: round up
```

`previousBidSum` is the sum of the bids already placed this round, and is 0 for the round leader.
This needs the memory of §2.

The logic: [the bids never sum to the trick count](RULES.md#5-bidding-restriction), so the table is
always collectively wrong. When it is collectively greedy, tricks are scarcer than the players think
and your marginal half-trick will not materialise — round down. When it is collectively timid,
tricks have to land somewhere and some of them will land on you — round up.

> **Note on the source spec.** The worked examples given for this rule in the 2-trick section were
> inverted (`0.5 → 1` labelled *floor*, `0.5 → 0` labelled *ceil*). The words are the intent and the
> 3–7-trick examples confirm it: floor when over-subscribed, ceil when under-subscribed.

**Refinement — `USE_EXPECTED_REMAINING` (default on).** As written, the test ignores the
seats who have not bid yet, which systematically inflates the round leader's bid: with
`previousBidSum = 0` an opener almost always ceils. Charging the unbid seats their fair share fixes
it without touching the rule's shape:

```
expectedRemaining = (number of seats still to bid) · R / N
if previousBidSum + raw + expectedRemaining > R: floor else ceil
```

On an 8-trick round with `raw = 4.5` and four players, the literal rule bids 5 and this one bids 4.
It shipped off and was then measured (1,600 seat-balanced games per table size against heuristic
bidding, with §4.4 and §4.5 as they now stand): a net gain, better at 2, 3, 5 and 6 players and
slightly worse at 4, so it is on.

### 4.2 One-trick rounds

Play is entirely forced — you hold one card and you will play it — so the bid is nothing but
`p = P(my card takes the trick)`, and the scoring rule turns that into a threshold.

With `t` tricks taken against a bid `b`, the score is `5 + b` on a hit and `−|b − t|` on a miss
([RULES §8](RULES.md#8-scoring)), and one-card rounds are excluded from the streak counters, so
there is no streak term to weigh:

```
EU(bid 1) = 6p − (1 − p) = 7p − 1
EU(bid 0) = −p + 5(1 − p) = 5 − 6p
bid 1  iff  7p − 1 > 5 − 6p  iff  p > 6/13 ≈ 0.4615
```

This is the same threshold [Reckoner §6.2](romanian-whist-reckoner-ai.md#62-one-trick-rounds)
derives; the two strategies differ in how they get `p`, not in what they do with it.

**Getting `p`.** It is `pLeadWins(myCard)` with `DUCK_PROPENSITY` forced to 0 — nobody can duck when
nobody has a choice of card. Which reduces it to one exact hypergeometric:

```
p = hyper0(|beaters(myCard, unseen)|, |unseen|, N − 1)
```

and note `beaters()` already handles both cases correctly:

- **Leading a trump.** Only higher trumps beat it.
- **Leading a plain card.** Higher cards of the suit beat it, *and so does every trump* — an
  opponent holding a single trump is void in your suit by definition, so the must-trump rule fires
  and they ruff.
- **Not leading.** The formula is unchanged. A lone trump is played on any lead (you follow trump,
  or you are void and must ruff), so its odds are identical to leading it. A lone plain card can
  only win when the trick is led in its suit — which is the case the rule below never has to
  evaluate, because it only reaches the probability branch when holding a trump.

**The rules, by seat.** The probability above is the whole answer for the round leader. For the
seats in the middle the spec prefers a table, because bidding order carries information that the
card alone does not:

| Position | Situation | Bid |
|---|---|---|
| **First (round leader)** | — | `p > 6/13 ? 1 : 0` |
| **Middle** | no trump in hand | 0 |
| | holding a trump, and someone after the opener has bid 1 | `p > 6/13 ? 1 : 0` |
| | holding a trump, everyone before me bid 0 | 1 |
| | holding a trump, only the opener bid 1, and my trump is small | 0 |
| | holding a trump, only the opener bid 1, and my trump is big | 1 |
| **Last** | 0 is legal | 0 |
| | 0 is barred | 1 |

The middle rows read as a claim about what a bid means. A *middle* seat bidding 1 has looked at its
own card and decided it wins — that is evidence, so fall back to the arithmetic. The *opener* bidding
1 is much weaker evidence: they had nothing to go on but the odds, and they may simply hold a
mediocre trump. Against that, a big trump is still worth backing and a small one is not.

The last row is not resignation, it is arithmetic: the last bidder in a one-card round is choosing
between 0 and 1 with one of them possibly barred, so there is nothing to decide.

### 4.3 Two-trick rounds

**As round leader**, this is a lookup on the shape of the two cards, and it fixes the opening lead
as well as the bid. The lead is half the strategy and §6 must honour it.

| Hand | Bid | Lead | Reasoning |
|---|---|---|---|
| `BT` + `BT` | **2** | the higher `BT` | The first draws out whatever trumps exist; nothing bigger is likely to be left for the second. |
| `BT` + `BN` | **2** | the `BT` | Same draw, and it clears the ruff that would otherwise kill the `BN` on trick 2. |
| `BT` + `ST` | **1** | the `ST` | Flush the other trumps cheaply; the `BT` then takes trick 2 almost for certain. |
| `BT` + `SN` | **1** | the `SN` | Burn the junk while the table is still fighting; the `BT` takes trick 2. |
| `ST` + `BN` | **1** | the `BN` | If everyone follows, it wins. If someone ruffs it, a trump that could have beaten your `ST` has just been spent — so the `ST` wins trick 2 instead. Either way, exactly one. |
| `ST` + `ST` | **1** | the lower `ST` | The first pulls a bigger trump; the second has a real chance. |
| `ST` + `SN` | **1** | the `SN` | Someone takes it, then the `ST` takes trick 2. |
| `BN` + `BN` | **1** | the lower `BN` | Genuinely a coin flip — 0, 1 and 2 are all live. Bid the middle and burn the weaker card first. |
| `BN` + `SN` | **1** | the `SN` | Someone takes it; the `BN` takes trick 2. |
| `SN` + `SN` | **0** | the lower `SN` | Nothing here wins on purpose. Duck twice. |

The pattern behind the table, worth stating because it is what makes the table memorable: **you lead
the card you are *not* planning to win with.** The exception is the two rows that bid 2, where you
lead the big trump precisely to protect the other card.

None of these need checking against `forbiddenBet` — it binds only the final bidder, and the round
leader is never that.

**Not the round leader**, points, and non-trumps count for nothing:

```
raw = 1.0 · (big trumps) + 0.5 · (small trumps)
bid = resolveFraction(raw, previousBidSum, 2)
```

That a side-suit ace scores zero here is not an oversight. In a two-card round every opponent holds
two cards, so a void is common and a ruff is cheap; an ace you cannot protect is not a trick, it is
a card you will lose to a three of trumps. Trump length is the only thing that reliably wins a short
round.

### 4.4 Three- to seven-trick rounds

Each card scores the probability that nothing an opponent holds can beat it, priced with the odds of §3:

```
rawMidRound(hand):
    raw = 0
    for c in hand:
        higher = unseen cards of c's suit ranked above c              // the turn-up is dead, not unseen
        p = hyper0(|higher|, |unseen|, handsOutstanding)              // no opponent holds one
        if c is not a trump:
            suitUnseen = unseen cards of c's suit
            for each opponent s:
                p *= 1 − hyper0(|suitUnseen|, |unseen|, handSize[s])  // s still holds the suit: no ruff
        raw += p
    return raw

dist = P(exactly t tricks), t = 0..R, treating each card's odds as an independent trial
bid  = the b != forbiddenBet that maximises  Σ_t dist[t] · (t == b ? 5 + b : −|b − t|)
```

**The bid is an expected-score choice, not a rounded average.** RULES §8 pays `5 + b` for a hit and
takes `|b − t|` for a miss, so the question is not "how many tricks will this hand take on average" but
"which bid scores best across the ways the round can go" — the same question §4.2 answers for one card,
where it gives the 6/13 threshold. In a 3–7 trick round most hands' likeliest outcome is 0 or 1 tricks,
and 0 is the easiest bid to hit exactly; this rule finds that from the odds. It ignores the bids already
made — the table's count cannot change the hand's own distribution — and a barred bid falls to the next
best. Ties go to the lower bid.

A trump that no unseen trump outranks scores exactly 1. Every other card scores less, and less again
at a bigger table: more opponents means more hands a higher card can be in, and more chances that one
of them is void in a plain suit. That is the point of the rule — the bid falls with the player count
the way the fair share of tricks, `R / N`, does.

> **Why not half-points.** This section first read `raw = 0.5 · (all trumps) + 0.5 · (big non-trumps)`.
> A random hand scores about 0.31 of those points per card *at every table size* — a quarter of the
> cards are trumps, and half of the rest are big — while a card's fair share of tricks is `1/N`. So
> it under-bid at two players, was about right at three, and at six bid 1.7 tricks a round against
> 0.9 won. Rescaling it to the fair share fixed the average but not which hands it rated highly: it
> still hit its bid far less often than `LowRiskStrategy`. Measured over 1,600 seat-balanced games per
> table size against heuristic bidding, heuristic play on both sides, this rule together with §4.5's
> `g == 1` branch and `USE_EXPECTED_REMAINING` moved six players from −35.9 points a game to −4.5.

The rule approximates in three places, each pessimistic. Opponents' hands are treated as independent
draws for the void test; a void opponent is assumed to hold a trump to ruff with; and each card is
priced alone, so a long trump suit — whose small trumps win once the big ones are drawn — scores less
than it is worth. It also still loses 3–7 trick rounds to `LowRiskStrategy`'s bidding at four to six
players, which is partly that heuristic play rewards bidding low; §6 is where that is expected to move.

> **Why not round the average.** Until the re-calibration after Phase 3 this section summed the odds
> to `raw` and rounded it with §4.1. The sum was right on average — bid minus tricks won within ±0.3 —
> yet at four to six players Cyborg made its bid 35–46% of the time in these rounds against
> LowRisk's 47–60%, because it bid 1 on three hands in four where LowRisk's low bids and ducking hit 0
> more reliably. The expected-score bid, measured on 100 seeds per table size, moved one Cyborg
> against LowRisk from +8.1, +6.7, +5.0, +2.2, −3.8 points a game at 2–6 players to +15.5, +12.0,
> +11.0, +8.9, +7.0.

`resolveFraction` clamps to `[0, R]`, but for §4.3's non-leader rule and §4.5 the clamp never binds:
both score at most 1.0 per card, so `raw ≤ R`.

### 4.5 Eight-trick rounds

No trump, and the whole deck is dealt, so nothing is dead and nothing is unknowable in principle —
only unlocated. The bid is a count of cards that will win by force.

```
countTricks(hand):
    raw = 0
    for each suit S:
        mine = my cards of S, sorted high to low
        winnersSoFar = 0
        for c in mine:
            g = number of cards of S ranked above c that are NOT mine
            if g == 0:
                raw += 1.0 ; winnersSoFar += 1                  // top of the suit: a certain trick
            else if g <= winnersSoFar:
                raw += GAP_CREDIT ; winnersSoFar += 1           // likely: my higher cards flush the gap
            else if g == 1:
                raw += 0.5                                      // one stranger above: a fair chance it falls first
    return raw

bid = resolveFraction(raw, previousBidSum, 8)
```

The first branch is the spec's "1 point per ace, then 1 point per card of a decreasing sequence from
the ace". Walking the suit downward and asking `g == 0` produces exactly that, and it cannot count
an ace twice.

The second branch is the spec's "catch", made into a rule. Holding `A K J` of hearts, the jack is not
in the run — but leading the ace and then the king pulls two rounds of hearts out of the table, and
the queen very probably falls to one of them. So the jack is not a certainty, but it is much more
than the half-point a bare big card gets. The test `g <= winnersSoFar` says: *I have at least as many
established leads in this suit as there are cards outstanding above this one.* Checked against the
spec's own examples:

- `A K J` — jack has `g = 1` (the queen), `winnersSoFar = 2` → credited. Suit total 2.75.
- `A K 10` — ten has `g = 2` (queen, jack), `winnersSoFar = 2` → credited. Suit total 2.75.
- `A K Q J` — all `g = 0` → 4.0, and the gap branch never fires.
- `A Q` — queen has `g = 1` (the king), `winnersSoFar = 1` → credited. Suit total 1.75, which is
  optimistic for what is really a finesse; this is the case `GAP_CREDIT` is tuned against.

The third branch scores a card that is neither the top of its suit nor flushed by my own leads. It
gets half a trick only when exactly one card it does not hold ranks above it. The whole deck is dealt
and every trick takes `N` cards, so a suit of `2N` goes round about twice, and a card with two or more
strangers above it almost never lives to win. The branch first read `else if isBig(c)`, and "big" is
the top `N` ranks: the right cut at two players, but at six it gave half a trick to a nine with five
strangers above it. Measured against heuristic bidding, the change turned eight-trick rounds at four to
six players from a loss of 3–11 points a game into a gain of about one.

`GAP_CREDIT` defaults to 0.75. Lower it to 0.6 when not the round leader: cashing a gap winner
requires being on lead in that suit repeatedly, and a seat that does not open the round may never get
the tempo back. This is the only place where "am I first?" changes the eight-card arithmetic — the
spec is right that the rest is the same count and a hope that the opener eventually leads something
you can take control with.

**Optional refinement — `LENGTH_CREDIT` (default off).** In a no-trump round a long suit wins by
exhaustion: hold six hearts in a four-player game and the other three seats share two, so your fifth
and sixth hearts win outright. Add, per suit:

```
others = 2N − (my count of S)
maxOtherLength ≈ ceil(others / (N − 1)) + 1
raw += 0.5 · max(0, myCount − maxOtherLength − (cards already credited in S))
```

Left off by default because it interacts with `GAP_CREDIT` and can double-count. Measure before
enabling.

### 4.6 The barred bid

If the computed bid is `forbiddenBet`, step **against** the way the fraction was rounded — the
estimate said the true value was on the other side:

```
if we floored: bid += 1
elif we ceiled: bid -= 1
else:           bid = (bid > 0) ? bid − 1 : 1     // integer estimate: prefer shedding a trick
clamp to [0, R]
if bid is still forbiddenBet:                     // the clamp undid the step, at 0 or at R
    step the other way from the original bid
```

Stepping *down* on an integer estimate is `LowRiskStrategy`'s reasoning and it holds here: giving
away a trick you expected to win is something a hand can usually arrange, while manufacturing an
extra trick out of a hand that has none is a coin flip. `R ≥ 1` always, so stepping up from a barred
0 is safe.

### 4.7 Forehead and Hidden rounds

In both, the bidder is not entitled to use their own hand
([RULES §9](RULES.md#9-special-rounds)), and `BetContext` carries no substitute — the other seats'
cards are visible to a *human* in a Forehead round but are not in the struct. So every rule above is
unusable.

Bid 0, or 1 if 0 is barred, and say so in a comment. A strategy that reads `context.hand` in either
round type is cheating, and it is cheating for no gain: these are one-card rounds, excluded from the
streak counters, worth ±1 to get wrong.

---

## 5. The plan

The thing that separates Cyborg from a bot that bids well and then plays randomly. After bidding,
and again at the start of every trick, it partitions its hand:

```
struct Plan {
    Mask winners;       // cards I intend to take tricks with
    Mask losers;        // cards I intend to be rid of
    unsigned need;      // bet − tricksWon
    Mode mode;
};
```

```
rebuild():
    need = bet − tricksWon                  // clamp at 0; overshoot is water under the bridge
    rem  = cards left in hand
    score every card c by pLeadWins(c)
    winners = the `need` highest-scoring cards      (empty when need == 0)
    losers  = the rest

    expected = Σ_{c in winners} pLeadWins(c)
    surplus  = Σ_{c in hand}    pLeadWins(c) − need

    if need == 0                     : mode = DUCK
    elif need >= rem                 : mode = TAKE
    elif expected < need − SLACK      : mode = TAKE      // behind plan: grab what you can
    elif surplus > SLACK              : mode = SHED      // hand is too strong for the bid
                                                          // (no trump: surplus counts certainties only)
    else                             : mode = BALANCE
```

Rebuild every trick rather than committing once. Cards leave the live set constantly, and a queen
that was a maybe on trick one is a certainty on trick five — the plan should notice.

The four modes and what each is for:

| Mode | Means | Wants |
|---|---|---|
| `DUCK` | bid already met | Never take another trick. Every card is a liability, biggest first. |
| `TAKE` | must win every remaining trick, or is behind schedule | Win whatever can be won, as cheaply as possible. |
| `SHED` | the hand will take more tricks than were bid | Get rid of the surplus winners *early*, while other players still have the cards to beat them. |
| `BALANCE` | on schedule, with slack | Take tricks in the order that keeps the plan feasible. |

`SHED` is the mode most rule-based bots lack and the one that saves the most points. A hand that
bid 1 and holds two aces is not safe; it is one trick from a miss, and the fix is to throw an ace
under someone else's trick *now*, not to discover on trick seven that both aces are unavoidable.

---

## 6. Card play

Always start from `CardValidator::getLegalCards()`. The engine validates every card handed to it
([RULES §6](RULES.md#6-play-restrictions)) and rejects an illegal one rather than obeying it.

Two helper notions used throughout:

- **Liability(c)** — how much trouble `c` is if you do not want tricks: `pLeadWins(c)`. Highest
  liability is what a ducking hand throws first.
- **Asset(c)** — the same number, read the other way. Highest asset is what a taking hand keeps.

That is the unification: one score, and which end of it you want depends only on the mode. It also
subsumes `heuristics::isMoreDangerous`, which is the same ordering estimated without memory (any
trump above any plain card, rank within that). Where Cyborg has no useful information — trick one of
a round it has seen nothing of — the two agree; where it has counted cards, `pLeadWins` is strictly
better informed.

### 6.1 Leading

```
lead():
    if mode == TAKE:
        if the suit I am already cashing still holds a sure winner (§3.2):
            cash that suit's highest sure winner        // even if it is now the SHORTER suit
        elif any sure winner exists:
            cash from the suit where I hold the most cards, highest card first
        else:
            lead the LOWEST card of my longest suit     // never a big card into an unknown table
    if mode == SHED:
        lead the highest-liability card among those that are NOT sure winners
        // a sure winner cannot be shed by leading it; save it for a trick someone else opens
        if every legal card is a sure winner: lead the CHEAPEST of them
    if mode == DUCK:
        lead argmin pLeadWins
        tie-break: among cards of near-equal (low) win chance, lead the DEAREST
                   (isMoreDangerous: any trump above any plain card, rank within that)
    if mode == BALANCE:
        if there is no trump:
            lead exactly as TAKE does                   // cash the certainties while on lead
        elif my winners are non-trump and trumps are still live:
            cash the best winner now — it is only getting more ruffable
        elif every winner is a certainty: lead the safest loser   // the bid is already paid for
        else:                             lead argmax pLeadWins among `winners`
```

The three paragraphs below are the *reasoning* behind the branches above, not corrections to them —
each one was found while implementing, and the block has been rewritten to match. Phases 0, 1 and 2
each found real errors in this document; these were §6.1's.

**The DUCK tie-break is not a detail.** Holding `Q♥` and `7♥` with the king the only heart left
alive, both are safe to lead — the king's holder must follow suit and must therefore beat you. Lead
the **queen**. The seven will still be safe next trick; the queen might not be, and it is the card
you will struggle to lose later. Throw the expensive safe card, keep the cheap one.

**Correction, found in review: "expensive" is not rank.** The example above is single-suit, so rank
and liability agree and it never settles the *cross-suit* case. §6's ordering does, and it is the one
that holds: **any trump is dearer than any plain card, whatever the ranks**. Once the big trumps are
gone, a small trump and a plain card both score zero on `pLeadWins`, and a bare-rank tie-break throws
the plain card and keeps the trump — the one card that will later be *forced* to ruff and take the
very trick `DUCK` is trying not to win. Measured across random trump-round positions, bare rank
disagreed with this on 9% of `DUCK` leads.

**Cashing from the longest suit** in `TAKE` mode is what makes the eight-card bid honest. §4.5
credited your `J♥` on the assumption that leading `A♥` and `K♥` first would flush the queen. If play
cashes the ace and then switches suits, the bid was a lie. Lead the ace, then the king, then the
jack, in that suit, before starting another.

**Correction, found implementing this.** "The suit where I hold the most cards" cannot produce that
sequence by itself: once `A♥` is gone hearts is the *shorter* suit, and the rule switches to diamonds
before the jack is cashed — §7.3's own example contradicts the line above it. So the rule carries one
piece of state: **keep cashing the suit you are part-way through while it still holds a certainty**,
and choose by length only when it does not. That is `PlaySituation::cashingSuit`, and it is the only
thing card play remembers from one trick to the next.

Set it from a lead that was itself a **cash** — a `TAKE`-mode lead of a sure winner — and clear it on
any other lead. Recording *every* lead, as a first cut did, means a `DUCK` or `BALANCE` discard leaves
a hint behind, and the next `TAKE` lead then cashes that junk suit instead of starting the run this
rule exists to protect: §7.3 inverted.

**`TAKE` with no sure winner leads low, not high.** Leading the best card you hold into an unknown
table is exactly what the "no aces" paragraph below says not to do, so the branch above reads "the
lowest card of my longest suit" where an earlier draft said `argmax pLeadWins`.

Two edges the pseudocode leaves open, settled in the implementation: when every legal card is a sure
winner, `SHED` has nothing to shed and spends the cheapest of them instead of the dearest; and
`BALANCE` counts itself ahead of plan when **every winner it is counting on is a certainty**, which is
when it leads a loser rather than cashing.

**That test is an equality, not an inequality.** `expected` is the sum of the `need` best `pLeadWins`
scores and `pLeadWins` is capped at 1, so `expected` can never *exceed* `need`: written `expected >=
need` it reads like a margin, but the only way to satisfy it is for every one of those cards to have
returned exactly `1.0` from one of §3.2's certainty early-outs. A hand holding one certainty and one
near-certainty is *not* ahead of plan and cashes instead. The code says this with a named
`winnersAreCertain()` rather than leaving the reader to work out that the inequality is unreachable.
Whether a slack-based threshold (`expected >= need - slack`, mirroring the behind-plan test in §5)
plays better is a Phase 4 measurement, not an assumption to bake in here.

**No aces, and needing tricks.** Do not lead a big card into an unknown table — whoever holds the
card above it simply takes it, and you have spent your best card for nothing. Lead your *lowest*
card in your longest suit instead: it flushes the guards, costs nothing, and gives your big cards a
second round of the suit to work in.

### 6.2 Following

```
follow():
    legal = CardValidator::getLegalCards(hand, trump, leadSuit)        // no playedCards arg
    best  = CardValidator::getWinningCard(playedCards, leadSuit, trump)
    canWin = { c in legal : CardValidator::beats(c, best, ...) }
    safe   = legal \ canWin                            // exact (§3.2): these cannot take the trick

    if mode == DUCK or mode == SHED:
        if safe is non-empty: play argmax liability over `safe`
        else:                 play argmin liability over canWin   // forced to win; win cheapest
    if mode == TAKE:
        if canWin is non-empty:
            if I am last to act:  play argmin liability over canWin        // cheapest that wins
            else:                 play the cheapest c in canWin with pHolds(c) >= HOLD_THRESHOLD,
                                  falling back to argmin liability over canWin
        else:                     play argmin liability over `safe`        // keep the winners
    if mode == BALANCE:
        alignment rule, below
```

Note the asymmetry in the last discard: a **ducking** hand throws its *biggest* safe card, a
**taking** hand throws its *smallest*. Same list, opposite ends, and getting it backwards is the
single most expensive bug available in this file.

`pHolds()` matters when players are still to act. The cheapest card that beats the current winner is
often not cheap enough — if three seats are still to play and your nine is only beating a seven, you
have spent a card and will probably still lose the trick. `HOLD_THRESHOLD` (default 0.5) says: pay
for a trick you will probably keep, otherwise duck and keep your powder dry.

### 6.3 The BALANCE alignment rule

The real "hit it exactly" decision: you can win this trick and you are not sure you should. Compare
the two hands you would be left with.

```
w = cheapest card in canWin ; d = best card to throw from `safe`
feasibility(n, H) = | Σ_{top n cards of H by pLeadWins} pLeadWins − n |     // 0 is perfect

A = feasibility(need − 1, hand \ {w})       // take it
B = feasibility(need,     hand \ {d})       // duck it
choose the smaller; on a tie, duck
```

One ply, no rollouts, and it captures what actually goes wrong: hands that miss their bid usually
miss because they were forced into a trick they had already spent the cards to avoid, or because
they ducked a trick and then found that every remaining card was a winner. Comparing the *shape* of
the two futures catches both.

Tie goes to ducking because a taken trick cannot be given back, while a ducked trick often comes
round again.

### 6.4 Two-trick rounds

If §4.3's table chose the bid, it also chose the opening lead — play it, and do not let §6.1
second-guess it. The bid and the lead are one decision there; a table that bids `ST + BN → 1` and
then leads the small trump has thrown the reasoning away.

From trick two onward the general machinery applies, and with one card left there is nothing to
decide anyway.

### 6.5 Endgame certainty

When `|unseen| ≤ ENDGAME_CARDS` (default 8), stop estimating. Enumerate the ways the unseen cards
can be distributed among the hands consistent with `handSize[]` and `voidMask[]`, play each
candidate card against every distribution with everyone following §6.1–§6.3, and pick the card that
hits the bid most often.

Optional, and worth it: the last three tricks are where an exact bid is won or lost, and where the
`pLeadWins` approximation is at its weakest — the sample is small and the hands are correlated in
exactly the way §3.3 assumes away.

---

## 7. Worked examples

Numbers below are computed from the formulas above, not sketched.

### 7.1 A one-card round, leading (4 players)

Trump is `J♥`, turned up and therefore dead. I hold `10♥` and I am the round leader.

```
deck 32 → live = 31 (the turn-up is out) → unseen = 30 (my card is out too)
beaters(10♥) = Q♥ K♥ A♥                     — the jack is dead, not a beater
H = 3 (three opponents, one card each)
p = hyper0(3, 30, 3) = (27/30)(26/29)(25/28) = 0.720
```

**0.720 > 6/13, so bid 1.** `EU(1) = 7(0.720) − 1 = 4.04` against `EU(0) = 5 − 6(0.720) = 0.68`.

Worth noting what the "90%" figure in the spec is and is not. Three of the thirty unseen cards beat
mine, so a *single* random opponent card beats me 10% of the time — that part is right. But there
are three opponents, and the three misses compound: `0.9 × 0.897 × 0.893 = 0.72`. The per-card
fraction is the intuition; the hypergeometric is the number to threshold against. They diverge fast
as the table grows, and always in the direction of over-confidence.

### 7.2 A five-trick round, third to bid (4 players, trump ♠)

Hand: `A♠ 9♠ K♥ 8♥ 7♦`; the turn-up is `8♠`. Unseen: 32 − 1 − 5 = 26 cards, of which the three
opponents hold `H = 15`. One opponent's five cards miss every heart with probability
`hyper0(6, 26, 5) = 0.236` (six hearts unseen), and every diamond with `hyper0(7, 26, 5) = 0.177`.

```
A♠   no higher trump                                           → 1.000
9♠   10 J Q K♠ above:  hyper0(4, 26, 15)                        → 0.022
K♥   A♥ above:         hyper0(1, 26, 15) × (1 − 0.236)³ = 0.423 × 0.446 → 0.189
8♥   9 10 J Q A♥ above: hyper0(5, 26, 15) × (1 − 0.236)³        → 0.003
7♦   seven ♦ above:    hyper0(7, 26, 15) × (1 − 0.177)³        → 0.000
                                                          raw   = 1.214
```

As independent trials, those odds give the hand's trick count: `P(1) = 0.790`, `P(2) = 0.205`,
`P(3) = 0.005` — the ace makes 0 impossible. The expected score of each bid:

| Bid | Expected score |
|---|---|
| 0 | −1.214 |
| **1** | **+4.529** |
| 2 | +0.637 |

So the hand bids **1**, whatever the table has already bid. Barred from 1 as the last bidder, it bids
**2**, not 0: the ace makes a bid of 0 a certain miss.

(This example used to show §4.1's rounding bidding 1 or 2 depending on the bids already made. Measured,
rounding the expected count was the wrong question in these rounds — see §4.4.)

Under the half-point rule this hand scored 1.5, and the king of hearts alone was worth half a trick.
Priced, it is worth under a fifth of one: an opponent holds the ace with probability 15/26, and even
when nobody does, each of three opponents has nearly a one-in-four chance of being out of hearts.

### 7.3 An eight-trick round, leading (4 players, no trump)

Hand: `A♥ K♥ J♥  A♦ Q♦ 9♦  8♠  7♣`.

```
♥  A: g=0 → 1.00   K: g=0 → 1.00   J: g=1 (Q) ≤ 2 → 0.75          = 2.75
♦  A: g=0 → 1.00   Q: g=1 (K) ≤ 1 → 0.75   9: g=3 → 0             = 1.75
♠  8: g=6 → 0          ♣  7: g=7 → 0                              = 0
                                                            raw   = 4.50
```

Literal §4.1 rule: opener, `previousBidSum = 0`, `0 + 4.5 ≤ 8` → ceil → bid 5.
With `USE_EXPECTED_REMAINING`, the default: `0 + 4.5 + 3·(8/4) = 10.5 > 8` → floor → **bid 4**.

Four is the better bid — `A♥ K♥ A♦` are solid, the `J♥` is likely, the `Q♦` is a guess — and this is
the case that refinement exists for.

Then play it the way it was counted: `A♥`, `K♥`, `J♥`, then `A♦`. Cashing `A♥` and switching to
diamonds would abandon the assumption that earned the `J♥` its 0.75.

### 7.4 Which junk to throw

`DUCK` mode, holding `Q♥ 7♥ J♠`, on lead.

| Live in the suit | `pLeadWins(Q♥)` | What to do |
|---|---|---|
| `K♥` only | 0 — the king's holder must follow, and must beat me | Lead `Q♥`. Safe *and* it is the card hardest to lose later. |
| `K♥` and `10♥` | > 0 — one player may hold both and duck with the ten | Lead `7♥`. The queen is now a real risk. |

Then ask the same question of `J♠`: which spades are still live, can any of them beat it, how
likely is their holder to be able to duck. There is no separate rule for spades — it is
`pLeadWins()` again, on a different suit.

The reasoning in the second row is the whole reason `pLeadWins` has a duck term. The danger is never
"a bigger card exists"; it is "a bigger card exists *and* its holder has something cheaper to throw".

### 7.5 The certainty behind 7.4

In an eight-trick round nothing is dead, so `hyper0(|B|, |unseen|, H)` with `|unseen| == H` returns 0
whenever any beater exists — the beater is definitely in somebody's hand. `pLeadWins(Q♥)` is then
exactly `DUCK_PROPENSITY · pCanDuck`, and with `K♥` the only live heart, `|Lo| = 0` makes it exactly
zero. Not an estimate: leading the queen there **cannot** win. That is the third exact test in §3.2,
falling out of the general formula rather than needing a special case.

---

## 8. What this will get wrong

Written down so the test suite can aim at it.

| Weakness | Why | Mitigation |
|---|---|---|
| Opponent hands treated as independent draws | §3.3's duck term ignores that the beater and the low card must be in one hand | `DUCK_PROPENSITY`; or sample, as Reckoner does |
| No opponent modelling | It never learns that a seat over-bids or always ducks | Out of scope by design; that is what Reckoner is for |
| Two-trick table is shape-only | It ignores the other bids entirely when leading | Consider blending with the point rule when `previousBidSum` is extreme |
| Bid and play can disagree | §4.5 credits gap winners that §6.1 must then cash correctly | Test it directly: assert the eight-card lead order |
| No tempo reasoning | It cannot plan to lose a trick in order to regain the lead later | The `BALANCE` alignment rule approximates it at one ply |
| Side-suit aces score zero at two tricks | Correct on average, wrong when nobody is void | Accept; the round is worth ±2 |
| `pLeadWins` is nearly blind without trumps | With every card dealt, it is 0 for any card with a higher card still out, apart from the duck term. As first written, the `BALANCE` lead and the `SHED` test read that term as strength and §6 play lost 1.3–5.9 points a game to heuristic play at 3–6 players | Without trumps, `BALANCE` leads as `TAKE` does and `SHED` counts only certainties (Phase 3c). Now level or ahead except about −1 at five players; other rules that rank uncertain no-trump cards by `pLeadWins` deserve the same suspicion |

### An observer removed mid-game is not detected at its cause

`common::RoundMemory::roundInitialised` is set by `initRound()` and **never cleared**. So the §2.4
guard catches "this strategy was never registered as an observer" but not "this strategy was
registered, played a round, and then removed". After that, the memory keeps answering from the last
round it saw.

What catches the *consequence* is `agreesWith()` — the hand no longer matches `live`, so both
decisions fall through to the heuristic and the bot plays weakly rather than illegally. What is
missing is an error that names the *cause*.

Inherited from the Phase 0 extraction and shared with `ReckonerStrategy`, which has the same gap.
Closing it means a `roundComplete()` call or a monotonic round counter on `RoundMemory`, which is a
change to shared state that both strategies would have to agree on — worth doing deliberately rather
than as a side effect of Cyborg.

---

## 9. Knobs

| Knob | Default | Effect |
|---|---|---|
| `DUCK_PROPENSITY` | 0.6 | How often a player holding a beater and a spare declines the trick. Raise against ducking bots, lower against a table that wants tricks. |
| `GAP_CREDIT` | 0.75 | Value of a non-contiguous top-of-suit card in eight-trick rounds. |
| `GAP_CREDIT_FOLLOWER` | 0.60 | Same, when not the round leader. |
| `HOLD_THRESHOLD` | 0.50 | Minimum `pHolds()` before paying for a trick with players still to act. |
| `SLACK` | 0.75 | Distance from plan before switching to `TAKE` or `SHED`. |
| `ENDGAME_CARDS` | 8 | Unseen-card count at which §6.5 replaces estimation with enumeration. |
| `USE_EXPECTED_REMAINING` | on | The §4.1 refinement. Measured as a net gain, so on by default. Applies to §4.3's non-leader rule and §4.5; §4.4 no longer rounds. |
| `LENGTH_CREDIT` | off | The §4.5 refinement. |

These are development tuning, not a difficulty dial. **Cyborg ships as one strength.** It has no
RNG and no presets, and that is deliberate: its determinism is what makes the strongest tests
possible — same seed, identical scores, every time — and a "weakened" Cyborg would still count
every card perfectly, so the spread between a crippled one and a sharp one is narrow enough not
to be worth the machinery.

If difficulty tiers are ever wanted, the mechanism to reach for is a temperature on the decision
margin — bid 1 with probability `1 / (1 + exp(−(EU₁ − EU₀) / τ))` rather than whenever
`EU₁ > EU₀`, and the same softening on §4.1's rounding and §6.3's alignment. That stays confident
where the call is obvious and wavers only where it is genuinely close, which is what a weaker
human looks like. Randomising a raw per-card fraction instead does the opposite: it is near a
coin-flip on the worst card in the deck, where the answer is obvious.

---

## 10. Implementation checklist

Suggested layout, following the shape the reckoner already established:

```
include/romanian_whist/strategies/CyborgStrategy.h        IStrategy + IGameObserver
include/romanian_whist/strategies/cyborg/CyborgKnobs.h    §9
include/romanian_whist/strategies/cyborg/Halves.h         §1 — isBig / isSmall / categorise
include/romanian_whist/strategies/cyborg/Odds.h           §3
include/romanian_whist/strategies/cyborg/Bidding.h        §4
include/romanian_whist/strategies/cyborg/PlayPlan.h       §5, §6
src/strategies/cyborg/*.cpp
tests/CyborgOddsTests.cpp  CyborgBiddingTests.cpp  CyborgStrategyTests.cpp  CyborgTournamentTests.cpp
```

Add the sources to the `romanian_whist_engine` target in `CMakeLists.txt` and the tests to
`tests/CMakeLists.txt`. Provide a `makeCyborgSeat(name, engine, knobs)` free function that builds
the strategy, stores its name and registers it as an observer before returning the `SeatSetup`,
the way `makeReckonerSeat()` does (`src/strategies/ReckonerStrategy.cpp:304-312`) — a Cyborg that
was never registered as an observer degrades to a bot that believes it is seat 0 in a four-player
game, which is a bug that will not announce itself. See §2.4 for the four guards against it.

Build order, each step testable on its own:

1. **§1 halves** against the table in §1, for all five player counts. Trivial and it catches an
   off-by-one in `minRankInt` that would poison everything downstream.
2. **§2 memory**, verified against a scripted game: after each card, assert `live`, `handSize[]` and
   `voidMask[]`. Test the must-trump inference explicitly.
3. **§3 odds**, against hand-computed hypergeometrics — including the degenerate `|unseen| == H`
   case, where `pNoneOut` must be exactly 0 or 1.
4. **§4 bidding**, one test per round size, using the worked examples in §7 as fixtures.
5. **§5/§6 play**, asserting *decisions* rather than scores: given this memory and this hand, this
   card. Reuse the fixture style of `tests/TrickHeuristicsTests.cpp`.
6. **Tournaments** against `LowRisk`, `Ducking` and `Reckoner`, at every player count and both
   [round structures](RULES.md#3-round-structure). `tests/ReckonerTournamentTests.cpp` is the
   template; a fixed seed makes the result a regression test rather than a coin flip.

The bar to clear: Cyborg should beat `LowRiskStrategy` comfortably and lose to `ReckonerStrategy`
narrowly. If it beats Reckoner, one of them has a bug — most likely Cyborg reading state it should
not have (`context.hand` in a Forehead round, §4.7) or Reckoner's rollouts being mis-seeded.

---

## 11. Deferred work

Carried deliberately, so it does not get rediscovered as a surprise.

### Retire the two reckoner compatibility shims

Extracting `common::RoundMemory`, `common::CardMask` and `common::hyper0` left two headers behind
whose entire job is to keep the old spellings alive:

- `include/romanian_whist/strategies/reckoner/CardMask.h` — using-declarations for `Mask`,
  `cardToId` and the rest.
- the `using common::hyper0;` line in `include/romanian_whist/strategies/reckoner/Evaluator.h`.

They exist because without them, moving three files would have meant editing every `.cpp` under
`strategies/reckoner/` plus `tests/ReckonerTrackerTests.cpp` — turning a mechanical move into a
diff nobody could review against a tournament test that only asserts one average beats another.
With them, the move touched eight files.

That it changed no behaviour was established afterwards, and it is worth recording how, because the
pinned scores in `tests/ReckonerStrategyTests.cpp` could not do it on their own: they were added by
the extraction commit itself, so they only ever pinned post-refactor output. The check that counts
was run separately — the pinned `TEST_CASE` was ported onto a `master` worktree predating the move
and built there, and all six vectors came back identical on Linux/GCC/Release. The portable state
pin in `tests/CyborgMemoryTests.cpp` covers the same ground going forward, on every platform.

The cost is that `reckoner::Mask` and `common::Mask` are both live spellings for the same entity,
which is one more thing a newcomer has to be told.

**To retire them:** rewrite every `reckoner::`-qualified and unqualified use of the moved names
under `src/strategies/reckoner/` and `tests/` to say `common::`, delete
`strategies/reckoner/CardMask.h`, drop the `using` line from `Evaluator.h`, and confirm the pinned
scores are unchanged. Mechanical, and safe in exactly the way the first move was — but it is churn
across the reckoner subtree, so it is worth doing **after** Cyborg is finished rather than in the
middle, when the same files are being read for other reasons.
