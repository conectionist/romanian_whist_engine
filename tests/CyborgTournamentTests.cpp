#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using namespace romanian_whist;
using namespace romanian_whist::test;

// Three seat-balanced A/B matches, each pinned exactly per table size. They are
// PINS, NOT GATES: none of them asserts that A wins. Each pins a number that a
// named kind of change should move, so that any change which moves one is read
// and re-pinned on purpose rather than slipping through.
//
// Every arm spells out BOTH decision knobs. The defaults are allowed to change,
// and a pin that leant on one would then move for no reason connected to what it
// measures.
//
// No WHIST_PIN_FLOAT_GOLDENS gate: LowRisk has no floats, and Cyborg's odds use
// only basic arithmetic - which rounds identically everywhere ONLY because the
// engine is built with -ffp-contract=off (see CMakeLists.txt). Without it,
// macOS/arm64 fuses multiply-adds, section 6 play breaks a few ties the other
// way, and these totals move. If one diverges again, suspect contraction before
// reaching for a gate.

namespace
{
cyborg::CyborgKnobs knobsFor(bool heuristicBid, bool heuristicPlay)
{
    cyborg::CyborgKnobs knobs{};
    knobs.useHeuristicBid = heuristicBid;
    knobs.useHeuristicPlay = heuristicPlay;
    return knobs;
}

struct MatchResult
{
    long totalA = 0;
    long totalB = 0;

    // The LowRisk seats filling the rest of the table, for an absolute read
    // alongside the relative one. Empty at two players, where A and B are the
    // whole table.
    long lowRiskTotal = 0;
    unsigned int lowRiskSeatGames = 0;

    std::size_t fallbacks = 0;
    unsigned int games = 0;
};

// Two Cyborgs, A and B, at a table filled with LowRisk. Every deal is played once
// for each pair of adjacent seats A and B can take, and again with A and B
// swapped, so neither seat position nor bidding order decides the result.
MatchResult playAbMatch(const cyborg::CyborgKnobs& knobsA, const cyborg::CyborgKnobs& knobsB,
                        unsigned int playerCount, unsigned int seeds, std::uint32_t firstSeed)
{
    MatchResult result;

    for(unsigned int seed = 0; seed < seeds; ++seed)
    {
        for(unsigned int shift = 0; shift < playerCount; ++shift)
        {
            for(const bool swapped : {false, true})
            {
                const unsigned int left = shift;
                const unsigned int right = (shift + 1) % playerCount;
                const unsigned int seatA = swapped ? right : left;
                const unsigned int seatB = swapped ? left : right;

                GameEngine engine;
                GameSetup setup;

                // Held so the fallback counters can be read after the game;
                // SeatSetup owns the providers, not these pointers.
                CyborgStrategy* cyborgA = nullptr;
                CyborgStrategy* cyborgB = nullptr;

                for(unsigned int i = 0; i < playerCount; ++i)
                {
                    const std::string name = "P" + std::to_string(i);

                    if(i != seatA && i != seatB)
                    {
                        setup.seats.push_back(
                            {name, std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
                        continue;
                    }

                    auto strategy = std::make_unique<CyborgStrategy>(i == seatA ? knobsA : knobsB);
                    (i == seatA ? cyborgA : cyborgB) = strategy.get();
                    strategy->setPlayerName(name);
                    engine.addObserver(strategy.get());
                    setup.seats.push_back(SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))});
                }

                setup.shuffleSeed = firstSeed + seed;
                engine.start(std::move(setup));
                engine.run();

                const auto scores = finalScores(engine);
                result.totalA += scores[seatA];
                result.totalB += scores[seatB];

                for(unsigned int i = 0; i < playerCount; ++i)
                {
                    if(i != seatA && i != seatB)
                    {
                        result.lowRiskTotal += scores[i];
                        ++result.lowRiskSeatGames;
                    }
                }

                result.fallbacks += cyborgA->getFallbacksTaken() + cyborgB->getFallbacksTaken();
                ++result.games;
            }
        }
    }

    return result;
}

struct Expected
{
    unsigned int players;
    long totalA;
    long totalB;
};

void checkPins(const cyborg::CyborgKnobs& knobsA, const cyborg::CyborgKnobs& knobsB,
               const std::array<Expected, 5>& expected)
{
    for(const Expected& e : expected)
    {
        const MatchResult result = playAbMatch(knobsA, knobsB, e.players, 40, 30000);

        const unsigned int players = e.players;
        const unsigned int games = result.games;
        const long totalA = result.totalA;
        const long totalB = result.totalB;
        const long lowRiskTotal = result.lowRiskTotal;
        const unsigned int lowRiskSeatGames = result.lowRiskSeatGames;
        CAPTURE(players, games, totalA, totalB, lowRiskTotal, lowRiskSeatGames);

        CHECK(result.fallbacks == 0);
        CHECK(totalA == e.totalA);
        CHECK(totalB == e.totalB);
    }
}
} // namespace

