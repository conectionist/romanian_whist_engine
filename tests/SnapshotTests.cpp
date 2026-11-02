#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/Player.h>
#include <romanian_whist/Snapshot.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

static_assert(std::is_default_constructible_v<GameSnapshot>);
static_assert(std::is_copy_constructible_v<GameSnapshot>);
static_assert(std::is_copy_assignable_v<GameSnapshot>);
static_assert(std::is_default_constructible_v<SeatSnapshot>);
static_assert(std::is_copy_constructible_v<SeatSnapshot>);
static_assert(std::is_copy_assignable_v<SeatSnapshot>);

namespace
{
std::optional<unsigned int> indexOf(std::optional<Seat> seat)
{
    if(!seat)
        return std::nullopt;

    return seat->index;
}

std::vector<std::unique_ptr<IMoveProvider>> randomProviders(unsigned int playerCount, std::uint32_t seed)
{
    std::vector<std::unique_ptr<IMoveProvider>> providers;

    for(unsigned int seat = 0 ; seat < playerCount ; seat++)
        providers.push_back(std::make_unique<AiMoveProvider>(
            std::make_unique<RandomCardStrategy>(seed * playerCount + seat)));

    return providers;
}

// The rule written out again rather than read from canSeeHand(), so the leak check does not
// rest only on the function the snapshot itself calls.
bool rulesLetSee(RoundType type, Seat viewer, Seat holder)
{
    switch(type)
    {
        case RoundType::Normal:   return viewer == holder;
        case RoundType::Forehead: return viewer != holder;
        case RoundType::Hidden:   return false;
    }

    return false;
}

std::string describe(const char* what, std::optional<Seat> viewer,
                     std::optional<unsigned int> holder = std::nullopt)
{
    std::ostringstream text;
    text << what << ": viewer " << (viewer ? std::to_string(viewer->index) : "spectator");

    if(holder)
        text << ", holder " << *holder;

    return text.str();
}

// Returns what is wrong with one viewer's snapshot, or an empty string. One assertion per
// snapshot rather than one per field keeps a few million checks fast enough to run on every
// build.
std::string findProblem(const GameEngine& engine, std::optional<Seat> viewer)
{
    const GameSnapshot snapshot = snapshotFrom(engine, viewer);
    const unsigned int playerCount = engine.getPlayerCount();

    if(indexOf(snapshot.viewer) != indexOf(viewer))
        return describe("viewer not echoed", viewer);

    if(snapshot.seats.size() != playerCount)
        return describe("wrong seat count", viewer);

    if(!snapshot.roundLeader || !snapshot.ledBy)
        return describe("round leader or led-by missing", viewer);

    if(indexOf(snapshot.winning) != indexOf(engine.getCurrentTrickLeader()))
        return describe("winning seat differs from the engine's", viewer);

    const std::vector<PlayedCard>& played = engine.getCurrentTrick().getPlayedCards();

    if(snapshot.table.size() != played.size())
        return describe("table differs from the trick", viewer);

    for(std::size_t i = 0 ; i < played.size() ; i++)
    {
        if(snapshot.table[i].seat != played[i].seat || snapshot.table[i].card != played[i].card)
            return describe("table differs from the trick", viewer);
    }

    unsigned int betsPlaced = 0;
    unsigned int betSum = 0;

    for(unsigned int i = 0 ; i < playerCount ; i++)
    {
        const Seat holder{i};
        const SeatSnapshot& seat = snapshot.seats[i];
        const std::vector<Card>& hand = engine.getPlayers().at(i).getHand();

        const bool engineLetsSee = viewer && engine.canSeeHand(*viewer, holder);
        const bool rulesLet = viewer && rulesLetSee(engine.getCurrentRoundType(), *viewer, holder);

        if(seat.hand.has_value() != engineLetsSee)
            return describe("hand present where canSeeHand() disagrees", viewer, i);

        if(seat.hand.has_value() != rulesLet)
            return describe("hand present where the rules disagree", viewer, i);

        if(seat.hand && *seat.hand != hand)
            return describe("hand differs from the engine's, or is out of order", viewer, i);

        if(seat.cardsLeft != hand.size())
            return describe("cardsLeft differs from the hand's size", viewer, i);

        if(seat.bet != engine.getBet(holder))
            return describe("bet differs from the engine's", viewer, i);

        if(seat.bet)
        {
            betsPlaced++;
            betSum += *seat.bet;
        }
    }

    if(snapshot.betsPlaced != betsPlaced || snapshot.betSum != betSum)
        return describe("bid count or sum is wrong", viewer);

    return {};
}

// Checks every viewer's snapshot at every callback the engine makes.
class LeakChecker : public IGameObserver
{
public:
    bool sawForehead = false;
    bool sawHidden = false;
    unsigned int snapshotsChecked = 0;

