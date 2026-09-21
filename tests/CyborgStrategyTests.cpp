#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <string>
#include <utility>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

// Cyborg plays exactly the game LowRisk would.
//
// Today that is nearly the whole of what the class does, but this test does not
// become obsolete when the Cyborg rules land on top. It pins two things:
//
//   (a) that the WIRING changes nothing - registering an observer, resolving a
//       seat by name, keeping a round memory and filtering answers through the
//       card validator leave the deal, the bidding order and every trick exactly
//       as they were; and
//   (b) that the heuristic answers reproduce LowRiskStrategy exactly.
//
// Since Phase 3c made section 4 bidding and section 6 play the defaults, (b) pins
// the FALLBACK rather than the shipping path - the answer both decisions give
// when the memory disagrees with the position. That is still worth pinning. (a)
// never decays at all. Do not delete this on the grounds that Cyborg has moved on.
//
// The comparison is exact rather than statistical because neither strategy
// consumes randomness and neither touches the engine's generator: the deal
// depends on the shuffle seed alone.

namespace
{
// finalScores and RoundRecorder between them cannot tell apart two different
// cards that win the same number of tricks, so parity needs a third oracle that
// watches every card go down.
struct PlayRecorder : IGameObserver
{
    std::vector<std::pair<unsigned int, Card>> plays;

    void onCardPlayed(const GameEngine&, Seat seat, const Card& card) override
    {
        plays.emplace_back(seat.index, card);
    }
};

struct GameTrace
{
    std::vector<int> scores;
    RoundRecord rounds;
    std::vector<std::pair<unsigned int, Card>> plays;
};

bool operator==(const GameTrace& a, const GameTrace& b)
{
    return a.scores == b.scores && a.rounds == b.rounds && a.plays == b.plays;
}

// The control arm: the round-robin line-up, untouched. buildRoundRobinProviders
// already puts a LowRiskStrategy at seat 1 for every player count, so the seat
// being compared needs no special construction.
GameTrace playWithLowRisk(GameStructure structure, unsigned int playerCount, std::uint32_t seed,
                          bool endWithForeheadAndHidden = false, bool all1Forehead = false)
{
    RoundRecorder rounds;
    PlayRecorder plays;

    const auto engine = playFullGame(structure, buildRoundRobinProviders(playerCount), seed,
                                     { &rounds, &plays }, endWithForeheadAndHidden, all1Forehead);

    return GameTrace{ finalScores(*engine), rounds.record, plays.plays };
}

// The treatment arm: the same table with one seat's provider swapped for a
// Cyborg of the same name. buildSetup names seats "P0".."Pn", so matching that
// keeps the two tables identical in every respect the engine can see.
GameTrace playWithCyborg(GameStructure structure, unsigned int playerCount, std::uint32_t seed,
                         unsigned int seatIndex, bool endWithForeheadAndHidden = false,
                         bool all1Forehead = false, std::size_t* fallbacksOut = nullptr)
{
    RoundRecorder rounds;
    PlayRecorder plays;

    GameEngine engine;
    engine.addObserver(&rounds);
    engine.addObserver(&plays);

    GameSetup setup = buildSetup(structure, buildRoundRobinProviders(playerCount), seed,
                                 endWithForeheadAndHidden, all1Forehead);

    const std::string name = "P" + std::to_string(seatIndex);

    // Held so the fallback counter can be read after the game; SeatSetup takes
    // ownership of the provider, not of this pointer.
    auto strategy = std::make_unique<CyborgStrategy>(cyborg::CyborgKnobs::parityWithLowRisk());
    CyborgStrategy* observer = strategy.get();
    strategy->setPlayerName(name);
    engine.addObserver(observer);

    setup.seats[seatIndex] = SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))};

    engine.start(std::move(setup));
    engine.run();

    if(fallbacksOut)
        *fallbacksOut = observer->getFallbacksTaken();

    return GameTrace{ finalScores(engine), rounds.record, plays.plays };
}
} // namespace

TEST_CASE("CyborgStrategy: plays LowRisk's game exactly, at every table size", "[cyborg]")
{
    constexpr std::uint32_t seed = 42;

    for(unsigned int n = 2 ; n <= 6 ; n++)
    {
        for(const GameStructure structure : { GameStructure::S_181, GameStructure::S_818 })
        {
            INFO(n << " players, structure " << (structure == GameStructure::S_181 ? "1-8-1" : "8-1-8"));

            // Seat 1 is the round-robin's LowRisk seat at every player count.
            REQUIRE(playWithCyborg(structure, n, seed, 1) == playWithLowRisk(structure, n, seed));
        }
    }
}