TEST_CASE("Cyborg tournament: section 4 bidding against heuristic bidding, pinned per table size",
          "[cyborg][tournament]")
{
    // Phase 2's measurement: section 4 bidding against heuristic bidding, with
    // HEURISTIC play on both sides. The plan's gate for Phase 2 was "section 4
    // bidding averages higher". As first written it did not at five and six
    // players, which was reported rather than tuned away; since section 4.4 bids
    // by expected score (after Phase 3) it does at every table size.
    //
    // Moved by: a bidding change. NOT moved by card play - both arms set
    // useHeuristicPlay = true, so neither seat reaches cyborg::choosePlay. If a
    // change confined to card play moves these, something leaked into a helper
    // the heuristics also reach, and that is the thing to find.
    //
    // Seeds 30000..30039. Average points per game when pinned
    // (A = section 4 bidding, B = heuristic bidding):
    //
    //     players     A        B      A - B
    //        2      29.9     19.4    +10.5
    //        3      54.9     46.2     +8.8
    //        4      73.0     67.2     +5.9
    //        5      90.9     85.3     +5.6
    //        6     108.0    102.9     +5.1
    constexpr std::array<Expected, 5> expected{{
        {2, 4790, 3106},
        {3, 13184, 11080},
        {4, 23366, 21491},
        {5, 36376, 34137},
        {6, 51816, 49386},
    }};

    checkPins(knobsFor(false, true), knobsFor(true, true), expected);
}

TEST_CASE("Cyborg tournament: section 6 play against heuristic play, pinned per table size",
          "[cyborg][tournament]")
{
    // What Phase 3's card play is worth. Both arms bid by section 4, so the play
    // is the only difference: A = section 6 play, B = heuristic play.
    //
    // History: as first landed (Phase 3b) section 6 play lost at three to six
    // players, almost all of it in no-trump rounds, where every card is dealt and
    // the SHED and BALANCE leads chose by the duck term. Phase 3c made the
    // no-trump BALANCE lead cash its certainties and judged SHED on certainties
    // without trumps. These numbers measure that, and they are why section 6 play
    // is the default. Re-pinned after section 4.4's expected-score bid, which both
    // arms use, and after Phase 4's BALANCE lead change; the small five-player
    // loss is still there. See docs/cyborg-implementation-plan.md.
    //
    // Moved by: card play. Also by bidding, since both arms bid by section 4.
    //
    // Seeds 30000..30039. Average points per game when pinned:
    //
    //     players     A        B      A - B
    //        2      32.8     21.6    +11.2
    //        3      56.0     52.0     +4.0
    //        4      76.3     72.6     +3.7
    //        5      89.8     90.3     -0.5
    //        6     109.0    108.8     +0.1
    constexpr std::array<Expected, 5> expected{{
        {2, 5248, 3454},
        {3, 13448, 12478},
        {4, 24415, 23236},
        {5, 35903, 36103},
        {6, 52296, 52245},
    }};

    checkPins(knobsFor(false, false), knobsFor(false, true), expected);
}

TEST_CASE("Cyborg tournament: section 4 bidding against heuristic bidding under section 6 play",
          "[cyborg][tournament]")
{
    // Section 4 bidding against heuristic bidding with SECTION 6 play on both
    // sides - the question Phase 2 could not answer, because heuristic play
    // rewards bidding low. A = section 4 bidding, B = heuristic bidding.
    //
    // This was the input to the bidding re-calibration after Phase 3: section 4
    // lost at five and six players (-1.2, -3.5), all of it in 3-7 trick rounds,
    // where it bid a hand's average and hit less often than the heuristic's low
    // bids. Section 4.4 now bids the count with the highest expected score, and
    // wins at every table size.
    //
    // Moved by: bidding, and by card play.
    //
    // Seeds 30000..30039. Average points per game when pinned:
    //
    //     players     A        B      A - B
    //        2      33.0     12.5    +20.5
    //        3      55.8     45.2    +10.6
    //        4      75.5     68.1     +7.4
    //        5      90.6     83.3     +7.3
    //        6     108.5    103.5     +5.1
    constexpr std::array<Expected, 5> expected{{
        {2, 5276, 1994},
        {3, 13390, 10838},
        {4, 24163, 21801},
        {5, 36242, 33305},
        {6, 52097, 49664},
    }};

    checkPins(knobsFor(false, false), knobsFor(true, false), expected);
}