    void onGameStarted(const GameEngine& engine) override { check(engine); }
    void onRoundStarted(const GameEngine& engine) override { check(engine); }
    void onBetRequested(const GameEngine& engine, Seat) override { check(engine); }
    void onBetPlaced(const GameEngine& engine, Seat, unsigned int) override { check(engine); }
    void onBettingComplete(const GameEngine& engine) override { check(engine); }
    void onTrickStarted(const GameEngine& engine, unsigned int, Seat) override { check(engine); }
    void onCardRequested(const GameEngine& engine, Seat) override { check(engine); }
    void onCardPlayed(const GameEngine& engine, Seat, const Card&) override { check(engine); }
    void onTrickWon(const GameEngine& engine, Seat, unsigned int) override { check(engine); }
    void onRoundScored(const GameEngine& engine) override { check(engine); }
    void onRoundComplete(const GameEngine& engine) override { check(engine); }
    void onGameOver(const GameEngine& engine) override { check(engine); }
    void onGameStopped(const GameEngine& engine) override { check(engine); }

private:
    void check(const GameEngine& engine)
    {
        // Counted only while a round is dealt, so a pass means those rounds' hands were checked.
        if(engine.getPhase() == GamePhase::Betting || engine.getPhase() == GamePhase::Playing)
        {
            sawForehead = sawForehead || engine.getCurrentRoundType() == RoundType::Forehead;
            sawHidden = sawHidden || engine.getCurrentRoundType() == RoundType::Hidden;
        }

        for(unsigned int i = 0 ; i <= engine.getPlayerCount() ; i++)
        {
            const std::optional<Seat> viewer =
                i < engine.getPlayerCount() ? std::optional<Seat>(Seat{i}) : std::nullopt;

            const std::string problem = findProblem(engine, viewer);

            if(!problem.empty())
                FAIL(problem);

            snapshotsChecked++;
        }
    }
};

// Follows ledBy across every frame of every trick.
class LedByChecker : public IGameObserver
{
public:
    unsigned int tricksWonByAnotherSeat = 0;

    void onTrickStarted(const GameEngine& engine, unsigned int, Seat leader) override
    {
        const GameSnapshot snapshot = snapshotFrom(engine, std::nullopt);

        REQUIRE(snapshot.table.empty());
        REQUIRE(indexOf(snapshot.ledBy) == leader.index);

        ledBy = leader.index;
    }

    void onCardRequested(const GameEngine& engine, Seat) override { requireSameLeader(engine); }
    void onCardPlayed(const GameEngine& engine, Seat, const Card&) override { requireSameLeader(engine); }

    void onTrickWon(const GameEngine& engine, Seat winner, unsigned int) override
    {
        requireSameLeader(engine);

        const GameSnapshot snapshot = snapshotFrom(engine, std::nullopt);

        REQUIRE(snapshot.table.size() == engine.getPlayerCount());
        REQUIRE(indexOf(snapshot.ledBy) == snapshot.table.front().seat.index);
        REQUIRE(indexOf(snapshot.winning) == winner.index);

        if(winner.index != ledBy)
            tricksWonByAnotherSeat++;
    }

private:
    unsigned int ledBy = 0;

