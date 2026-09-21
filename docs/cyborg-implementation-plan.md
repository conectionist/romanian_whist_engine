# Cyborg — implementation plan for the remaining phases

This file is meant to be enough for a fresh session with no chat history to pick up the Cyborg work
where it stopped. It covers what already exists, the conventions this work follows, the lessons that
cost time the first time round, and Phases 2–4 in detail.

The **design** lives in [romanian-whist-cyborg-ai.md](romanian-whist-cyborg-ai.md) and is the
authority on *what* Cyborg does. This file is only about *how to land it*. Where they disagree, fix
the design document in the same PR — Phases 0 and 1 each found real errors in it.

---

## 1. Where things stand

| Phase | What | Status |
|---|---|---|
| 0 | Extract `common::RoundMemory`, `common::CardMask`, `common::hyper0` out of Reckoner | ✅ merged, PR #19 |
| 1 | `CyborgStrategy` seat with memory + guards, playing at LowRisk strength; the odds kit | ✅ merged, PR #20 (`9b12614`) |
| 2 | Bidding (design §4) | ✅ merged, PR #21 (`e9f9eeb`) — §4.4 and §4.5 revised, A/B pinned per table size, see §4 "Gate" |
| 3 | The plan and card play (§5, §6.1–§6.4) | ✅ merged — 3a PR #22 (`f634301`), 3b PR #23 (`c488cb3`, measurement only), 3c PR #24 (`ff19b42`, §6 play the default) |
| — | Bidding re-calibration: §4.4 bids by expected score | in review — see the end of §5 |
| 4 | Tournament bar, refinements, documentation, release | not started |

Baseline: **148 test cases** after Phase 1; **185 on GCC Release and Debug, 184 on clang** after the
bidding re-calibration (one Reckoner test is compiled on Linux/GCC only — see §3). CI green on Linux,
macOS and Windows.

### What Phase 1 built — the pieces later phases plug into

| File | Contents |
|---|---|
| `include/romanian_whist/strategies/CyborgStrategy.h`, `src/strategies/CyborgStrategy.cpp` | `CyborgStrategy : IStrategy, IGameObserver` and `makeCyborgSeat(name, engine, knobs = {})`. Namespace `romanian_whist`. |
| `include/romanian_whist/strategies/cyborg/CyborgKnobs.h` | `duckPropensity`, `useHeuristicBid`, `useHeuristicPlay`, and `static constexpr parityWithLowRisk()`. |
| `include/romanian_whist/strategies/cyborg/Halves.h` | Header-only big/small split: `isBigId`, `rankIndexOf`, `bigHalfMask`, `categoriseId` → `{BigTrump, SmallTrump, BigNonTrump, SmallNonTrump}`, plus `Card` conveniences. |
| `include/romanian_whist/strategies/cyborg/Odds.h`, `src/strategies/cyborg/Odds.cpp` | `OddsContext`, `makeOddsContext`, `beaters`, `reachableBeaters`, `isSureWinner`, `isSureLoserOnLead`, `pLeadWins`, `pHolds`. Pure functions. |
| `include/romanian_whist/strategies/common/RoundMemory.h` | Shared round memory (Phase 0): `live`, `handSize[]`, `voidMask[]`, `bids[]`, `won[]`, `opener`, `currentTrickCards`, `roundInitialised`, and `bidsPlaced()`, `previousBidSum()`, `handsOutstanding()`. |

**How `CyborgStrategy` decides today.** Both decisions follow Reckoner's 4.2.0 shape:

1. `requireMemory()` throws `std::logic_error` if `memory.roundInitialised` is false (never registered as an observer).
2. Blind rounds / empty hand handled up front.
3. **The legal list is computed from the `PlayContext`, before memory is consulted.** The context is the authority; memory is a replica.
4. The memory-dependent part runs inside `try { … } catch(const std::logic_error&) { }` — deliberately not catch-all. `encodeHand()` (the single `cardsToMask` call site) can throw for a deck the memory can't encode, and `agreesWith()` returns false if memory disagrees with the position.
5. On any failure, the decision **falls back to the heuristic** (`heuristicBid` / `heuristicPlay`, a reproduction of `LowRiskStrategy`), and `fallbacksTaken` is incremented.