// ---- The Phase 4 bar ------------------------------------------------------------
//
// The A/B pins above catch a rule change; these catch the strategy getting WORSE.
// They assert standings with margins rather than exact totals, so a deliberate
// improvement passes them untouched.

namespace
{
// Mean time per Cyborg decision, in microseconds. See the first bar test.
constexpr double kMicrosPerDecisionBound = 200.0;

// A Cyborg that times its own decisions, for the latency bound.
struct TimedCyborg : CyborgStrategy
{
    using CyborgStrategy::CyborgStrategy;

    std::chrono::nanoseconds spent{0};
    std::size_t decisions = 0;

    unsigned int getBestBet(const BetContext& context) override
    {
        const auto start = std::chrono::steady_clock::now();
        const unsigned int bid = CyborgStrategy::getBestBet(context);
        spent += std::chrono::steady_clock::now() - start;
        ++decisions;
        return bid;
    }

    std::optional<Card> getBestChoice(const PlayContext& context) override
    {
        const auto start = std::chrono::steady_clock::now();
        const std::optional<Card> card = CyborgStrategy::getBestChoice(context);
        spent += std::chrono::steady_clock::now() - start;
        ++decisions;
        return card;
    }
};

struct TableResult
{
    double cyborgAvg = 0.0;
    double lowRiskAvg = 0.0;
    std::size_t fallbacks = 0;
    std::chrono::nanoseconds spent{0};
    std::size_t decisions = 0;
};

// One Cyborg with today's defaults among LowRisk players, in every seat in turn.
TableResult playCyborgAmongLowRisk(unsigned int playerCount, unsigned int seeds, std::uint32_t firstSeed,
                                   GameStructure structure = GameStructure::S_181)
{
    long cyborgTotal = 0;
    long lowRiskTotal = 0;
    unsigned int games = 0;
    unsigned int lowRiskSeatGames = 0;
    TableResult result;

    for(unsigned int seed = 0; seed < seeds; ++seed)
    {
        for(unsigned int cyborgSeat = 0; cyborgSeat < playerCount; ++cyborgSeat)
        {
            GameEngine engine;
            GameSetup setup;
            setup.structure = structure;
            TimedCyborg* cyborg = nullptr;

            for(unsigned int i = 0; i < playerCount; ++i)
            {
                const std::string name = "P" + std::to_string(i);

                if(i != cyborgSeat)
                {
                    setup.seats.push_back(
                        {name, std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
                    continue;
                }

                auto strategy = std::make_unique<TimedCyborg>();
                cyborg = strategy.get();
                strategy->setPlayerName(name);
                engine.addObserver(strategy.get());
                setup.seats.push_back(SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))});
            }

            setup.shuffleSeed = firstSeed + seed;
            engine.start(std::move(setup));
            engine.run();

            const auto scores = finalScores(engine);
            ++games;
            cyborgTotal += scores[cyborgSeat];

            for(unsigned int i = 0; i < playerCount; ++i)
            {
                if(i != cyborgSeat)
                {
                    lowRiskTotal += scores[i];
                    ++lowRiskSeatGames;
                }
            }

            result.fallbacks += cyborg->getFallbacksTaken();
            result.spent += cyborg->spent;
            result.decisions += cyborg->decisions;
        }
    }

    result.cyborgAvg = static_cast<double>(cyborgTotal) / games;
    result.lowRiskAvg = static_cast<double>(lowRiskTotal) / lowRiskSeatGames;
    return result;
}
} // namespace

TEST_CASE("Cyborg tournament: one Cyborg beats three LowRisk by a clear margin, within its time budget",
          "[cyborg][tournament]")
{
    // Seeds 1000..1019, the Cyborg in every seat: 80 games. Measured when this was
    // written: Cyborg 75.3, LowRisk 60.9 points a game - a gap of 14.4. The margin
    // is set at about half of it: a real slide fails, a deliberate change that
    // costs a point or two does not. A bare `>` would not catch Cyborg falling
    // from +14 to +1.
    constexpr double kMargin = 7.0;

    const TableResult result = playCyborgAmongLowRisk(4, 20, 1000);

    const double cyborgAvg = result.cyborgAvg;
    const double lowRiskAvg = result.lowRiskAvg;
    CAPTURE(cyborgAvg, lowRiskAvg);

    CHECK(result.fallbacks == 0);
    REQUIRE(cyborgAvg > lowRiskAvg + kMargin);

    // Latency. A decision takes about half a microsecond on Release, and a few on
    // Debug; this bound is far above both, so it cannot flake on a slow runner,
    // yet an accidental enumeration (design section 6.5 is thousands of deals a
    // decision) would blow straight through it.
    const double microsPerDecision =
        std::chrono::duration<double, std::micro>(result.spent).count() / static_cast<double>(result.decisions);
    CAPTURE(microsPerDecision);
    REQUIRE(microsPerDecision < kMicrosPerDecisionBound);
}