    void requireSameLeader(const GameEngine& engine)
    {
        REQUIRE(indexOf(snapshotFrom(engine, std::nullopt).ledBy) == ledBy);
    }
};

// The frames the pinned test reads, each as seen by seat 1 and by a spectator.
struct Frame
{
    GameSnapshot seat1;
    GameSnapshot spectator;
};

Frame take(const GameEngine& engine)
{
    return { snapshotFrom(engine, Seat{1}), snapshotFrom(engine, std::nullopt) };
}

struct PinnedCapture : IGameObserver
{
    std::optional<Frame> atStart;
    std::optional<Frame> firstDealt;
    std::optional<Frame> midBidding;
    std::optional<Frame> midTrick;
    std::optional<Frame> trickWon;
    std::optional<Frame> lastScored;
    std::optional<Frame> gameOver;
    std::optional<GameSnapshot> foreheadDealt;

    void onGameStarted(const GameEngine& engine) override
    {
        atStart = take(engine);
    }

    void onRoundStarted(const GameEngine& engine) override
    {
        if(!firstDealt)
            firstDealt = take(engine);

        if(!foreheadDealt && engine.getCurrentRoundType() == RoundType::Forehead)
            foreheadDealt = snapshotFrom(engine, Seat{1});
    }

    void onBetPlaced(const GameEngine& engine, Seat, unsigned int) override
    {
        if(!midBidding && snapshotFrom(engine, std::nullopt).betsPlaced == 2)
            midBidding = take(engine);
    }

    void onCardPlayed(const GameEngine& engine, Seat, const Card&) override
    {
        if(!midTrick && engine.getCurrentTrick().getPlayedCards().size() == 2)
            midTrick = take(engine);
    }

    void onTrickWon(const GameEngine& engine, Seat winner, unsigned int) override
    {
        if(!trickWon && engine.getCurrentTrick().getPlayedCards().front().seat != winner)
            trickWon = take(engine);
    }

    void onRoundScored(const GameEngine& engine) override
    {
        if(engine.getCurrentRoundIndex() + 1 == engine.getRoundCount())
            lastScored = take(engine);
    }

    void onGameOver(const GameEngine& engine) override
    {
        gameOver = take(engine);
    }
};

// One seeded game, four seats, S_181, ending with a Forehead and a Hidden round, played once for
// every pinned test.
const PinnedCapture& pinnedGame()
{
    static const PinnedCapture capture = []
    {
        PinnedCapture played;
        playFullGame(GameStructure::S_181, buildRoundRobinProviders(4), 42, { &played }, true, false);
        return played;
    }();

    return capture;
}

const char* nameOf(GameStatus status)
{
    switch(status)
    {
        case GameStatus::NotStarted: return "NotStarted";
        case GameStatus::InProgress: return "InProgress";
        case GameStatus::Finished:   return "Finished";
        case GameStatus::Stopped:    return "Stopped";
    }

    return "?";
}

const char* nameOf(GamePhase phase)
{
    switch(phase)
    {
        case GamePhase::NotStarted:  return "NotStarted";
        case GamePhase::Betting:     return "Betting";
        case GamePhase::Playing:     return "Playing";
        case GamePhase::RoundScored: return "RoundScored";
        case GamePhase::GameOver:    return "GameOver";
    }

    return "?";
}

const char* nameOf(RoundType type)
{
    switch(type)
    {
        case RoundType::Normal:   return "Normal";
        case RoundType::Forehead: return "Forehead";
        case RoundType::Hidden:   return "Hidden";
    }

    return "?";
}

const char* nameOf(Suit suit)
{
    switch(suit)
    {
        case Suit::Hearts:   return "Hearts";
        case Suit::Diamonds: return "Diamonds";
        case Suit::Spades:   return "Spades";
        case Suit::Clubs:    return "Clubs";
    }

    return "?";
}

std::string text(std::optional<Seat> seat)
{
    return seat ? std::to_string(seat->index) : "-";
}

std::string text(std::optional<unsigned int> value)
{
    return value ? std::to_string(*value) : "-";
}

// Every field but the viewer, one line per part of the table, so a failing pin shows the whole
// frame next to what was expected.
std::string render(const GameSnapshot& snapshot)
{
    std::ostringstream out;

    out << nameOf(snapshot.status) << ", " << nameOf(snapshot.phase) << ", round "
        << snapshot.roundIndex << " of " << snapshot.roundCount << ", trick " << snapshot.trickNumber
        << " of " << snapshot.trickCount << ", " << nameOf(snapshot.roundType) << "\n";

    out << "trump " << (snapshot.trump ? snapshot.trump->toString() : "-")
        << ", round leader " << text(snapshot.roundLeader) << ", active " << text(snapshot.activeSeat)
        << ", forbidden " << text(snapshot.forbiddenBet) << ", bets " << snapshot.betsPlaced
        << " summing " << snapshot.betSum << "\n";

    out << "table";
    for(const PlayedCard& played : snapshot.table)
        out << " " << played.seat.index << ":" << played.card.toString();

    out << ", winning " << text(snapshot.winning) << ", led by " << text(snapshot.ledBy)
        << ", lead suit " << (snapshot.leadSuit ? nameOf(*snapshot.leadSuit) : "-") << "\n";

    for(const SeatSnapshot& seat : snapshot.seats)
    {
        out << seat.name << ": order " << seat.biddingOrder << ", bet " << text(seat.bet)
            << ", won " << seat.tricksWon << ", left " << seat.cardsLeft << ", scores "
            << seat.roundScore << "/" << seat.totalScore << ", streaks " << seat.winStreak << "/"
            << seat.lossStreak;

        if(seat.hand)
        {
            out << ", hand [";
            for(std::size_t i = 0 ; i < seat.hand->size() ; i++)
                out << (i == 0 ? "" : " ") << (*seat.hand)[i].toString();
            out << "]";
        }

        out << "\n";
    }

    return out.str();
}

// A spectator sees what seat 1 sees, without a single hand.
void requireSpectatorMatches(const Frame& frame)
{
    REQUIRE(indexOf(frame.seat1.viewer) == 1u);
    REQUIRE_FALSE(frame.spectator.viewer.has_value());

    GameSnapshot withoutHands = frame.seat1;
    for(SeatSnapshot& seat : withoutHands.seats)
        seat.hand.reset();

    for(const SeatSnapshot& seat : frame.spectator.seats)
        REQUIRE_FALSE(seat.hand.has_value());

    REQUIRE(render(frame.spectator) == render(withoutHands));
}
}

