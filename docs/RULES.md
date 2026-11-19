# The rules this engine implements

The authority on *how the game is played*. Where the code is, see
[README.md](../README.md); how it came to be shaped that way, see
[ENGINE_V4_PLAN.md](../ENGINE_V4_PLAN.md).

Each rule names where it lives, so a question about behaviour has one place to start and one
place to end.

---

## 1. Player count

2–6 players.

Validated by `GameEngine::start()`, which throws `std::invalid_argument` outside that range.

## 2. Deck size

Two ranks per player, across all four suits — so the deck holds exactly eight cards per player,
which is what makes the 8-card round deal out completely and so have no trump (rule 4).

| Players | Ranks |
|---|---|
| 2 | Ace, King, Queen, Jack |
| 3 | Ace through 9 |
| 4 | Ace through 7 |
| 5 | Ace through 5 |
| 6 | Ace through 3 |

`Deck::initialize` (`src/Deck.cpp`).

## 3. Round structure

In both structures the 1-card and 8-card rounds repeat once per player.

- **`S_181`** (1-8-1): repeated 1-card rounds, 2–7, repeated 8-card rounds, 7–2, repeated 1-card
  rounds.
- **`S_818`** (8-1-8): repeated 8-card rounds, 7–2, repeated 1-card rounds, 2–7, repeated 8-card
  rounds.

`Scoreboard::initialize` (`src/Scoreboard.cpp`) lays the whole schedule out at `start()`, which is
why `GameEngine::getRound(index)` can name a round that has not been played yet.

## 4. Trump

For rounds below 8 cards, the next card off the deck is turned as trump. **8-card rounds have no
trump** — the whole deck is dealt, so there is no card left to turn.

`GameEngine::getCurrentTrumpCard()` returns `std::optional<Card>`; empty *is* the 8-card round.

## 5. Bidding restriction

The final bidder may not make the round's bids add up to exactly the trick count — somebody has
to be wrong.

It binds only the final bidder, and only while a bid could still hit the total: once the bids so
far exceed the trick count, no single bid can bring it back, and the restriction lifts.

`GameEngine::getForbiddenBet()` names the barred bid (empty when nothing is barred);
`isBetLegal()` checks a bid against it and against the range `[0, trick count]`. The engine
validates every bid it is handed and throws on an illegal one.

## 6. Play restrictions

In priority order:

1. Must follow the lead suit if possible.
2. Must play trump if holding no card of the lead suit.
3. May play anything if holding neither.

**Overtrumping is not required** — a player who must trump may play any trump, not necessarily one
that beats the trump already down.

`CardValidator::getLegalCards`. The engine validates every card it is handed, so a move provider
that ignores this is rejected rather than obeyed.

## 7. Trick ranking

The highest trump wins. With no trump played, the highest card of the lead suit wins. An off-suit
discard never wins.

`CardValidator::beats`, and `CardValidator::getWinningCard` for a partly played trick — which is
also what `GameEngine::getCurrentTrickLeader()` answers, so a "currently winning" highlight can
never disagree with the winner the engine eventually declares.

## 8. Scoring

| Outcome | Score |
|---|---|
| Bid exactly | `5 + bid` |
| Missed the bid | `-abs(bid - actual)` |
| 5th consecutive exact bid | `+10` |
| 5th consecutive miss | `-10` |

A streak starts over once it pays out, so the 10th, 15th, … consecutive exact bid earns another
`+10`, and the 10th, 15th, … consecutive miss another `-10`.

**One-card rounds are excluded from the streak counters**, in both directions: they neither
advance a streak nor break one.

`Scoreboard::calculateScores` (`src/Scoreboard.cpp`). A round's score and the committed total are
separate — see `GameEngine::getRoundScore()` / `getTotalScore()`, and the note in
[README.md](../README.md#scores) about which is readable when.

### Winning

Highest total after the last round wins. **There is no tie-breaker** — seats level on points share
the place, and a game can end in a two-, three- or four-way draw. Romanian Whist has no
"most exact bids" or "best final round" rule to separate them, and the engine does not invent one.

`GameEngine::getStandings()` ranks every seat with `Standing::place`, tied seats sharing a place and
the places a tie consumes being skipped (50, 40, 40, 30 ranks 1, 2, 2, 4). `getWinners()` returns
every seat on the top score, which is more than one row for a drawn game — a client that reads
`getStandings().front()` instead names a single winner of a game nobody won alone.

## 9. Special rounds

Two one-card round types change what a bidder may see:

- **`Forehead`** — the bidder cannot see their own card, but everyone else's is visible to them.
- **`Hidden`** — nobody sees any card before bidding.

Two flags on `GameSetup` place them:

- `all1GamesAreForehead` marks **every** one-card round in the schedule as Forehead.
- `endWithForeheadAndHidden` appends one Forehead and one Hidden round at the end of the game.

`RoundType` (`include/romanian_whist/RoundType.h`) records the type;
`BetContext::roundType` tells a bidder which round they are in;
`GameEngine::canSeeHand(viewer, holder)` states the visibility rule.

**`canSeeHand()` is a rule, not a guard.** The engine will hand any caller any seat's cards —
`Player::getHand()` is public because a renderer drawing a Forehead round needs it. Enforcement is
the client's, and for a networked client it is load-bearing; see
[README.md](../README.md#canseehand-is-a-rule-not-a-guard).

Note the two round types are currently identical *to an AI*: `BetContext` carries no field for
other seats' cards, so a strategy in a Forehead round has nothing extra to reason from. They
differ in what a human is shown, not in what a bot can compute.
