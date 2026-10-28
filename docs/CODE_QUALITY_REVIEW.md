# Code quality review

A review of the engine as of 5.0.0 (commit `bbec273`, October 2026): what is good, what gets in the
way, and what to change first.

**Verdict: 7 out of 10.** On a scale where 1 is AI slop and 10 is a codebase others should copy.
It is a well-built, well-tested library with a careful API. What holds it back is code from
different periods written in different styles, the scoring rule copied into three places, and
C++20 features that are mostly unused. None of that needs a change in behaviour to fix.

## How this was measured

- Read `include/` and `src/` (10.4k lines), `tests/` (9.2k lines), `CMakeLists.txt`, the CI
  workflow, the README and `docs/`.
- Built the `default` preset with GCC 13 and `-DWHIST_WARNINGS_AS_ERRORS=ON`: no warnings.
- Ran the tests: all 188 passed, in 40 seconds.
- Counted lines of code against lines of comment, function lengths, and a few patterns (header
  guards, casts, brace style).

## Strengths

### The API is designed with care

- **A move is a position in the hand, not a card** (`IMoveProvider::playCard()`). A player cannot
  name a card they do not hold, so a provider wrapping an untrusted client cannot cheat that way.
- **The engine guards against being re-entered.** `DispatchGuard` and `DrivingGuard` stop an
  observer callback or a provider from calling back into a running engine.
- **Stopping is safe from another thread.** `requestStop()` sets an atomic flag, and the rest of the
  class is documented as single-threaded.
- **Repeatable games.** `GameSetup::shuffleSeed` makes a deal reproducible, and the strategies that
  use randomness take seeds of their own.

### The tests are excellent

- 188 test cases, and almost as much test code as library code.
- **A property test** (`RandomPlayPropertyTests.cpp`) plays 2,000 seeded games and checks every card
  against its own implementation of the rules, not the engine's, so it would still catch a bug in
  `CardValidator`.
- **Golden games** (`GoldenGameTests.cpp`) pin the exact outcome for 2, 4 and 6 players and both
  game shapes.
- **Tournament tests** check that each AI beats weaker ones by a clear margin and within a time
  budget.
- **The README's quick start is a test** (`ReadmeQuickStartTests.cpp`): it is compiled, played to
  the end, and checked against the text printed in the README.

### It behaves well as a library

- Warnings and tests are on only when the engine is the top-level project
  (`PROJECT_IS_TOP_LEVEL`), so it cannot break the build of a project that includes it.
- Consumers link one alias, `RomanianWhist::engine`, and warnings are `PRIVATE`.
- CI builds with warnings as errors on Linux, Windows and macOS, plus a Linux Debug build.
- Versions follow semantic versioning, `CHANGELOG.md` explains every breaking change, and the README
  is a real manual: setup, observers, providers, threading, stopping and failure, upgrading.

### It plays the same on every platform

`-ffp-contract=off` stops the compiler from fusing a multiply and an add into one instruction,
which rounds differently. Without it, the Cyborg broke ties differently on Apple ARM and games
diverged from Linux and Windows. Most projects never notice this problem.

### The comments are mostly right

About 3 comment lines per 10 lines of code, and the heaviest files are public headers, where a
library should explain itself. Project history comes up only 16 times. References such as "§4.4"
point at the AI design documents in `docs/`, which describe the algorithms, not a plan.

## Weaknesses

### 1. Code from different periods is written in different styles

The core (`Card`, `Round`, `GameEngine`, the simple strategies) and the newer AI code (`cyborg/`,
`reckoner/`) read like two projects:

- **Header guards.** 23 of the 41 public headers use generic guards: `CARD_H`, `ROUND_H`,
  `PLAYER_H`, `DECK_H`, `SEAT_H`, `TRICK_H` and others. A consumer with its own `Card.h` guarded by
  `CARD_H` would silently lose one of the two headers. The newer headers use
  `ROMANIAN_WHIST_<NAME>_H`, which is safe.
- **Names.** The core uses full words. The AI code has `cand`, `sab`, `lam`, `res`, `c1`, `c2`, `o`
  (`reckoner/RolloutEngine.cpp`, `cyborg/Bidding.cpp`).