// The round-robin strategies are the ones GoldenGameTests pins on every CI toolchain. The first 24
// rounds are its 4-player S_181 game, so the totals going into the Forehead round are its final
// scores.

TEST_CASE("Pinned snapshot: before the first deal", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.atStart);
    requireSpectatorMatches(*capture.atStart);

    // Seat 1 may see its own hand, and has none yet: present and empty, not absent.
    REQUIRE(render(capture.atStart->seat1) ==
        "InProgress, NotStarted, round 0 of 26, trick 0 of 1, Normal\n"
        "trump -, round leader 0, active -, forbidden -, bets 0 summing 0\n"
        "table, winning -, led by 0, lead suit -\n"
        "P0: order 1, bet -, won 0, left 0, scores 0/0, streaks 0/0\n"
        "P1: order 2, bet -, won 0, left 0, scores 0/0, streaks 0/0, hand []\n"
        "P2: order 3, bet -, won 0, left 0, scores 0/0, streaks 0/0\n"
        "P3: order 4, bet -, won 0, left 0, scores 0/0, streaks 0/0\n");
}

TEST_CASE("Pinned snapshot: the first deal", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.firstDealt);
    requireSpectatorMatches(*capture.firstDealt);

    REQUIRE(render(capture.firstDealt->seat1) ==
        "InProgress, Betting, round 0 of 26, trick 0 of 1, Normal\n"
        "trump JH, round leader 0, active -, forbidden -, bets 0 summing 0\n"
        "table, winning -, led by 0, lead suit -\n"
        "P0: order 1, bet -, won 0, left 1, scores 0/0, streaks 0/0\n"
        "P1: order 2, bet -, won 0, left 1, scores 0/0, streaks 0/0, hand [7H]\n"
        "P2: order 3, bet -, won 0, left 1, scores 0/0, streaks 0/0\n"
        "P3: order 4, bet -, won 0, left 1, scores 0/0, streaks 0/0\n");
}

