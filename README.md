# Romanian Whist Engine

A reusable C++20 game engine for [Romanian Whist](https://en.wikipedia.org/wiki/Romanian_Whist).
It contains all the game logic — dealing, betting, trick resolution, scoring — and **no
user interface**.

The engine owns the game loop. You supply two things: an `IMoveProvider` per seat, which answers
"what do you want to do?", and — if you are drawing anything — an `IGameObserver`, which is told
what just happened. The engine ships with AI opponents, so any client gets computer players for
free.

**Start here:**

- Writing a client? [Quick start](#quick-start), then
  [Watching a game](#watching-a-game--igameobserver) and
  [Supplying moves](#supplying-moves--imoveprovider).
- Is your UI on a different thread from the game (Qt, a web backend)? Read
  [Threading, stopping, and failure](#threading-stopping-and-failure) **first**. It is not
  optional, and it is not discoverable from a compile error.
- Writing an AI? [docs/STRATEGIES.md](docs/STRATEGIES.md).
- Coming from engine 3.x? [CHANGELOG.md](CHANGELOG.md) has the migration table. Almost everything
  moved.

The rules the engine implements — the schedules, the bidding restriction, the scoring — are
written down in [docs/RULES.md](docs/RULES.md).

---

## Requirements

- A C++20 compiler
- CMake 3.21 or newer

## Adding the engine to your project

### Step 1 — add it as a git submodule

From the root of your project:

```bash
git submodule add https://github.com/conectionist/romanian_whist_engine.git libs/RomanianWhistEngine
git commit -m "Add Romanian Whist engine as a submodule"
```

This clones the engine into `libs/RomanianWhistEngine/` and records the exact commit
your project depends on.

### Step 2 — wire it into your CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyWhistGame LANGUAGES CXX)

add_subdirectory(libs/RomanianWhistEngine)

add_executable(my_whist_game src/main.cpp)
target_link_libraries(my_whist_game PRIVATE RomanianWhist::engine)
```

That is all the wiring required. The include directory comes through automatically, so
you do **not** need a `target_include_directories` call of your own.

### Step 3 — include the headers

```cpp
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>
```

### Cloning a project that uses the engine

A plain `git clone` leaves submodule directories empty. Use either:

```bash
git clone --recurse-submodules <your-project-url>
```

or, if you already cloned without it:

```bash
git submodule update --init --recursive
```

### Updating to a newer version of the engine

Releases are tagged, so pin one rather than tracking a branch:

```bash
cd libs/RomanianWhistEngine
git fetch --tags
git checkout v4.3.0
cd ../..
git add libs/RomanianWhistEngine
git commit -m "Update Romanian Whist engine to 4.3.0"
```

`git pull origin master` also works if you would rather track the tip. Either way, read
[CHANGELOG.md](CHANGELOG.md) before crossing a major version — 4.0.0 broke most of the 3.x API.

---

## Building the engine on its own

```bash
cmake --preset default
cmake --build build
```

This produces `build/libromanian_whist_engine.a`.

### Running the tests

```bash
ctest --preset default --output-on-failure
```

The suite covers the rules, the observer contract, move validation, cancellation, seven full
games pinned to golden scores, and the shared round tracker pinned to golden state.

One test — Reckoner's exact final scores — is compiled only on Linux/GCC, the toolchain it was
pinned against. Reckoner decides by argmax over floats, so a different libm plays a different
game; `tests/CMakeLists.txt` has the measurements. Nothing else in the suite is float-dependent.

`WHIST_BUILD_TESTS` defaults to `PROJECT_IS_TOP_LEVEL`, so a project that consumes the engine
through `add_subdirectory` builds **neither** the tests nor Catch2. You pay for them only when
you are working on the engine itself.

### Build options

| Option | Default | What it does |
|---|---|---|
| `WHIST_BUILD_TESTS` | `PROJECT_IS_TOP_LEVEL` | Builds the Catch2 suite, and fetches Catch2 |
| `WHIST_ENABLE_WARNINGS` | `PROJECT_IS_TOP_LEVEL` | `-Wall -Wextra -Wshadow -Wpedantic -Wnon-virtual-dtor -Woverloaded-virtual`, or `/W4` on MSVC |
| `WHIST_WARNINGS_AS_ERRORS` | `OFF` | Adds `-Werror`, or `/WX` on MSVC |

Both warning options are off for a consumer by design. Compile options are *appended* to the ones
your own build already sets, so if ours were always on, your global `-Werror` would apply to our
`-Wextra` on our sources — and the next compiler release that adds a warning would break your
build on our code. Turn them on deliberately with `-DWHIST_ENABLE_WARNINGS=ON` if you want them.

To build the way CI does:

```bash
cmake --preset release -DWHIST_WARNINGS_AS_ERRORS=ON
cmake --build --preset release
```

---

## Quick start

A complete program: three AI players, a seeded game, one observer that narrates it.

```cpp
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>

#include <iostream>
#include <memory>

using namespace romanian_whist;

class Commentator : public IGameObserver
{
public:
    void onTrickWon(const GameEngine& engine, Seat winner, unsigned int trickNumber) override
    {
        std::cout << "Trick " << trickNumber << " to "
                  << engine.getPlayers().at(winner.index).getName() << '\n';
    }

    void onGameOver(const GameEngine& engine) override
    {
        for(const Standing& standing : engine.getStandings())
            std::cout << standing.name << ": " << standing.score << '\n';
    }
};

std::unique_ptr<IMoveProvider> bot(std::uint32_t seed)
{
    return std::make_unique<AiMoveProvider>(std::make_unique<RandomCardStrategy>(seed));
}

int main()
{
    // The observer is declared first, so it is destroyed last: addObserver()
    // takes a raw pointer and the engine must not outlive what it points at.
    Commentator commentator;
    GameEngine game;

    game.addObserver(&commentator);

    GameSetup setup;
    setup.seats.push_back({"Ana", bot(1)});
    setup.seats.push_back({"Bogdan", bot(2)});
    setup.seats.push_back({"Carmen", bot(3)});
    setup.structure = GameStructure::S_181;
    setup.shuffleSeed = 42;

    game.start(std::move(setup));
    game.run();
}
```

> This snippet is compiled on every build, as `tests/ReadmeQuickStartTests.cpp` — and that
> test reads this file back and fails if the two have drifted apart. Change one, change the
> other; the suite will tell you if you forget.

Replace a bot with your own `IMoveProvider` and you have a playable game. Everything below is
detail on the four things that program does: set up, watch, decide, and read.

---

## How a game runs

```
your client                          the engine
-----------                          ----------
build a GameSetup      ------->      start()      validates, deals nothing yet
                                                  fires onGameStarted
run()                  ------->      playRound()  for each round in the schedule:
                                                    deal, then ask each seat to bid
                       <-------        IMoveProvider::makeBet
                                                    then play each trick
                       <-------        IMoveProvider::playCard
                       <-------        IGameObserver::on...   throughout
                                                    score, commit, advance
                                     ...until the schedule runs out
```

`run()` blocks until the game is over. It is not asynchronous, there is no message queue, and
there is no thread inside the engine — everything above happens on the thread that called
`run()`. That is the one design decision the rest of this document keeps coming back to.

Two seams cross that boundary:

| Interface | Answers | Required |
|---|---|---|
| [`IMoveProvider`](include/romanian_whist/IMoveProvider.h) | "What do you want to do?" | yes, one per seat |
| [`IGameObserver`](include/romanian_whist/IGameObserver.h) | "Here is what just happened" | only if you render something |

---

## Setting up a game

Everything the engine needs arrives in one `GameSetup`, validated as a whole:

```cpp
GameSetup setup;
setup.seats.push_back({"You", std::make_unique<MyUiMoveProvider>()});
setup.seats.push_back({"Bot", std::make_unique<AiMoveProvider>(
                                  std::make_unique<LowRiskStrategy>())});

setup.structure = GameStructure::S_181;   // 1-8-1; S_818 runs 8-1-8
setup.endWithForeheadAndHidden = true;    // append one Forehead and one Hidden round
setup.all1GamesAreForehead = false;       // make every one-card round Forehead
setup.shuffleSeed = 42;                   // optional; unset seeds from std::random_device

game.start(std::move(setup));
```

`GameSetup` owns the move providers, so it is move-only and every call reads
`start(std::move(setup))`.

`start()` throws `std::invalid_argument` if the setup is not playable — fewer than 2 or more than
6 seats, an empty name, or two names equal byte for byte. It validates the whole setup before
applying any of it, so a rejected `start()` leaves the **engine** untouched and still
`NotStarted`, ready to be started again.

The `GameSetup` does not survive the attempt. `start()` takes it **by value**, so
`start(std::move(setup))` empties `setup.seats` — destroying the move providers it owned — before
validation so much as runs. Retrying means building a fresh `GameSetup`, new move providers
included; there is nothing left in the old one to fix up.

**Register observers before `start()`.** `onGameStarted()` fires from inside it, and an observer
registered afterwards has already missed the game beginning.

`shuffleSeed` makes the **deal** reproducible, and nothing else. A move provider is free to use
its own randomness — `RandomCardStrategy`'s default constructor still draws from
`std::random_device` — so a fully reproducible game needs every such provider seeded too, which is
what `RandomCardStrategy(std::uint32_t)` is for.

---

## Watching a game — IGameObserver

Your client does not drive the game. It watches one:

```cpp
#include <romanian_whist/IGameObserver.h>

class MyUi : public romanian_whist::IGameObserver
{
public:
    void onRoundStarted(const romanian_whist::GameEngine& engine) override
    {
        draw(engine);                    // dealt, trump known, nobody has bid yet
    }

    void onCardPlayed(const romanian_whist::GameEngine& engine,
                      romanian_whist::Seat,
                      const romanian_whist::Card&) override
    {
        draw(engine);
    }
};
```

All thirteen callbacks are no-ops by default, so implement only what you render. Every one is
handed `const GameEngine&`, so an observer never needs to keep a parallel copy of the game — it
reads what it needs at the moment it is told something changed.

Three rules, and they are the whole model:

1. **Callbacks are synchronous, on the thread that called `run()`.** There is no queue and no
   dispatch thread. A callback runs between two engine operations, and the engine resumes when it
   returns.
2. **Blocking in a callback pauses the game.** That is where pacing and "press Enter to continue"
   belong — it is the intended use, not an abuse of one.
3. **Observers must outlive the engine.** `addObserver()` stores a raw pointer and takes no
   ownership.

### The order events fire in

```
onGameStarted                                set up, not yet dealt
  onRoundStarted                             dealt, trump known
    onBetRequested -> onBetPlaced            per seat, in bidding order
  onBettingComplete
    onTrickStarted                           per trick
      onCardRequested -> onCardPlayed        per seat, in play order
    onTrickWon
  onRoundScored                              scored, not yet committed
  onRoundComplete                            committed, round index advanced
onGameOver             -- or onGameStopped, never both
```

**Read the engine at the right callback.** Two of these are easy to get wrong, and both fail
silently:

- **`onRoundScored` is the round-end render hook, not `onRoundComplete`.** It is the last callback
  at which `getCurrentRoundIndex()` still names the round being reported, and at which
  `getRoundScore()` is what the round was worth. By `onRoundComplete` the index has advanced and
  the total has absorbed the round. A "round 5 of 26 — here is what it was worth" screen drawn
  from `onRoundComplete` prints round 6's number over round 5's scores.
- **`onTrickWon` fires while `getCurrentTrick()` is still the finished trick** — but
  `getTrickLeaderSeat()` has already moved to its winner. A client that rebuilds its table as
  `(leader + i) % playerCount` inside that callback draws every card against the wrong seat, on
  the one screen a player stops and reads. Take the seat off each `PlayedCard` instead; that is
  why a trick carries seats and not just cards.

The exact state readable inside each callback is documented on the callback itself in
[`include/romanian_whist/IGameObserver.h`](include/romanian_whist/IGameObserver.h). Read it once
before writing a renderer.

### What a callback must not do

- **Add or remove an observer.** The engine is walking that list to reach you; both throw
  `std::logic_error` rather than corrupt the walk in progress. An observer that wants to detach
  sets a flag and lets the client detach it between rounds.
- **Call `run()` or `playRound()`.** Both throw from every callback except `onGameStarted()`,
  which the engine makes while it is still idle. A re-entered round deals over the hand the outer
  round is midway through playing.
- **Let the `const GameEngine&` escape to another thread.** See
  [Threading](#threading-stopping-and-failure).

`requestStop()` is the one thing a callback may always do.

---

## Supplying moves — IMoveProvider

One per seat. This is where a game waits for a human.

```cpp
namespace romanian_whist {

class IMoveProvider
{
public:
    virtual ~IMoveProvider() = default;

    // How many tricks does this player bet they will win?
    virtual unsigned int makeBet(const BetContext& context) = 0;

    // Which card? The position of the chosen card WITHIN context.hand.
    // Empty says "no legal play", which the engine treats as an error.
    virtual std::optional<std::size_t> playCard(const PlayContext& context) = 0;
};

}
```

**`playCard` returns an index, not a card.** An index cannot name a card the player does not hold:
the engine range-checks it and erases by position, so a fabricated card is not merely rejected, it
cannot be expressed. That matters most for a provider that is a thin shim over an untrusted
client.

The index is into **`context.hand` as given** — not into a sorted, filtered or legal-only view of
it. A provider that shows the player a rearranged list owes the translation back to a hand
position. `ConsoleMoveProvider` in the terminal client is the worked example.

`BetContext` carries everything the bid decision needs:

```cpp
struct BetContext
{
    const std::vector<Card>& hand;               // size == the round's trick count
    std::optional<Card> trump{};                 // empty in 8-card rounds
    bool isFirstPlayer = false;                  // opens the bidding
    std::optional<unsigned int> forbiddenBet{};  // see below
    RoundType roundType = RoundType::Normal;     // Normal / Forehead / Hidden
};
```

`PlayContext` does the same for the card decision:

```cpp
struct PlayContext
{
    const std::vector<Card>& hand;
    const std::vector<Card>& playedCards;        // this trick, in play order
    std::optional<Card> trump{};                 // empty in 8-card rounds
    std::optional<Suit> leadSuit{};              // empty when leading
    unsigned int bet = 0;                        // this player's bid this round
    unsigned int tricksWon = 0;                  // of it, so far
};
```

Cards are held **by value** throughout. There are no `Card*` and no null checks anywhere in the
engine's public API; "none" is an empty `std::optional`.

### Choosing a legal card

```cpp
#include <romanian_whist/CardValidator.h>

#include <algorithm>
#include <stdexcept>

std::optional<std::size_t> playCard(const romanian_whist::PlayContext& context) override
{
    romanian_whist::CardValidator validator;

    // Applies the rules for you: follow suit, else trump, else anything.
    const std::vector<romanian_whist::Card> legal =
        validator.getLegalCards(context.hand, context.trump, context.leadSuit);

    if(legal.empty())
        return std::nullopt;

    const romanian_whist::Card chosen = askUserToPick(legal);

    // getLegalCards returns a filtered copy, so translate back to a hand position.
    const auto position = std::find(context.hand.begin(), context.hand.end(), chosen);

    // legal is a subset of hand, so this only trips if askUserToPick returned a
    // card it was never offered. Do not hand the engine end() - std::nullopt
    // means "no legal play", which is a different bug from this one.
    if(position == context.hand.end())
        throw std::logic_error("picked a card that is not in the hand");

    return static_cast<std::size_t>(std::distance(context.hand.begin(), position));
}
```

> Use `CardValidator::getLegalCards` rather than writing your own rule checks — it
> enforces following suit and trumping correctly.

`playedCards` is what makes it possible to ask whether a card would actually win: pass it to
`CardValidator::getWinningCard`, then `CardValidator::beats`. A card that does not beat the
current winner cannot take the trick however many players are still to go, since they can only
push the winner higher.

### The bidding restriction

The final bidder may not make the round's bids add up to exactly the trick count. The engine owns
that rule so clients do not each re-derive it:

```cpp
// The bid the final bidder may not make. Empty for every other bidder, and
// empty once the bids already exceed the trick count - no bid can hit the
// total then. It arrives in BetContext::forbiddenBet, and honouring it
// whenever it is present is enough to always bid legally.
std::optional<unsigned int> forbidden = game.getForbiddenBet();

// Or check a bid you already have, range included.
if(!game.isBetLegal(bet))
    reject(bet);
```

A legal bid is one in `[0, getCurrentRoundTrickCount()]` that is not `*getForbiddenBet()`. The
engine validates every bid it is given and throws on an illegal one, so this is for *prompting* —
re-ask the human before answering, rather than letting the game die on their typo.

---

## Reading game state

A seat is named by `Seat`: a value that identifies a chair and carries no access to the player in
it. Build one from a loop index as `Seat{i}` and go back with `seat.index`. It is explicitly
constructed on purpose — a seat travels next to counts that look exactly like it, and an alias for
`unsigned int` would let a swapped argument compile.

| Question | Accessor |
|---|---|
| Whose turn is it? | `getActiveSeat()` — empty between turns |
| What did seat N bid? | `getBet(Seat)` — empty until they have bid |
| How many tricks have they taken? | `getTricksWon(Seat)` |
| What is on the table? | `getCurrentTrick()` — each entry carries its seat |
| Who is winning this trick so far? | `getCurrentTrickLeader()` |
| Who leads the next trick? | `getTrickLeaderSeat()` |
| Who opened this round? | `getRoundLeaderSeat()` |
| Where does seat N bid in the order? | `getBiddingOrder(Seat)` — 1-based |
| Which round, of how many? | `getCurrentRoundIndex()`, `getRoundCount()` |
| How many cards this round, and what type? | `getCurrentRoundTrickCount()`, `getCurrentRoundType()` |
| What is trump? | `getCurrentTrumpCard()` — empty in 8-card rounds |
| What happened in an earlier round? | `getRound(index)` |
| Where in the round are we? | `getPhase()` |
| Is the game over, and how? | `getStatus()`, `isInProgress()` |

**`getTrickLeaderSeat()` and `getRoundLeaderSeat()` name the same seat until the first trick is
won, and different seats from then on.** Bidding order, and anything measured from where the round
began, wants the round leader.

### Scores

```cpp
for(const Standing& standing : game.getStandings())   // best first
    display(standing.place, standing.name, standing.score);

for(const Standing& winner : game.getWinners())       // every seat on the top score
    announce(winner.name);

display(game.getRoundScore(seat));    // what this round is worth so far
display(game.getTotalScore(seat));    // the committed total
```

`Standing::place` is a competition ranking: seats level on points share a place, and the places a
tie consumes are skipped, so 50, 40, 40, 30 ranks 1, 2, 2, 4 and that game has no third place.
Romanian Whist has no tie-breaker, so `getWinners()` returns a *list* — more than one row for a
drawn game, and never empty for a started game. Reading `getStandings().front()` instead is the
easy mistake: it names a single winner of a game two seats drew. Both read during a game too,
where they mean "currently leading"; only a `Finished` game's standings are final.

`getTotalScore()` does not include the current round until that round is committed. Inside
`onRoundScored()` the two are "what this round was worth" and "the total it has not yet been added
to", so a projected total is their sum; by `onRoundComplete()` the round score is 0 and the total
includes it. Render one without the other and the scoreboard reads differently either side of the
commit.

### Before the game has started

Every accessor whose answer depends on a round throws `std::logic_error` until `start()` has laid
out the schedule. `isSetUp()` is how you ask; `getStatus()`, `getPhase()`, `isInProgress()` and
`getPlayerCount()` are callable at any time. This matters for a client with a setup screen — a
scoreboard widget drawn before the wizard finishes, or a `GET /game/{id}` on a game that was
created but never started.

---

## Round types and who may see a hand

Most rounds are `RoundType::Normal`. A schedule can also contain:

- **`Forehead`** — a one-card round in which you may see everyone else's card but not your own.
- **`Hidden`** — a one-card round in which nobody sees any card before bidding.

`GameSetup::endWithForeheadAndHidden` appends one of each to the end of the schedule;
`all1GamesAreForehead` marks every one-card round Forehead. A bidder is told which through
`BetContext::roundType`; a renderer asks `getCurrentRoundType()`.

### `canSeeHand()` is a rule, not a guard

```cpp
bool canSeeHand(Seat viewer, Seat holder) const;
```

Normal: only your own. Forehead: everyone's but your own. Hidden: nobody's. It exists so that two
clients cannot disagree about the rule.

**It enforces nothing. The engine will hand any caller any seat's cards.** `getPlayers()` and
`Player::getHand()` are public and have to stay public — a Forehead round is *drawn* by reading
other seats' hands. Blinding one struct while the same cards sit one accessor away would be a
speed bump, not a wall.

For a terminal client that distinction is harmless: a card it does not draw never leaves the
process. **For a networked client it is the whole thing.** A server that builds a player's payload
straight from the game —

```cpp
// WRONG. Sends every seat their own Forehead card, over the wire, to a
// client you do not control.
for(unsigned int i = 0 ; i < engine.getPlayerCount() ; i++)
    payload.seats.push_back({ engine.getPlayers().at(i).getName(),
                              toJson(engine.getPlayers().at(i).getHand()) });
```

— has already lost, and hiding the card in the browser does not get it back. It is in the response
body, in the network tab, and in the first client somebody writes themselves.

Route every hand read through the viewer, so that *not passing a viewer* is not something you can
express:

```cpp
// The only function in the backend that calls Player::getHand().
std::vector<CardDto> visibleHand(const GameEngine& engine, Seat viewer, Seat holder)
{
    if(!engine.canSeeHand(viewer, holder))
        return {};        // absent from the payload, not "hidden": true in it

    return toJson(engine.getPlayers().at(holder.index).getHand());
}
```

This is the same argument `playCard` makes by returning an index rather than a card — with one
difference worth being precise about. An index makes a fabricated card inexpressible **in the
engine**. `canSeeHand()` cannot do that, and does not claim to. One serializer that takes a viewer
makes an invisible card inexpressible **in your backend**, which is the boundary you control.
Write it once, then `grep -n "getHand()" src/` and check that function is the only hit.

The rule is broader than the special rounds: in a Normal round a backend must already send seat A
only A's cards. Forehead merely inverts it. A spectator is not a seat, so "a spectator sees
everything" stays your policy and simply does not consult `canSeeHand()`.

> **For strategy authors.** Forehead and Hidden mean the same thing to an AI today: `BetContext`
> has no field for other seats' cards, so both are "bid on nothing". The hand you are given in
> those rounds is the **real** one — the engine has no other one to give — so a strategy that
> reads it is bidding on information the round says it does not have. Check `context.roundType`;
> `LowRiskStrategy` is the one bundled strategy that has to.

---

## Threading, stopping, and failure

**One thread per engine, and no internal locking.** A `GameEngine` is created, played and
destroyed on a single thread. There is no mutex anywhere in it, and there should not be: a client
that needs concurrency owns the thread, not the engine. `requestStop()` is the single exception —
it sets an atomic flag and is safe to call from anywhere. `addObserver()` and `removeObserver()`
are **not**.

**`IMoveProvider` is the suspend point.** `run()` blocks until the game is over, and when it is a
human's turn it is blocked inside your `playCard()`. That is the whole cost of letting the engine
own the loop, and it is only a cost if your UI lives somewhere else.

**If your UI is not on the game thread, snapshot inside the callback:**

```cpp
void onCardPlayed(const GameEngine& engine, Seat, const Card&) override
{
    GameSnapshot snapshot = snapshotFrom(engine);   // still on the game thread
    emit stateChanged(snapshot);                    // queued; the GUI thread gets a copy
}
```

Never hand another thread a `const GameEngine&`, a `const Round&`, a `const Player&`, or a pointer
into anything the engine owns. A cross-thread read racing a deal is a torn read of a hand — a card
half-removed, a vector resized under a loop. The Qt client's
[QT_CLIENT_PLAN.md](https://github.com/conectionist/romanian_whist_desktop/blob/master/docs/QT_CLIENT_PLAN.md) §3–4 works this out end to end: engine on a worker thread, observer snapshots and emits queued signals, move
provider parked on a condition variable until a click. A web backend is the same shape with an
HTTP request in place of the click, at the cost of one parked thread per game in progress.

### Stopping a game

`requestStop()` ends the game cleanly at the next round, bid or trick boundary. The status becomes
`Stopped` and observers get `onGameStopped()` instead of `onGameOver()`.

- The round it lands in is **left unscored** — including one caught on that round's final trick,
  which is played out and then abandoned.
- A stop raised during bidding is honoured before the next seat is asked, so the rest of the table
  is never prompted for a hand that is about to be thrown away.
- A stop asked for once the final round has been scored is a no-op: there is no boundary left to
  land on, so the game stays `Finished` and `onGameOver()` is what fired.
- **It cannot interrupt a move provider already parked on a human.** The flag is only read between
  bids, tricks and rounds.

A stopped game is not resumable. Destroy the engine.

### Abandoning a game whose provider is parked

A window closed mid-turn is exactly the case `requestStop()` cannot reach. A provider has no
non-throwing way to say "abandon" — every other return value is a move, and `std::nullopt` means
"no legal play", which the engine treats as a bug. So wake it and **throw**:

```cpp
std::optional<std::size_t> playCard(const PlayContext&) override
{
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [this]{ return submitted.has_value() || abandoned; });

    if(abandoned)
        throw GameAbandoned{};      // propagates out of run()

    // Take the move and clear it, or the next turn returns this same index
    // without ever waiting - and the engine throws on the illegal play.
    const std::optional<std::size_t> chosen = submitted;
    submitted.reset();
    return chosen;
}
```

A GUI needs both halves: `requestStop()` for the idle case, wake-and-throw for the parked one.

### When something throws

An exception from a move provider or an observer propagates out of `run()`/`playRound()`
unchanged, and leaves the engine mid-round and **not resumable**. Nothing here is exception-safe
in the strong sense and it does not need to be: the state is already inconsistent, and pretending
otherwise is worse. Catch it outside `run()`, tell the user, destroy the engine — and restore
whatever you set up before the call, because nothing else will.

`start()` is the one call that fails cleanly. See [Setting up a game](#setting-up-a-game).

---

## Built-in AI strategies

`AiMoveProvider` delegates its decisions to an `IStrategy`:

| Strategy | Behaviour |
|---|---|
| `RandomCardStrategy` | Plays a random legal card; bids a random legal number of tricks |
| `FirstCardStrategy` | Plays the first legal card; bids 0, or 1 when 0 is barred |
| `LowRiskStrategy` | Bids the tricks its hand will take whether it wants them or not — usually 0 — then plays to that bid: takes tricks as cheaply as it can while it still owes some, and ducks every trick after that |
| `DuckingStrategy` | Bids 0 come what may and never chases a trick, dumping its highest cards at the moments they cannot win |
| `CyborgStrategy` | Counts every card played and prices each one exactly, then bids for the best expected score and plays to hit that bid. Rule-based and deterministic, with one strength; about half a microsecond a decision. **Needs registering as an observer — see below.** |
| `ReckonerStrategy` | Counts every card played and searches imagined deals before each decision. Four presets, from a memoryless `Easy` to a `Brutal` that solves the endgame exactly. **Needs registering as an observer — see below.** |

The two low-risk strategies share their judgement calls through
`<romanian_whist/strategies/TrickHeuristics.h>`, which is public — reuse it rather than
re-deriving "is this card safe to play?" for your own strategy.

### CyborgStrategy and ReckonerStrategy are also observers

They are the two strategies here that cannot be built and handed over on their own.
Everything they play from — who has shown void in what, which cards are gone, what everyone
bid — arrives through `IGameObserver`, so each has to be registered with the engine
**before** `start()`, which is where it also resolves its own seat from its name.

Use the factories, which do all of that:

```cpp
GameEngine engine;
GameSetup setup;

setup.seats.push_back(makeCyborgSeat("Ana", engine));
setup.seats.push_back(makeReckonerSeat("Bogdan", engine, reckoner::ReckonerKnobs::hard()));
// ...other seats...

engine.start(std::move(setup));   // AFTER the seats were built against this engine
```

Constructing one directly and skipping `addObserver()` is a mistake the type system cannot
catch, so the strategies catch it instead: both decisions throw `std::logic_error` if they
are reached before the strategy ever saw a round start.

`CyborgStrategy` has no presets and no randomness, so it takes no seed and two of them play
the same game identically; its design is in `docs/romanian-whist-cyborg-ai.md`. Reckoner's
presets are `ReckonerKnobs::easy() / medium() / hard() / brutal()`, and
`docs/romanian-whist-reckoner-ai.md` documents every knob behind them.

Write your own by implementing `IStrategy` from
`<romanian_whist/strategies/IStrategy.h>`: `getBestBet` returns an `unsigned int` and
`getBestChoice` an `std::optional<Card>`. Strategies reason in cards; turning the chosen card into
the index the engine wants is `AiMoveProvider`'s job.

See **[docs/STRATEGIES.md](docs/STRATEGIES.md)** for how each one decides, how they score against
each other, and where they fall down.

---

## Upgrading from 3.x

4.0.0 moved the game loop into the engine, and most of the 3.x public API went with it.
[CHANGELOG.md](CHANGELOG.md) carries the migration table, call by call.

---

## Layout

```
include/romanian_whist/     public headers — this is the include root
├── GameEngine.h            main facade: setup, the loop, and every accessor
├── IMoveProvider.h         implement this to supply moves — one per seat
├── IGameObserver.h         implement this to render — thirteen no-op callbacks
├── BetContext.h            what IMoveProvider::makeBet is handed
├── PlayContext.h           what IMoveProvider::playCard is handed
├── Seat.h                  how the engine names a player
├── Card.h                  Rank, Suit, Card
├── RoundType.h             Normal / Forehead / Hidden
├── Round.h                 one round: bets, tricks, trump, type
├── Trick.h                 PlayedCard (a card and its seat), and the trick
├── Player.h                name, hand, score, streaks
├── PlayerList.h            the table, indexed by seat
├── Scoreboard.h            the round schedule and GameStructure
├── Deck.h                  composition and the seeded shuffle
├── CardValidator.h         legal-move rules and the trick ranking
├── AiMoveProvider.h        AI player driven by an IStrategy
├── strategies/             IStrategy + the six bundled strategies
│   ├── common/             shared across strategies: round memory, card bitmasks
│   ├── cyborg/             CyborgStrategy's internals: odds, bidding, plan, play, knobs
│   └── reckoner/           ReckonerStrategy's internals: sampler, rollout, evaluator,
│                           and the guessing half of its tracker
└── detail/                 internal; not part of the public contract
src/                        implementation
tests/                      Catch2 suite (see Running the tests)
docs/RULES.md               the rules this engine implements
```

## Clients

- [romanian_whist_terminal](https://github.com/conectionist/romanian_whist_terminal) —
  terminal UI client
- [romanian_whist_desktop](https://github.com/conectionist/romanian_whist_desktop) —
  Qt desktop client. Its [QT_CLIENT_PLAN.md](https://github.com/conectionist/romanian_whist_desktop/blob/master/docs/QT_CLIENT_PLAN.md)
  is the worked example of running the engine on a thread of its own