TEST_CASE("CyborgStrategy: resolves its own seat wherever it sits", "[cyborg]")
{
    // Seat resolution is by NAME, so a bug that only shows away from seat 0 -
    // or away from the round opener - would hide in the matrix above, which
    // always uses seat 1.
    constexpr std::uint32_t seed = 4242;
    constexpr unsigned int n = 4;

    // The control arm's seats 0 and 3 are not LowRisk, so a Cyborg there will
    // NOT match the control. What must hold is that it plays a complete, legal
    // game and knows which seat it is - checked by the game finishing and by the
    // fallback counter staying at zero, which is only possible if the memory
    // agreed with the position on every single decision.
    for(const unsigned int seat : { 0u, 3u })
    {
        INFO("seat " << seat);

        std::size_t fallbacks = 1;
        const GameTrace trace = playWithCyborg(GameStructure::S_181, n, seed, seat, false, false,
                                               &fallbacks);

        REQUIRE(trace.scores.size() == n);
        REQUIRE(fallbacks == 0);
    }
}

TEST_CASE("CyborgStrategy: parity holds through the blind rounds too", "[cyborg]")
{
    // Forehead and Hidden bar the bidder from reading its own hand. No existing
    // Reckoner test exercises either flag.
    constexpr std::uint32_t seed = 77;
    constexpr unsigned int n = 4;

    SECTION("a game ending with one Forehead and one Hidden round")
    {
        REQUIRE(playWithCyborg(GameStructure::S_181, n, seed, 1, true, false) ==
                playWithLowRisk(GameStructure::S_181, n, seed, true, false));
    }

    SECTION("every one-card round played Forehead")
    {
        REQUIRE(playWithCyborg(GameStructure::S_181, n, seed, 1, false, true) ==
                playWithLowRisk(GameStructure::S_181, n, seed, false, true));
    }
}

TEST_CASE("CyborgStrategy: the memory agrees with the table on every decision", "[cyborg]")
{
    // The counterpart to the robustness suite, which proves the fallback WORKS.
    // This proves it is never needed in a healthy game - otherwise every parity
    // assertion above could be passing for the wrong reason, with a permanently
    // disagreeing memory quietly delegating each decision to the heuristics.
    constexpr std::uint32_t seed = 42;

    for(unsigned int n = 2 ; n <= 6 ; n++)
    {
        INFO(n << " players");

        std::size_t fallbacks = 1;
        playWithCyborg(GameStructure::S_181, n, seed, 1, false, false, &fallbacks);

        REQUIRE(fallbacks == 0);
    }
}

TEST_CASE("CyborgStrategy: the same seed plays the same game twice", "[cyborg]")
{
    // Exact, not statistical: Cyborg holds no RNG at all, which is why its seat
    // factory takes no seed argument where makeReckonerSeat does.
    REQUIRE(playWithCyborg(GameStructure::S_818, 4, 31337, 1) ==
            playWithCyborg(GameStructure::S_818, 4, 31337, 1));
}

TEST_CASE("CyborgStrategy: two Cyborgs at one table each find their own seat", "[cyborg]")
{
    GameEngine engine;
    GameSetup setup;

    setup.seats.push_back(makeCyborgSeat("CyborgA", engine));
    setup.seats.push_back(makeCyborgSeat("CyborgB", engine));
    setup.seats.push_back({"LowRisk", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
    setup.shuffleSeed = 5150;

    engine.start(std::move(setup));

    REQUIRE_NOTHROW(engine.run());
    REQUIRE(engine.getStatus() == GameStatus::Finished);
}

TEST_CASE("CyborgStrategy: a two-trick lead planned while bidding lasts until the next round", "[cyborg]")
{
    // Section 4.3: leading a two-trick round, the bid and the opening lead are one
    // decision, so the lead has to be kept for card play - and must not leak into
    // a round it was not made for.
    CyborgStrategy strategy; // default knobs: section 4 bidding gets first refusal

    const Card turnUp{Rank::Seven, Suit::Diamonds};
    strategy.onRoundStart(4, 2, turnUp, 0, 0);

    const std::vector<Card> hand{Card{Rank::Queen, Suit::Diamonds}, Card{Rank::King, Suit::Diamonds}};
    const BetContext context{hand, turnUp, true, std::nullopt, RoundType::Normal};

    // Two big trumps: bid 2, lead the higher.
    REQUIRE(strategy.getBestBet(context) == 2);
    REQUIRE(strategy.getPlannedLead() == std::optional<Card>{Card{Rank::King, Suit::Diamonds}});
    REQUIRE(strategy.getFallbacksTaken() == 0);

    strategy.onRoundStart(4, 3, turnUp, 1, 0);
    REQUIRE_FALSE(strategy.getPlannedLead().has_value());
}