TEST_CASE("Pinned snapshot: mid-bidding", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.midBidding);
    requireSpectatorMatches(*capture.midBidding);

    // Inside onBetPlaced(), so the seat that just bid is still the active one.
    REQUIRE(render(capture.midBidding->seat1) ==
        "InProgress, Betting, round 0 of 26, trick 0 of 1, Normal\n"
        "trump JH, round leader 0, active 1, forbidden -, bets 2 summing 0\n"
        "table, winning -, led by 0, lead suit -\n"
        "P0: order 1, bet 0, won 0, left 1, scores 0/0, streaks 0/0\n"
        "P1: order 2, bet 0, won 0, left 1, scores 0/0, streaks 0/0, hand [7H]\n"
        "P2: order 3, bet -, won 0, left 1, scores 0/0, streaks 0/0\n"
        "P3: order 4, bet -, won 0, left 1, scores 0/0, streaks 0/0\n");
}

TEST_CASE("Pinned snapshot: mid-trick", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.midTrick);
    requireSpectatorMatches(*capture.midTrick);

    // Clubs led; the 7 of trumps is winning.
    REQUIRE(render(capture.midTrick->seat1) ==
        "InProgress, Playing, round 0 of 26, trick 1 of 1, Normal\n"
        "trump JH, round leader 0, active 1, forbidden -, bets 4 summing 0\n"
        "table 0:9C 1:7H, winning 1, led by 0, lead suit Clubs\n"
        "P0: order 1, bet 0, won 0, left 0, scores 0/0, streaks 0/0\n"
        "P1: order 2, bet 0, won 0, left 0, scores 0/0, streaks 0/0, hand []\n"
        "P2: order 3, bet 0, won 0, left 1, scores 0/0, streaks 0/0\n"
        "P3: order 4, bet 0, won 0, left 1, scores 0/0, streaks 0/0\n");
}

TEST_CASE("Pinned snapshot: inside onTrickWon", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.trickWon);
    requireSpectatorMatches(*capture.trickWon);

    // The finished trick is still on the table, already counted for seat 3, and led by seat 0
    // although getTrickLeaderSeat() has moved to the winner.
    REQUIRE(render(capture.trickWon->seat1) ==
        "InProgress, Playing, round 0 of 26, trick 1 of 1, Normal\n"
        "trump JH, round leader 0, active -, forbidden -, bets 4 summing 0\n"
        "table 0:9C 1:7H 2:10D 3:AH, winning 3, led by 0, lead suit Clubs\n"
        "P0: order 1, bet 0, won 0, left 0, scores 0/0, streaks 0/0\n"
        "P1: order 2, bet 0, won 0, left 0, scores 0/0, streaks 0/0, hand []\n"
        "P2: order 3, bet 0, won 0, left 0, scores 0/0, streaks 0/0\n"
        "P3: order 4, bet 0, won 1, left 0, scores 0/0, streaks 0/0\n");
}

TEST_CASE("Pinned snapshot: a Forehead deal", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.foreheadDealt);

    // Seat 1 sees every card but its own.
    REQUIRE(render(*capture.foreheadDealt) ==
        "InProgress, Betting, round 24 of 26, trick 0 of 1, Forehead\n"
        "trump JH, round leader 0, active -, forbidden -, bets 0 summing 0\n"
        "table, winning -, led by 0, lead suit -\n"
        "P0: order 1, bet -, won 0, left 1, scores 0/39, streaks 1/0, hand [10C]\n"
        "P1: order 2, bet -, won 0, left 1, scores 0/56, streaks 3/0\n"
        "P2: order 3, bet -, won 0, left 1, scores 0/10, streaks 0/2, hand [QH]\n"
        "P3: order 4, bet -, won 0, left 1, scores 0/16, streaks 2/0, hand [8H]\n");
}

TEST_CASE("Pinned snapshot: the last round scored", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.lastScored);
    requireSpectatorMatches(*capture.lastScored);

    // Scored, not committed: the round scores are not in the totals yet. Hidden, so seat 1 sees
    // no hand, not even its own empty one.
    REQUIRE(render(capture.lastScored->seat1) ==
        "InProgress, RoundScored, round 25 of 26, trick 1 of 1, Hidden\n"
        "trump 9H, round leader 1, active -, forbidden -, bets 4 summing 0\n"
        "table 1:JD 2:QD 3:KH 0:9C, winning 3, led by 1, lead suit Diamonds\n"
        "P0: order 4, bet 0, won 0, left 0, scores 5/44, streaks 1/0\n"
        "P1: order 1, bet 0, won 0, left 0, scores 5/61, streaks 3/0\n"
        "P2: order 2, bet 0, won 0, left 0, scores 5/9, streaks 0/2\n"
        "P3: order 3, bet 0, won 1, left 0, scores -1/21, streaks 2/0\n");
}