**The seams Phases 2 and 3 fill in.** `getBestBet` currently returns `heuristicBid(context)` after
encoding the hand for the check. `getBestChoice` currently calls `heuristicPlay` when `agreesWith`
passes. The knobs `useHeuristicBid` / `useHeuristicPlay` choose who gets *first refusal*; **the
heuristic stays as the fallback in every configuration**, so it is not dead code after Phase 3.

**The parity test is permanent.** `tests/CyborgStrategyTests.cpp` requires Cyborg configured with
`CyborgKnobs::parityWithLowRisk()` to play *exactly* LowRisk's game — final scores, per-round
`(bid, tricks)`, and every card played — at 2–6 players, both structures, both Forehead/Hidden flags,
with `fallbacksTaken == 0`. `parityWithLowRisk()` sets every field **explicitly**, so flipping a
default in a later phase must not break it. Do not delete it: after Phase 3 it pins the fallback path
and that wiring changes nothing.

---

## 2. How this work is run

These were agreed with the user and hold for every phase.

- **Phase by phase.** One branch and one PR per phase (`feature/cyborg-phase-N`). Plan the phase, get
  approval, implement, then stop for review before the next.
- **Never commit until the user has checked the change** and said so. "It builds" is not enough.
- **Squash-merge** with `gh pr merge <n> --squash` and a *written* message describing the net change —
  not the concatenated commit list. Pin with `--match-head-commit`.
- **Check master for drift before opening a PR.** PR #19 conflicted because 4.2.0 landed mid-flight.
  `git fetch origin && git log --oneline HEAD..origin/master`.
- **Watch CI to the end on all three platforms** before calling a PR done. Windows and macOS have each
  caught things Linux did not.
- **Review rounds happen.** The user sometimes pushes fixes to the PR branch after a review. Read
  every new commit and verify it independently — including building it — rather than trusting it or
  reverting it. Uncommitted edits in the working tree that you did not make are the user's: do not
  commit or push them unless asked.
- **Single strength, deterministic.** No difficulty presets, no RNG, no seed argument. `CyborgKnobs`
  is development tuning, not a difficulty dial.
- **No version bump or CHANGELOG entry until Phase 4.**
- **Knobs arrive with the code that reads them.** Do not pre-declare §9 knobs a phase doesn't use.

---

## 3. Lessons and traps — read before writing code

Each of these cost at least one CI round or a review round.

**Test names must be ASCII.** CTest passes each `TEST_CASE` name back to the executable as a filter,
and on Windows that goes through the console code page. A `§` in three names arrived mangled, Catch2
matched nothing, and the tests *silently did not run*. Write "section 4.2" or "worked example 7.3".
`SECTION` names are not passed as filters and are safe, but keep them ASCII anyway. Check before
pushing:

```bash
grep -nP 'TEST_CASE\("[^"]*[^\x00-\x7F][^"]*"' tests/*.cpp
```

**Aggregate field orders.** Getting these backwards compiles and silently tests the wrong branch.

```cpp
PlayContext{ hand, playedCards, trump, leadSuit, bet, tricksWon }   // bet BEFORE tricksWon
BetContext { hand, trump, isFirstPlayer, forbiddenBet, roundType }
```

Both hold `const std::vector<Card>&` members, so the vectors must be named lvalues in tests.

**`hyper0(k, n, h)`** — marked count first, then population, then draw. It reads backwards.

**No sanitizers, Release-only CI.** An out-of-bounds read surfaces as a plausible wrong number, not a
crash. `common::maskAbove`/`maskBelow` index a 48-entry table unchecked; `cyborg::beaters()` holds the
one `CardId` bounds check, so anything reaching those masks must go through it first.