- **Braces and loops.** Most of the code puts braces on their own line, but 11 lambdas open on the
  same line (`cyborg/Bidding.cpp:147`, `reckoner/BasePolicy.cpp:191` and others). Loops are written
  `i = 0 ; i < n ; i++` in the core and `o = 0; o < n; ++o` in the AI code.
- **Trailing whitespace** in 5 files (`Scoreboard.h`, `IStrategy.h`, `Scoreboard.cpp`,
  `GameEngine.cpp`, `Player.cpp`).

There is no `.clang-format`, so nothing keeps the styles from drifting further apart.

### 2. The scoring rule is written three times

"5 plus the bid for an exact bid, minus the difference otherwise" appears in:

- `src/Scoreboard.cpp:208`, the real score;
- `src/strategies/reckoner/RolloutEngine.cpp:55` and `:80`, the Reckoner's simulated score, which
  also repeats the 5-in-a-row bonus of 10 (`:62` to `:68`);
- `src/strategies/cyborg/Bidding.cpp:366`, the Cyborg's expected score.

The numbers 5 and 10 have no names anywhere, even in `Scoreboard.cpp:175` to `:188`. If the rule
ever changed in `Scoreboard`, both AIs would go on optimising for the old one, and no test would
fail: they would just play worse.

### 3. A name hides what the function does

`Player::resetCurrentRoundScore()` (`src/Player.cpp:92`) adds the round's score to the total and
then sets it to zero. The name says only the second half. `Scoreboard::commitRoundScores()` calls
it under the comment "Commit round scores to total scores", which is right, but only because the
reader has to know what "reset" does here.

### 4. A few API quirks

- **`CardValidator` holds no state, but `getLegalCards()` is not static**
  (`CardValidator.h:18`), so callers must create an object to call it. The desktop client keeps a
  `CardValidator` member for exactly that reason.
- **`IStrategy`, an interface, carries a member** (`IStrategy.h:18`, a protected `CardValidator`)
  only so strategies can call that non-static function.
- **A default-constructed `Card` is a real card**, the 2 of clubs (`Card.cpp:5`). A `Card` that was
  never set cannot be told apart from one that was.
- **`reckoner/CardMask.h` is only a compatibility shim** that forwards to `common/CardMask.h`, kept
  so older files compile unchanged.
- **`find_package()` is promised but not there.** `CMakeLists.txt:41` mentions "a future
  find_package()", but there are no install or export rules, so the only way in is a submodule.

### 5. One function is a long chain of special cases

`bidTwoTricksAsLeader()` (`cyborg/Bidding.cpp:133`) is 118 lines: eight true/false variables, one
per card category, then ten `if` blocks, each choosing a bid and a card to lead for one pair of
categories. The numbered comments above each block already read like a table. Otherwise the code is
in short functions: only 8 of 253 are longer than 60 lines.

### 6. C++20 is mostly unused

The engine requires C++20 and uses some of it well (`<bit>`'s `std::popcount`, a defaulted
`operator==` on `Card`), but:

- No `std::ranges`: `std::find(v.begin(), v.end(), x)` appears 7 times and `std::sort` with
  iterator pairs 4 times.
- No `[[nodiscard]]` and no `noexcept`.
- 9 lines compare ranks by casting both to `int`, such as
  `static_cast<int>(candidate.rank) > static_cast<int>(currentBest.rank)` in
  `CardValidator.cpp:55` and `:65`. `Rank` is an `enum class`, and those compare directly:
  `candidate.rank > currentBest.rank`.

### 7. No sanitizers or static analysis

There is no ASan/UBSan preset and no clang-tidy. The engine has no Qt dependency, so a sanitizer
build would be cheap to add to CI, and it would cover the bit manipulation in `CardMask` and the
Reckoner's sampling.

### 8. Smaller points

- Some comments still tell history: "Both used to be pointers" (`CardValidator.h`), "the two guards
  that used to stand here" (`GameEngine.cpp`, in `dealCards()`).
- Some older comments repeat the code: `// Check for 5 consecutive wins` above
  `if(player.getConsecutiveWins() == 5)` (`Scoreboard.cpp:174`).
- `docs/cyborg-implementation-plan.md` is a finished plan. The design documents next to it are worth
  keeping; the plan is not.
- `.vscode/` is committed, which sets one editor's preferences for everyone.

## Suggestions, most important first

### 1. One scoring rule, used by everyone