TEST_CASE("Pinned snapshot: game over", "[snapshot]")
{
    const PinnedCapture& capture = pinnedGame();
    REQUIRE(capture.gameOver);
    requireSpectatorMatches(*capture.gameOver);

    REQUIRE(render(capture.gameOver->seat1) ==
        "Finished, GameOver, round 25 of 26, trick 1 of 1, Hidden\n"
        "trump 9H, round leader 1, active -, forbidden -, bets 4 summing 0\n"
        "table 1:JD 2:QD 3:KH 0:9C, winning 3, led by 1, lead suit Diamonds\n"
        "P0: order 4, bet 0, won 0, left 0, scores 0/49, streaks 1/0\n"
        "P1: order 1, bet 0, won 0, left 0, scores 0/66, streaks 3/0\n"
        "P2: order 2, bet 0, won 0, left 0, scores 0/14, streaks 0/2\n"
        "P3: order 3, bet 0, won 1, left 0, scores 0/20, streaks 2/0\n");
}

TEST_CASE("No snapshot holds a hand its viewer may not see", "[snapshot][property]")
{
    constexpr std::uint32_t kSeedsPerShape = 4;

    for(unsigned int playerCount = 2 ; playerCount <= 6 ; playerCount++)
    {
        for(const GameStructure structure : { GameStructure::S_181, GameStructure::S_818 })
        {
            for(const bool allOneCardForehead : { false, true })
            {
                for(std::uint32_t seed = 0 ; seed < kSeedsPerShape ; seed++)
                {
                    INFO("players = " << playerCount << ", S_818 = " << (structure == GameStructure::S_818)
                         << ", all one-card rounds Forehead = " << allOneCardForehead << ", seed = " << seed);

                    LeakChecker checker;

                    const auto engine = playFullGame(structure, randomProviders(playerCount, seed), seed,
                                                     { &checker }, true, allOneCardForehead);

                    REQUIRE(engine->getStatus() == GameStatus::Finished);
                    REQUIRE(checker.sawForehead);
                    REQUIRE(checker.sawHidden);
                    REQUIRE(checker.snapshotsChecked > 0);
                }
            }
        }
    }
}

TEST_CASE("ledBy names who led the trick, never who won it", "[snapshot]")
{
    for(unsigned int playerCount = 2 ; playerCount <= 6 ; playerCount++)
    {
        INFO("players = " << playerCount);

        LedByChecker checker;

        playFullGame(GameStructure::S_181, randomProviders(playerCount, 7), 7, { &checker }, true, false);

        // Without such a trick, a ledBy read from getTrickLeaderSeat() would pass.
        REQUIRE(checker.tricksWonByAnotherSeat > 0);
    }
}

TEST_CASE("snapshotFrom refuses a game that has not started", "[snapshot]")
{
    const GameEngine engine;

    REQUIRE_THROWS_AS(snapshotFrom(engine, std::nullopt), std::logic_error);
    REQUIRE_THROWS_AS(snapshotFrom(engine, Seat{0}), std::logic_error);
}

TEST_CASE("snapshotFrom refuses a viewer who is not at the table", "[snapshot]")
{
    GameEngine engine;
    engine.start(buildSetup(GameStructure::S_181, buildRoundRobinProviders(4), 42));

    REQUIRE_THROWS_AS(snapshotFrom(engine, Seat{4}), std::out_of_range);
    REQUIRE_NOTHROW(snapshotFrom(engine, Seat{3}));
}

TEST_CASE("A snapshot says who it was built for", "[snapshot]")
{
    GameEngine engine;
    engine.start(buildSetup(GameStructure::S_181, buildRoundRobinProviders(3), 42));

    REQUIRE(indexOf(snapshotFrom(engine, Seat{2}).viewer) == 2u);
    REQUIRE_FALSE(snapshotFrom(engine, std::nullopt).viewer.has_value());
}