**Floats.** Reckoner's exact-score pin (`tests/ReckonerStrategyTests.cpp`) is compiled only under
`WHIST_PIN_FLOAT_GOLDENS` (Linux/GCC), because `std::exp`/`std::pow` differ across libm and change
tie-breaks. Cyborg uses only basic arithmetic, which IEEE-754 rounds the same way everywhere —
**provided the compiler does not fuse `a + b * c` into one fused multiply-add**. ARM toolchains do by
default, and Phase 3b's tournament pins moved on macOS/arm64 while Linux and Windows matched them,
because §6 play breaks ties at the last bit. So the engine builds with `-ffp-contract=off` (GCC and
Clang, private to the library), and with that Cyborg's fixtures and pins are portable — **do not gate
them**. If one ever diverges on one platform, suspect contraction first: on an FMA-capable x86, a
clang build with `-DCMAKE_CXX_FLAGS="-mfma -ffp-contract=on"` minus the engine's own flag reproduced
the macOS numbers exactly. Use exact `==` where the design claims certainty (`0.0f`, `1.0f`) and
`WithinAbs(…, 1e-5)` for estimates. **A table containing Reckoner is different**: gate any exact score
involving a Reckoner seat the same way as Reckoner's own pin.

**Integer-ness of a float sum.** §4.1 branches on "is `raw` an integer". Knob values like `0.60` are not
binary-exact. Use `std::fabs(raw - std::round(raw)) < 1e-4f`, never `raw == std::floor(raw)`.

**Verify a regression test fails without the fix.** Twice a test was checked by building a scratch copy
with the fix reverted. A test that passes either way proves nothing. Do this for every behavioural
fix, in a copy outside the working tree (e.g. `git ls-files -z | xargs -0 -I{} cp --parents {} <dir>`),
so it cannot interfere with the real build.

**Verify the tree you actually push.** If the working tree holds changes you are *not* committing,
your local test runs don't verify the commit. Export the index (`git checkout-index -a
--prefix=<dir>/`) and build that.

**Wait-loop gotcha.** `set -o pipefail` plus `echo … | grep -q` gives false results (`grep -q` exits
early, `echo` gets SIGPIPE). Don't combine them in CI-polling loops.

**Test fixtures.**
- Include `"CardStringMaker.h"` in any test file comparing `Card`s.
- `buildRoundRobinProviders(n)` seats `FirstCardStrategy` at 0, **`LowRiskStrategy` at 1**, `DuckingStrategy` at 2.
- `buildSetup` names seats `"P0".."Pn"`; a Cyborg swapped into seat *k* must be named `"P<k>"`.
- Never `REQUIRE` inside an `IGameObserver` callback — the throw unwinds through `GameEngine::run()`. Collect, then assert after the game.
- An observer that throws from `onGameStarted` leaves the engine unusable: assert on `start()`, never call `run()` afterwards.

**Build and verify, every phase:**

```bash
cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
cmake --preset default && cmake --build --preset default && ctest --preset default --output-on-failure
# clang, from a build directory OUTSIDE the repo:
cmake -S . -B /tmp/whist-clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
      -DFETCHCONTENT_SOURCE_DIR_CATCH2=$PWD/build-release/_deps/catch2-src
cmake --build /tmp/whist-clang -j && ctest --test-dir /tmp/whist-clang
```

Expected: clang runs one case fewer than GCC (the float-pinned Reckoner test).

---

## 4. Phase 2 — Bidding (design §4)

**Goal:** replace `heuristicBid` as the first-refusal bidder with the §4 rules. Play stays heuristic.

### Files

- `include/romanian_whist/strategies/cyborg/Bidding.h`, `src/strategies/cyborg/Bidding.cpp` — namespace
  `romanian_whist::cyborg`, free functions throughout so every rule is testable with no engine.
- `CyborgKnobs.h` gains the knobs §4 reads, with §9's defaults:
  `gapCredit = 0.75f`, `gapCreditFollower = 0.60f`, `useExpectedRemaining = true` (measured, see "Gate"),
  `useLengthCredit = false`.
- Flip `useHeuristicBid`'s default to `false`. `parityWithLowRisk()` already sets it to `true`
  explicitly, so the parity test must keep passing untouched — if it breaks, something is wrong.

### Suggested API

```cpp
enum class Rounding { Exact, Floored, Ceiled };

struct BidInputs {
    std::vector<Card> hand;              // leave EMPTY for Forehead/Hidden (§4.7)
    unsigned int playerCount = 4;
    unsigned int trickCount = 1;
    std::optional<Card> trump;           // from BetContext - authoritative
    RoundType roundType = RoundType::Normal;
    unsigned int biddingPosition = 0;    // 0 = round leader, N-1 = last
    unsigned int previousBidSum = 0;
    std::vector<int> bidsInBiddingOrder; // bids already placed, leader first
    std::optional<unsigned int> forbiddenBet;
    OddsContext odds;                    // for the probability rules
};

