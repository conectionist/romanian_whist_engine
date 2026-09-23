# Changelog

This project follows [Semantic Versioning](https://semver.org/). The version lives in
`CMakeLists.txt` and reaches consumers as `romanian_whist::VersionString`.

## Unreleased

**The engine builds with warnings on, and CI treats them as errors.** Nothing here changes what the
engine does; it changes what its own build refuses to let through.

### Added

- `WHIST_ENABLE_WARNINGS` (default `PROJECT_IS_TOP_LEVEL`) builds with `-Wall -Wextra -Wshadow
  -Wpedantic -Wnon-virtual-dtor -Woverloaded-virtual`, or `/W4` on MSVC.
- `WHIST_WARNINGS_AS_ERRORS` (default `OFF`) adds `-Werror` / `/WX`. CI configures with it on.

  Both default to leaving a consumer's build alone. Compile options are appended to the ones your
  build already sets, so warnings that are always on would put our sources under your `-Werror` —
  and a future compiler release adding a warning would then break your build on our code. Turn them
  on deliberately if you want them.

### Fixed

- **`ReckonerStrategy` did 200 rollouts per one-trick bid and discarded every result.** The bid came
  from a separate calculation below it. No bid changes; the work was simply thrown away.
- **The one-trick bid ignored its own importance weights.** Its samples are drawn from a constrained
  proposal and weighted by how well each deal explains the bids already made, but that estimate
  counted them one apiece — measuring the proposal's win rate rather than the posterior's. It now
  averages by weight, as every other bid already did. The two estimates differ by 0.007 on average,
  so the bid moves only near the decision threshold: 4 bids in 608 over 100 measured games.
- `BetContext` and `PlayContext` give their `std::optional` members default initializers, so
  `BetContext{hand}` and `PlayContext{hand, played}` no longer warn under
  `-Wmissing-field-initializers` in a consumer's build.
- Four `size_t` to `unsigned int` narrowings in `GameEngine` and `Scoreboard` are now explicit, and
  a handful of shadowed locals and unused parameters are gone.

## 4.3.0

**A sixth strategy: `CyborgStrategy`.** A rule-based opponent that remembers the whole round and
prices every card in its hand exactly, then bids for the best expected score and plays to hit that
bid. It sits between the two strategies it was built to be measured against. At four players it
scores 14.4 points a game more than `LowRiskStrategy` and 14.7 fewer than the Reckoner's `medium()`
preset, and it is ahead of LowRisk at every table size. A decision takes about half a microsecond,
and it is deterministic, with no presets and no seed. The design is in
[docs/romanian-whist-cyborg-ai.md](docs/romanian-whist-cyborg-ai.md), and
[docs/STRATEGIES.md](docs/STRATEGIES.md) has a section on it.

Like the Reckoner, it is an `IStrategy` **and** an `IGameObserver`, so it must be registered before
`start()`. `makeCyborgSeat()` does that for you. It has the guards 4.2.0 gave the Reckoner, plus
one more: `start()` throws if its name matches no seat at the table.

Additive: no signature changed, and no existing strategy's rules changed. One build change can
move floating-point results on some platforms; see **Changed**.

### Added

- `CyborgStrategy` and `makeCyborgSeat()`, in `strategies/CyborgStrategy.h`, with its parts under
  `strategies/cyborg/`: the odds kit, bidding, the plan, card play, and `CyborgKnobs`. The knobs are
  development tuning, not a difficulty dial.
- `strategies/common/`: `RoundMemory`, `CardMask` and `hyper0`, the card counting both remembering
  strategies share. They were extracted from the Reckoner, whose old `reckoner::` spellings still
  work. `ReckonerTracker` now derives from `common::RoundMemory`, and every member it had is still
  reachable through it.
- `heuristics::safeCards()` and `heuristics::winningCards()` in `TrickHeuristics.h`: the split of a
  legal list on the card currently winning the trick.
- Tests: the Cyborg suites (`tests/Cyborg*Tests.cpp`), including a tournament bar that asserts
  standings rather than exact totals, and a parity test that pins Cyborg's fallback to
  `LowRiskStrategy`'s play card for card.

### Changed

- **The engine builds with `-ffp-contract=off` on GCC and Clang.** On macOS/arm64 the compiler
  fused multiply-adds, which changed the last bit of some probabilities, flipped Cyborg's
  tie-breaks and gave different games from Linux. Contraction is now off for the engine target
  (`PRIVATE`, so your own code's flags are untouched), making results match across platforms.
  If you relied on the old contraction — for instance, pinned a Reckoner score on arm64 — expect
  last-bit floating-point differences.

## 4.2.0

**A fifth strategy that counts cards, and the guard rails it needed.** `ReckonerStrategy` landed
without a version of its own — it is the Monte-Carlo opponent described in
[docs/romanian-whist-reckoner-ai.md](docs/romanian-whist-reckoner-ai.md), and this release is where
it appears in the version, the README and [docs/STRATEGIES.md](docs/STRATEGIES.md), which all still
said the engine shipped four.

It is unlike the other four in one way that matters to every client: it is an `IStrategy` **and** an
`IGameObserver`, and it is inert as the first until it has been registered as the second. That
dependency was undocumented and unchecked, which is the rest of this release.

Additive: no signature changed, and `makeReckonerSeat`, `ReckonerKnobs` and `ReckonerPreset` — all a
client needs — behave as before.

### Added

- `ReckonerStrategy`, `makeReckonerSeat()`, `ReckonerKnobs` and `ReckonerPreset`, documented for the
  first time here. Four presets, `easy()` through `brutal()`, differing only in sampling depth,
  memory and a handful of weights.
- `ReckonerTracker::MaxPlayers` — the width of the per-seat arrays, and the engine's own 2..6 ceiling
  on a table.
- `tests/ReckonerRobustnessTests.cpp` — what happens when the tracker is absent, stale, or describes
  a different table from the one being played.

### Fixed

- **`ReckonerStrategy::getBestChoice()` could return an illegal card.**
  `RolloutEngine::choosePlay()` is handed no `PlayContext`: it reconstructs the lead suit, the trump
  and the deck profile from the tracker, so its answer is legal with respect to a *replica* of the
  position. Any drift from the real one became an illegal move that nothing below caught. The answer
  is now filtered through the same `CardValidator` every other strategy uses — as
  `PlayContext`'s own documentation asks — and falls back to a legal card.
- **Encoding a hand could throw rather than merely disagree.** The deck profiles are not nested in
  both directions (two-handed is Jack..Ace, six-handed Three..Ace), so a tracker believing the table
  is smaller than it is met a card it had no id for and threw `std::invalid_argument` out of
  `cardsToMask()` before any answer existed. Both decisions now contain that.
- **An unregistered strategy played on regardless.** Built but never passed to `addObserver()`, it
  kept the tracker's initialisers — a four-handed table, no trump, an empty deck — and played from
  them; seen from a two-handed game as "played a card that is not legal in this trick", a failure
  several layers from its cause. Both decisions now throw `std::logic_error` naming
  `makeReckonerSeat()` if they are reached before the strategy saw a round start. The check keys on
  `initRound()`, the single funnel for the observer callback and the standalone hook alike, so a
  tracker driven either way still answers.
- **`ReckonerTracker::initRound()` wrote per-seat arrays through an unclamped count** and reset only
  the seats in play, leaving a stale tail behind for anything that read past `playerCount`. Seats are
  clamped to the array width and every slot is reset. This also clears a `-Wstringop-overflow`
  warning at `-O2`.

### Known

- `RolloutEngine::rollout()` passes `BasePolicy::chooseCard()`'s `INVALID_CARD_ID` straight to
  `cardBit()` and `maskSuit()` (`RolloutEngine.cpp:256`, `:278`), which UBSan reports as a 255-bit
  shift and a read of `suitMasks[31]` when the rollout is driven from a sufficiently inconsistent
  tracker. Not reachable from ordinary play — the `[reckoner]` games report zero UBSan errors — and
  not fixed here, because the right recovery inside a sampled rollout is a judgement that would bias
  the estimator with nothing to catch it.

## 4.1.0

**A drawn game has more than one winner.** The engine ranked the standings but never said who won,
so every client decided that for itself — and the obvious way to decide it, reading the first row,
quietly names one winner of a game two seats drew. Romanian Whist has no tie-breaker, so the answer
is a list.

Additive: no existing call changes behaviour, and `Standing` gains a field rather than losing one.

### Added

- `Standing::place` — a competition ranking, from 1. Seats level on points share a place and the
  places a tie consumes are skipped, so 50, 40, 40, 30 ranks 1, 2, 2, 4. Ranking in the engine is
  what stops one client deciding a tie differently from the next.
- `GameEngine::getWinners()` — every seat on the top score, in seat order. More than one row for a
  drawn game, never empty for a started game. Like `getStandings()` it answers during a game too,
  where it means "currently leading"; a client announcing a *winner* checks `getStatus()` first.

### Changed

- `getStandings()` now fills `place` on every row. Existing readers of `seat`, `name` and `score`
  are unaffected.
- [docs/RULES.md](docs/RULES.md) §8 gains a **Winning** section: highest total wins, ties are
  shared, and there is no tie-breaker.

## 4.0.0

**The engine owns the game loop.** In 3.x a client wrote the loop itself out of engine
primitives — deal, ask each player to bid, resolve each trick, score, advance — which meant every
client hand-wrote the same game rules, no client's loop could be tested, and rules that needed the
loop (the Forehead and Hidden rounds) had nowhere to live. The loop is now
`GameEngine::run()`/`playRound()`, and a client decides moves (`IMoveProvider`) and renders events
(`IGameObserver`).

This is a breaking release throughout. See [README.md](README.md) for the shape of a v4 client.

### Migration

| 3.0.0 | 4.0.0 |
|---|---|
| `addPlayer()` + `initializeScoreboard()` + `initializeDeck()` | `start(GameSetup)` — one validated call |
| a hand-written `while(isInProgress())` loop | `run()` / `playRound()`, plus `IGameObserver` to render |
| `setStatus()`, `shuffleDeck()`, `dealCards()`, `placeBet()`, `calculateScores()`, `commitRoundScores()`, `completeCurrentRound()`, `determineTrickWinner()` | private — `playRound()` is their only caller |
| `PlayerList::iterator` in the public API | `Seat` |
| `getFirstPlayerOfTheRound()` / `setFirstPlayerOfTheRound()` / `getNextPlayer()` | `getRoundLeaderSeat()` / `getTrickLeaderSeat()` / `getNextSeat()` |
| `IMoveProvider::playCard()` → `Card*` | → `std::optional<std::size_t>`, an index into `context.hand` |
| `IStrategy::getBestChoice()` → `Card*` | → `std::optional<Card>` |
| `Card*` in `BetContext`, `PlayContext`, `Trick`, `CardValidator` | cards by value; `nullptr` becomes an empty `std::optional` |
| `getCurrentTrumpCard()` → `Card*` | → `std::optional<Card>` |
| `getPlayerScores()` / `getPlayerRoundScores()` | `getStandings()` / `getRoundScore(Seat)` / `getTotalScore(Seat)` |
| `Round::hasBet(name)` / `Round::getBet(name)` | `GameEngine::getBet(Seat)` → `std::optional<unsigned int>` |
| `Round::getActual(name)` / `GameEngine::setResult()` | `getTricksWon(Seat)` — derived from the round's stored tricks; there is no setter |
| `Round::getOpeningPlayer()` / `Round::getFirstPlayer()` | `getRoundLeaderSeat()` / `getTrickLeaderSeat()` |
| reaching a `Player` to call `playCard`/`getBet` | both are private, with `friend class GameEngine` |

### Added

- `IGameObserver` — thirteen no-op callbacks covering the whole game, called synchronously on the
  thread running it. `addObserver()` / `removeObserver()`.
- `GameEngine::run()` and `playRound()`, and `requestStop()` to end a game cleanly at the next
  round, bid or trick boundary. A stop never scores the round it lands in, and yields
  `onGameStopped()` rather than `onGameOver()`.
- `GameSetup` / `SeatSetup` and `start()`, which validates the whole setup — 2–6 seats, non-empty
  names, no duplicates — before applying any of it, and leaves the engine untouched if it refuses.
- `Seat`, and per-card seats in `Trick` via `PlayedCard`, so no client has to reconstruct turn
  order arithmetically.
- `canSeeHand(viewer, holder)` — the Forehead/Hidden visibility rule, in one place. It is a rule,
  not a guard; see the README before building a networked client on it.
- `getPhase()`, `isSetUp()`, `getActiveSeat()`, `getCurrentTrick()`, `getCurrentTrickLeader()`,
  `getCurrentTrickNumber()`, `getBiddingOrder(Seat)`, `getRound(index)`, `getStandings()`.
- `GameSetup::shuffleSeed` and `RandomCardStrategy(std::uint32_t)` for reproducible games, on a
  hand-rolled Fisher-Yates shuffle that gives the same sequence on every standard library.
- A Catch2 test suite, gated on `WHIST_BUILD_TESTS` (default: `PROJECT_IS_TOP_LEVEL`), run on
  Linux, Windows and macOS. The engine had no automated tests before this release.
- [docs/RULES.md](docs/RULES.md) — the rules the engine implements, alongside the code for each.

### Changed — behaviour

- **Forehead and Hidden rounds are played.** They were stored in the schedule and ignored. A
  bidder is now told which round they are in through `BetContext::roundType`, and
  `LowRiskStrategy` bids 0 in them rather than reading a hand it is not supposed to see. **This
  changes scores: a game played on 3.0.0 does not reproduce on 4.0.0.** It is the only intentional
  behaviour change in this release.
- Every accessor whose answer depends on a current round now throws `std::logic_error` before
  `start()`, instead of indexing an empty vector. `isSetUp()` is how to ask.
- The engine validates the moves it is given — an out-of-range bid, a barred bid, an index that is
  not a card in the hand — and throws rather than obeying. In 3.x the bundled strategies happened
  to be well-behaved and nothing checked.

### Fixed

- **A completed round no longer misreports itself.** Every finished `Round` held its trump and its
  tricks as `Card*` into a deck that is re-shuffled at the top of each round, so from round two
  onward every stored round reported cards that were never played in it — and kept changing for
  the rest of the game. Nothing read round history, so nobody had seen it; the first client to
  render "the previous round" would have. Cards are held by value now.

### Removed

- `Bet::actual`, `Round::setResult()`, `Round::getActual()`, `Round::hasBet()`. Tricks won are
  derived from the tricks the round already stores.
- `GameEngine::getPlayer(Seat)`, and the rest of the driving API listed in the migration table.

## 3.0.0 and earlier

Predate this changelog. See the git history.