TEST_CASE("Cyborg tournament: Reckoner ahead of Cyborg ahead of LowRisk", "[cyborg][tournament]")
{
    constexpr unsigned int kGames = 15;
    long cyborgTotal = 0;
    long reckonerTotal = 0;
    long lowRiskTotal = 0;

    for(unsigned int g = 0; g < kGames; ++g)
    {
        GameEngine engine;
        GameSetup setup;

        const unsigned int cyborgSeat = g % 4;
        const unsigned int reckonerSeat = (g + 2) % 4;

        for(unsigned int i = 0; i < 4; ++i)
        {
            const std::string name = "P" + std::to_string(i);

            if(i == cyborgSeat)
                setup.seats.push_back(makeCyborgSeat(name, engine));
            else if(i == reckonerSeat)
                setup.seats.push_back(makeReckonerSeat(name, engine, reckoner::ReckonerKnobs::beta(), 2000 + g));
            else
                setup.seats.push_back(
                    {name, std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        }

        setup.shuffleSeed = 2000 + g;
        engine.start(std::move(setup));
        engine.run();

        const auto scores = finalScores(engine);
        cyborgTotal += scores[cyborgSeat];
        reckonerTotal += scores[reckonerSeat];
        for(unsigned int i = 0; i < 4; ++i)
        {
            if(i != cyborgSeat && i != reckonerSeat)
                lowRiskTotal += scores[i];
        }
    }

    const double cyborgAvg = static_cast<double>(cyborgTotal) / kGames;
    const double reckonerAvg = static_cast<double>(reckonerTotal) / kGames;
    const double lowRiskAvg = static_cast<double>(lowRiskTotal) / (2.0 * kGames);
    CAPTURE(reckonerAvg, cyborgAvg, lowRiskAvg);

    // The design's bar: beat LowRisk comfortably, lose to the Reckoner. Measured
    // when this was written: Reckoner 87.5, Cyborg 72.7, LowRisk 60.2 points a
    // game. Orderings, not exact scores - the Reckoner decides by argmax over
    // std::exp/std::pow, which differ across libm, so its totals are not
    // portable (the same reason its own score pin is WHIST_PIN_FLOAT_GOLDENS-only).
    // The gaps are wide enough that a platform's tie-breaks cannot reorder them.
    //
    // If a change ever puts Cyborg ahead of the Reckoner, suspect a bug in one of
    // them before celebrating.
    REQUIRE(reckonerAvg > cyborgAvg);
    REQUIRE(cyborgAvg > lowRiskAvg);
}

TEST_CASE("Cyborg tournament: Cyborg beats LowRisk at every table size and in S_818", "[cyborg][tournament]")
{
    // The weaker bar across the other table sizes, five seeds each with the Cyborg
    // in every seat, plus one run in the S_818 structure. Measured when this was
    // written, points a game ahead of LowRisk: +16.2, +10.9, +13.1, +9.4 at 2, 3,
    // 5 and 6 players, and +3.7 in S_818 at four.
    for(const unsigned int playerCount : {2u, 3u, 5u, 6u})
    {
        const TableResult result = playCyborgAmongLowRisk(playerCount, 5, 3000);

        const double cyborgAvg = result.cyborgAvg;
        const double lowRiskAvg = result.lowRiskAvg;
        CAPTURE(playerCount, cyborgAvg, lowRiskAvg);

        CHECK(result.fallbacks == 0);
        CHECK(cyborgAvg > lowRiskAvg);
    }

    const TableResult s818 = playCyborgAmongLowRisk(4, 5, 3000, GameStructure::S_818);

    const double cyborgAvg = s818.cyborgAvg;
    const double lowRiskAvg = s818.lowRiskAvg;
    CAPTURE(cyborgAvg, lowRiskAvg);

    CHECK(s818.fallbacks == 0);
    CHECK(cyborgAvg > lowRiskAvg);
}