Put the rule in a public header, with named numbers, and have `Scoreboard`, the Reckoner and the
Cyborg call it:

```cpp
namespace romanian_whist::scoring
{
inline constexpr int exactBidBonus = 5;
inline constexpr unsigned int streakLength = 5;
inline constexpr int streakBonus = 10;

constexpr int roundScore(unsigned int bid, unsigned int tricks)
{
    if(bid == tricks)
        return exactBidBonus + static_cast<int>(bid);

    const int difference = static_cast<int>(bid) - static_cast<int>(tricks);
    return difference < 0 ? difference : -difference;
}
} // namespace romanian_whist::scoring
```

The scores are small whole numbers, which `float` and `double` hold exactly, so the AIs compute the
same values as now. The golden and tournament tests should pass unchanged, and they are what proves
it.

### 2. Add a `.clang-format`, and give every header guard the same prefix

- Write a `.clang-format` for the style most of the code already uses (braces on their own line,
  4-space indent, about 100 columns), run it once over the code in a PR of its own, and check it in
  CI.
- Rename the 23 generic guards to `ROMANIAN_WHIST_<NAME>_H`. Nothing outside a header refers to its
  guard, so this breaks no consumer.

### 3. Fix the names and the API quirks

- Rename `Player::resetCurrentRoundScore()` to `commitRoundScore()`. Consumers only see players
  through `const` references (`GameEngine::getPlayers()`), so they cannot call it and the rename
  does not break them.
- Make `CardValidator::getLegalCards()` and `hasSuit()` static. Calling a static function through an
  object still compiles, so existing callers keep working.
- Then `IStrategy` no longer needs its `cardValidator` member. Removing it breaks strategies written
  outside the engine that use it, so keep it until the next major version, and say so in
  `CHANGELOG.md`.
- Delete `reckoner/CardMask.h` once the Reckoner's files include `common/CardMask.h` directly.
- Either add install and export rules for `find_package()`, or drop the promise from
  `CMakeLists.txt` until a consumer needs it.

### 4. Add a sanitizer job and clang-tidy to CI

- Add an `asan` preset (`-fsanitize=address,undefined`) and run its tests on Linux in CI.
- Add a `.clang-tidy` with `bugprone-*`, `modernize-*` and `readability-*` (minus the noisy checks)
  and run it in the Linux job. `modernize-use-ranges` and `modernize-use-nodiscard` will find most of
  suggestion 6.

### 5. Turn `bidTwoTricksAsLeader()` into a table

Each rule is a pair of categories, a bid, and which card to lead, so the rules can be data:

```cpp
enum class Lead
{
    Higher,      // the higher of the two cards
    Lower,       // the lower of the two cards
    First,       // the card in the rule's first category
    Second       // the card in the rule's second category
};

struct TwoCardRule
{
    Category first;
    Category second;
    unsigned int bid;
    Lead lead;
};

constexpr TwoCardRule twoCardRules[] =
{
    { Category::BigTrump,   Category::BigTrump,    2, Lead::Higher },
    { Category::BigTrump,   Category::BigNonTrump, 2, Lead::First  },
    { Category::BigTrump,   Category::SmallTrump,  1, Lead::Second },
    // ...the other seven, in the order the code checks them now
};
```

The function then finds the rule whose pair matches the two cards' categories in either order. The
table replaces the eight variables and ten branches, and it reads like the comments do today.

### 6. Use C++20 where it reads better

```cpp
if(std::ranges::find(legalCards, *playedCard) == legalCards.end())
    throw std::logic_error(player.getName() + " played a card that is not legal in this trick");

return candidate.rank > currentBest.rank;
```

Add `[[nodiscard]]` to functions whose result must be used, such as
`CardValidator::getLegalCards()`, `CardValidator::beats()` and `GameEngine::isBetLegal()`.

### 7. Tidy the rest

- Remove the history and the comments that repeat the code (see weakness 8). **Done for new code:**
  `CLAUDE.md` now has the same rule for comments as the desktop client's.
- Delete `docs/cyborg-implementation-plan.md`, or move it to `docs/archive/`.
- Remove `.vscode/` and add it to `.gitignore`.

## What would make it an 8 or 9

Suggestions 1 to 4. They change no behaviour the tests pin, they make the code read as one project,
and they let CI catch the kinds of mistakes the tests do not cover.
