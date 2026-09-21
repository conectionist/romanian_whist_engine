#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <array>
#include <cstdint>
#include <memory>
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
    // bidding averages higher"; it does not at five and six players, and that was
    // reported rather than tuned away.
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
    //        2      24.6     20.7     +3.9
    //        3      52.8     47.1     +5.7
    //        4      67.9     67.6     +0.3
    //        5      82.9     86.2     -3.3
    //        6      99.6    104.0     -4.5
    constexpr std::array<Expected, 5> expected{{
        {2, 3942, 3316},
        {3, 12666, 11292},
        {4, 21730, 21620},
        {5, 33158, 34465},
        {6, 47792, 49933},
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
    // is the default. The one remaining loss, at five players, did not reproduce
    // on two other seed ranges (+1.5 and -0.8). See
    // docs/cyborg-implementation-plan.md.
    //
    // Moved by: card play. Also by bidding, since both arms bid by section 4.
    //
    // Seeds 30000..30039. Average points per game when pinned:
    //
    //     players     A        B      A - B
    //        2      31.3     24.6     +6.7
    //        3      51.9     50.3     +1.6
    //        4      71.5     68.3     +3.1
    //        5      81.2     82.9     -1.7
    //        6     101.2    100.7     +0.4
    constexpr std::array<Expected, 5> expected{{
        {2, 5002, 3938},
        {3, 12451, 12068},
        {4, 22865, 21862},
        {5, 32475, 33153},
        {6, 48554, 48347},
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
    // This is the input to the bidding re-calibration decision, which is taken
    // separately from card play. At five and six players heuristic bidding still
    // wins.
    //
    // Moved by: bidding, and by card play.
    //
    // Seeds 30000..30039. Average points per game when pinned:
    //
    //     players     A        B      A - B
    //        2      29.8     14.4    +15.4
    //        3      54.6     46.0     +8.6
    //        4      72.3     68.3     +3.9
    //        5      82.3     83.5     -1.2
    //        6      99.4    102.8     -3.5
    constexpr std::array<Expected, 5> expected{{
        {2, 4772, 2300},
        {3, 13107, 11049},
        {4, 23127, 21867},
        {5, 32911, 33391},
        {6, 47696, 49362},
    }};

    checkPins(knobsFor(false, false), knobsFor(true, false), expected);
}