struct BidResult {
    unsigned int bid = 0;
    Rounding rounding = Rounding::Exact;
    std::optional<Card> plannedLead;     // §4.3 only
};

unsigned int resolveFraction(float raw, unsigned int previousBidSum, unsigned int trickCount,
                             unsigned int seatsStillToBid, unsigned int playerCount,
                             const CyborgKnobs&, Rounding& out);                         // §4.1
unsigned int bidOneTrickAsLeader(float p);                                           // §4.2 - the seam
unsigned int bidOneTrick(const BidInputs&, const CyborgKnobs&);                       // §4.2
BidResult    bidTwoTricksAsLeader(const BidInputs&);                                  // §4.3
float        rawPointsTwoTricks(const std::vector<Card>&, std::optional<Suit>, unsigned int n); // §4.3
float        rawPointsMidRound(const std::vector<Card>&, const OddsContext&);  // §4.4, as revised in review
float        countTricksNoTrump(const std::vector<Card>&, unsigned int n,
                                float gapCredit, bool useLengthCredit);               // §4.5
unsigned int stepOffForbidden(unsigned int bid, Rounding, std::optional<unsigned int> forbidden,
                              unsigned int trickCount);                               // §4.6
BidResult    chooseBid(const BidInputs&, const CyborgKnobs&);                         // dispatch
```

`bidOneTrickAsLeader` takes `p`, not a hand, so the §4.2 round-leader rule stays a one-function swap
point. The user asked about changing exactly that rule once; `6/13` is provably optimal for own
expected score, so it only changes if the objective changes.

### Building `BidInputs` inside `getBestBet`

Keep the existing shape: `requireMemory`, blind-round early return, then inside the `try`:

- `biddingPosition = memory.bidsPlaced()` — a seat's own 0-based position, no engine call needed.
- `previousBidSum = memory.previousBidSum()`.
- `seatsStillToBid = playerCount - biddingPosition - 1`.
- `bidsInBiddingOrder`: walk seats from `memory.opener` for `biddingPosition` steps, reading `memory.bids[]`.
  The §4.2 middle-seat rules need to know *who* bid 1 — "the opener" versus "someone after the opener".
- `odds = makeOddsContext(memory, encodeHand(hand), context.trump ? context.trump->suit : nullopt, knobs.duckPropensity)`.
  It throws if memory and the context disagree about trump — that is the fallback working.
- Result: `knobs.useHeuristicBid ? heuristicBid(context) : chooseBid(inputs, knobs).bid`.
- Store `BidResult::plannedLead` on the strategy (for Phase 3); clear it in `onRoundStarted`.

### The rules, with the traps

- **§4.1 rounding.** Floor when `previousBidSum + raw > R`, ceil otherwise, clamp to `[0, R]`. With
  `useExpectedRemaining`, add `static_cast<float>(seatsStillToBid) * R / N` to the left side — **float
  division**; `2*5/3` is not exact in integers. Integer test by epsilon (§3).
- **§4.2 one trick.** `p = pLeadWins(card, odds)` with duckPropensity forced to 0 (or rely on the formula:
  with one card each `hbar == 1` and the duck term is already zero — Phase 1 has a test proving it).
  Bid 1 iff `p > 6/13`. Middle seats use the table in §4.2; the last seat bids 0 unless barred.
- **§4.3 two tricks as leader.** A lookup on the two cards' categories — ten rows, each with a bid **and
  an opening lead**. Store the lead. Not the leader: `raw = 1.0·bigTrumps + 0.5·smallTrumps`, non-trumps
  score nothing, then §4.1.
- **§4.4 three to seven.** *Revised in review:* each card scores P(nothing an opponent holds beats
  it) from the odds kit — `hyper0` over the higher unseen cards of its suit, times, for a plain card,
  P(no opponent is void in that suit). Then §4.1. The half-point rule it replaced ignored the player
  count.
- **§4.5 eight tricks.** No trump. Per suit, walk down: `g == 0` → 1.0; `g <= winnersSoFar` →
  `gapCredit` (use `gapCreditFollower` when not the round leader); else `g == 1` → 0.5 (*revised in
  review*: was "else big → 0.5", and "big" grows with the table). `LENGTH_CREDIT` stays off unless
  measured.
- **§4.6 barred bid.** Step against the rounding: floored → `+1`, ceiled → `-1`, exact → `-1` (or `1`
  from `0`). Clamp. Do not confuse with `heuristicBid`'s step-down or with `safestBid()`.
- **§4.7 blind rounds.** Already handled before `BidInputs` is built. Keep `hand` empty for them anyway.

### Tests — `tests/CyborgBiddingTests.cpp`, tag `[cyborg]`

Use the design document's worked examples as exact fixtures:

- **§7.1** → round leader holding `10♥`, trump `J♥`, 4 players: `p ≈ 0.720443`, bid 1. Assert the
  `6/13` threshold from both sides.
- **§7.2** → `A♠ 9♠ K♥ 8♥ 7♦`, trump ♠, 4 players: turn-up `8♠`: `rawPointsMidRound ≈ 1.2144` (was `1.5f` under the half-point rule); `previousBidSum = 4`
  → bid 1 `Floored`; `previousBidSum = 1` → bid 2 `Ceiled`.
- **§7.3** → `A♥ K♥ J♥ A♦ Q♦ 9♦ 8♠ 7♣`, no trump: `countTricksNoTrump == 4.50f`; per suit
  `A K J` → 2.75, `A K 10` → 2.75, `A K Q J` → 4.00, `A Q` → 1.75. As round leader: bid **5** with
  `useExpectedRemaining` off, **4** with it on.
- **§4.3**: one `SECTION` per table row, asserting **both** bid and planned lead.
- **§4.6**: all three rounding cases, the barred-0 → 1 case, clamping at `R`.
- **§4.7**: Forehead and Hidden, with and without `forbiddenBet == 0`.
- The user's own six one-card examples (4 players, trump `J♥`, round leader): `10♥` → 1, `A♥` → 1,
  `A♠` → 0 (p ≈ 0.4362, just under 6/13), `7♠` → 0, `10♠` → 0, `10♦` → 0. `10♠` and `10♦` must
  give identical `p` (≈ 0.2387).

### Gate — A/B, not absolute

Add `tests/CyborgTournamentTests.cpp` (tag `[cyborg][tournament]`) with an A/B match: one Cyborg with
§4 bidding and one with `useHeuristicBid = true`, both with heuristic play, plus LowRisk seats, 15
seeded games, swapping seats across games so seat position doesn't decide it. Assert the §4 side
averages higher.

**Do not use "beats LowRisk" as the Phase 2 gate.** §4 bids ambitiously and heuristic play has no plan
to deliver those bids, so Phase 2 may score *worse* in absolute terms. If even the A/B fails, **report
it rather than tuning knobs until it passes** — that is design information the user needs.

**What happened (review of Phase 2).** The A/B failed as first written: §4 bidding lost at 4–6 players
(−7.6, −21.6, −35.9 points a game) and won at 2–3. A breakdown by round size showed §4.4's half-point
rule scoring about 0.31 per card at every table size against a fair share of `1/N`, and §4.5's
"big card → 0.5" branch growing with the table the same way. With the user, §4.4 became the
odds-priced rule, §4.5's branch became `g == 1`, and `useExpectedRemaining` was turned on — each
candidate measured on seed ranges the diagnosis had not used. Result per game against heuristic
bidding: +3.9, +5.7, +0.3, −3.3, −4.5 at 2–6 players. The gate still fails at 5–6, so
`CyborgTournamentTests` **pins** the exact totals per table size instead of asserting a winner. Both of
its arms set `useHeuristicPlay = true`, so card play does not move those totals; Phase 3b added separate
real-play A/Bs (see §5).

---

## 5. Phase 3 — The plan and card play (design §5, §6.1–§6.4)

**Goal:** replace `heuristicPlay` as first refusal with a winners/losers plan played to the bid.

### Files — the split is the safety mechanism

- `strategies/cyborg/Plan.h` / `.cpp`: `buildPlan(Mask hand, unsigned bet, unsigned tricksWon, const
  OddsContext&, const CyborgKnobs&)` → `{winners, losers, need, mode, expected, surplus}`, plus
  `feasibility(Mask hand, unsigned n, const OddsContext&)` for §6.3. **Names cards, never chooses one.**
- `strategies/cyborg/Play.h` / `.cpp`: `choosePlay(const PlaySituation&, const Plan&, const OddsContext&,
  const CyborgKnobs&)`. `PlaySituation` is populated entirely from `PlayContext` and holds the
  already-filtered legal list. **It takes no memory parameter**, so the context-is-authoritative rule
  cannot be broken by accident. It takes no player count either — `PlayContext` has none, so a field
  for it could only come from the memory. Every argmax/argmin iterates the legal list, and *that* is
  the intersection with `Plan::winners`: walking the legal list and testing the mask needs no separate
  legal mask to `&` with, and an earlier draft's one was a no-op.
- `CyborgKnobs` gains `holdThreshold = 0.50f` and `slack = 0.75f`. Flip `useHeuristicPlay` to `false`.
- `endgameCards` (§6.5) is **not** added. Enumeration is up to 5040 distributions per decision at six
  players, against a microsecond budget. Leave it for a later, measured, capped addition.

### Modes (§5)

`need = bet − tricksWon` clamped at 0; `rem` = cards in hand.

| Mode | When | Wants |
|---|---|---|
| `Duck` | `need == 0` | never take another trick; throw biggest liability first |
| `Take` | `need >= rem`, or `expected < need − slack` | win whatever can be won, cheapest winner |
| `Shed` | `surplus > slack` | lose surplus winners early, while others can still beat them |
| `Balance` | otherwise | §6.3 alignment rule |

Rebuild the plan at **every** decision, not once per round.

### Rules to get exactly right

- **DUCK vs TAKE discard asymmetry.** From the same safe list, DUCK throws the **highest**-liability card,
  TAKE the **lowest**. The design calls getting this backwards "the single most expensive bug available".
- **DUCK tie-break when leading (§6.1).** Among near-equal low win chances, lead the **dearest** by
  `heuristics::isMoreDangerous` — any trump above any plain card, rank within that. §7.4: holding
  `Q♥ 7♥` with only `K♥` live, lead the queen. *Revised in review:* this was "highest rank", which is
  the same thing inside one suit and the wrong thing across two — bare rank keeps a small trump and
  throws a bigger plain card, and the retained trump is then forced to ruff.
- **SHED never leads a sure winner.** This relies on `isSureWinner` and `isSureLoserOnLead` excluding
  each other — guaranteed since PR #20, and tested.
- **TAKE with sure winners** cashes from the longest suit, highest first. **TAKE with no aces** leads the
  lowest card of the longest suit.
- **Following with players still to act**: only pay for a trick whose `pHolds(...) >= holdThreshold`.
  Derive seats yet to act and the trick leader from the `PlayContext`
  (`leader = (mySeat − playedCards.size() + N) % N`), never from `memory.currentTrickLeader`.
- **§6.3 BALANCE alignment**: compare `feasibility(need − 1, hand \ {cheapest winner})` against
  `feasibility(need, hand \ {best safe card})`; smaller wins; **a tie ducks**.
- **§6.4**: on trick one of a two-trick round, if Phase 2 stored a `plannedLead` and it is legal, play it.
  The bid and the lead were one decision.
- **Belt and braces**: after `choosePlay`, confirm the card is in the legal list; if not, count a fallback
  and use the heuristic — the existing tail already does this.

### Tests — `tests/CyborgPlayTests.cpp`, tag `[cyborg]`

Decision fixtures in the style of `tests/TrickHeuristicsTests.cpp`:

- §7.4 both rows: `K♥` only live → lead `Q♥`; `K♥` and `10♥` live → lead `7♥`.
- The DUCK/TAKE asymmetry as a named test.
- §7.3's lead order as a *sequence*: after bidding on that hand, the leads are `A♥, K♥, J♥, A♦`. This
  is the design's "bid and play can disagree" risk turned into a test — §4.5 credited `J♥` because
  the ace and king flush the queen.
- SHED never leads a sure winner; TAKE with no aces leads low in the longest suit; the planned lead
  overrides `chooseLead` on trick one.
- `holdThreshold`: TAKE with three seats to act and a cheap winner ducks; with one seat left it takes.
- Mutation-check the asymmetry test and the §7.4 test: invert the rule in a scratch copy, confirm failure.

**When `useHeuristicPlay` flips, update the parity test's header comment** to say it pins the fallback
path. The test itself must keep passing unchanged. `fallbacksTaken == 0` in a healthy game still holds,
and matters more then: with real play rules, a silently disagreeing memory would *hide* inside heuristic
play.

Gate: the play fixtures pass, and a randomised sweep over many seeded games confirms every card Cyborg
plays is legal and `fallbacksTaken == 0`.

**What happened.** Phase 3 was split in two. **3a** (PR #22) landed `Plan` and `Play` behind
`useHeuristicPlay = true`, with fixtures; its review made the cashing hint mean "the suit being cashed"
and the DUCK tie-break trump-aware. **3b** added two real-play A/Bs to `CyborgTournamentTests` and
measured before flipping the default — and §6 play **lost** to heuristic play at 3–6 players (+1.3,
−4.7, −1.3, −5.9, −5.0 points a game at 2–6). By agreement the default stayed heuristic and 3b landed
as measurement only.

The loss is almost all in no-trump rounds. There every card is dealt, so `pLeadWins` is 0 for any card
with a higher card still out, apart from the duck term, and an ablation put most of the loss on the SHED
and BALANCE *leads*, which choose by that term. Heuristic play in no-trump rounds with §6 play elsewhere
measured −0.4, −1.0, +0.8, +1.7, +1.1 (seeds 9000–9039). Two plan-level fixes were tried and failed —
dropping the duck term from the plan, and counting the plan's winners with §4.5's rank-gap credits — so
the fix belongs in the lead rules themselves. **Phase 3c** redesigns §6.1's SHED and BALANCE leads for
no-trump rounds, and the pinned A/Bs will show the result.

Bidding under §6 play (§4 against heuristic bidding): +15.0, +8.1, +1.5, −3.7, −6.1. The bidding
re-calibration decision waits for 3c, since those numbers come from play known to under-take without
trumps.

**3c.** Six candidate no-trump rules were measured in scratch on the pinned seeds, a confirmation range
and a fresh 160-seed sample, against a reference that simply led as the heuristic does. The one shipped
changes two things, both only when there is no trump: the **BALANCE lead runs TAKE's cashing rules**
(nothing can ruff, so a certainty is lost only by losing the lead or being forced to discard it), and
**SHED is entered on certainties** (the duck term, summed over a hand, made ordinary hands look
over-strong). A third change — SHED leading only certain losers — measured as doing nothing once SHED
stopped misfiring, and was dropped. §6 play against heuristic play, pinned seeds: +6.7, +1.6, +3.1,
−1.7, +0.4. The agreed flip rule was "even or better at every table size"; the user flipped the default
anyway, because the five-player loss did not reproduce on two other seed ranges (+1.5, −0.8). Bidding
under the new play (§4 against heuristic bidding): +15.4, +8.6, +3.9, −1.2, −3.5 — the input to the
bidding re-calibration decision.

**Bidding re-calibration (after Phase 3).** With §6 play in place, one Cyborg against LowRisk scored
+8.1, +6.7, +5.0, +2.2, −3.8 points a game at 2–6 players, and the whole loss was in 3–7 trick rounds
at 4–6 players — also in all-Cyborg tables, so Cyborg's own weakness rather than a match-up effect.
Broken down by bid value, it bid 1 on three hands in four where LowRisk bid 0 about half the time, and 0
is the most reliable bid to hit: a bidding problem, not a delivery one. Candidates measured in
scratch: always round down, an **expected-score bid** (§4.2's reasoning generalised: the trick-count
distribution from §4.4's per-card odds, then the bid with the best expected score), and LowRisk's own
count as a reference. The expected-score bid shipped: +15.5, +12.0, +11.0, +8.9, +7.0 against LowRisk
(100 fresh seeds), ahead of the reference at 2–4 players and about 1.5 behind it at 5.

---

## 6. Phase 4 — Tournament bar, refinements, documentation, release

### Tournaments (`tests/CyborgTournamentTests.cpp`, template `tests/ReckonerTournamentTests.cpp`)

- **1 Cyborg vs 3 LowRisk**, 15 seeded games: `cyborgAvg > lowRiskAvg + MARGIN`. `MARGIN` cannot be
  written in advance — run it, look at the real gap, pin a margin below it with headroom for seed
  variation. A bare `> lowRiskAvg` would not catch Cyborg dropping from +30 to +2.
- **1 Cyborg vs 1 Reckoner (`medium()`) vs 2 LowRisk**: `reckonerAvg > cyborgAvg` **and**
  `cyborgAvg > lowRiskAvg`. The design's bar: beat LowRisk comfortably, lose to Reckoner narrowly. If
  Cyborg beats Reckoner, suspect a bug in one of them before celebrating. Reckoner makes these scores
  float-dependent — assert the ordering, don't pin exact values, or gate exact pins on
  `WHIST_PIN_FLOAT_GOLDENS`.
- **Player-count sweep** at 2, 3, 5, 6 with the weaker bar, ~5 games each, plus one `S_818` run. The
  design's full matrix is ~450 games and would dominate the suite.
- **Latency**: bound it tightly enough to detect a regression, noting CI runs Release and developers
  run Debug.
- Do **not** put a Cyborg seat in `tests/GoldenGameTests.cpp`: a tunable strategy there turns every knob
  change into golden-score churn.

### Refinements — measure, then decide

`useExpectedRemaining` (§4.1) was measured in Phase 2's review and is now on by default; §7.3 is the
case it exists for, where the literal rule makes a round leader bid 5 and 4 is right. `useLengthCredit`
(§4.5) stays off unless a tournament shows it wins. The expected-score bid (§4.4) is also worth
measuring for two-trick non-leaders and eight-trick rounds, which still round an average with §4.1.

### Documentation

Reckoner's entries (added in 4.2.0) are the template:

- `README.md` strategy table: a `CyborgStrategy` row next to `ReckonerStrategy`.
- `README.md`: the same "also an observer — register before `start()`" warning Reckoner carries;
  point at `makeCyborgSeat()`.
- `docs/STRATEGIES.md`: a `CyborgStrategy` section pointing at the design document rather than repeating
  it; update the strategy count in its opening line.
- The design document: remove or correct anything the implementation proved wrong.

### Release

Version bump in `CMakeLists.txt` and a `CHANGELOG.md` entry. This is where Cyborg becomes a documented
strategy, the way 4.2.0 did it for Reckoner — likely a minor version bump.

---

## 7. After Cyborg — carried debt

- **Retire the reckoner compatibility shims** (design §11). `strategies/reckoner/CardMask.h` and one
  `using common::hyper0;` line in `reckoner/Evaluator.h` exist only to keep `reckoner::` spellings
  alive after Phase 0. Rewrite uses to `common::`, delete the shim, confirm Reckoner's pinned scores are
  unchanged. Do this *after* Cyborg, not in the middle.
- **`roundInitialised` is never cleared** (design §8). An observer removed mid-game is detected only by
  its consequence (`agreesWith` fallbacks), not its cause. Shared with Reckoner; fixing it changes
  shared state.
- **The build sets no warning flags.** `CMakeLists.txt` has only `target_compile_features`, so
  `-Wswitch` — which is what stops a new `Mode` from being silently unhandled — is off. The cyborg
  sources are clean under `-Wall -Wextra` today; turning them on project-wide is a separate change
  because it will surface warnings in older files, and it belongs with Phase 4's tidy-up rather than
  in the middle of card play.
- **`pHolds` over-counts slightly.** A seat void in both the lead suit and trump adds nothing to the
  marked set but still adds its hand to the draw size. That biases towards pessimism (a lower hold
  probability), which is the safe direction. Refine only if Phase 3 measurements show it mattering.
- **§6.5 endgame enumeration** — deferred indefinitely; see Phase 3.
- **Reckoner's own known limitations** (#16, #17) are recorded in its design document §11 and are not
  part of this work.
