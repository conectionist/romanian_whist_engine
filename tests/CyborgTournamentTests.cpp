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

namespace
{
struct MatchResult
{
    long sectionFourTotal = 0;
    long heuristicTotal = 0;
    std::size_t fallbacks = 0;
    unsigned int games = 0;
};

// A: a Cyborg bidding by section 4. B: a Cyborg bidding by the heuristic. Both
// play heuristically, at a table filled with LowRisk. Every deal is played once
// for each pair of adjacent seats A and B can take, and again with A and B
// swapped, so neither seat position nor bidding order decides the result.
MatchResult playAbMatch(unsigned int playerCount, unsigned int seeds, std::uint32_t firstSeed)
{
    cyborg::CyborgKnobs sectionFour{};
    sectionFour.useHeuristicBid = false;
    sectionFour.useHeuristicPlay = true;

    cyborg::CyborgKnobs heuristic = sectionFour;
    heuristic.useHeuristicBid = true;

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

                    auto strategy = std::make_unique<CyborgStrategy>(i == seatA ? sectionFour : heuristic);
                    (i == seatA ? cyborgA : cyborgB) = strategy.get();
                    strategy->setPlayerName(name);
                    engine.addObserver(strategy.get());
                    setup.seats.push_back(SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))});
                }

                setup.shuffleSeed = firstSeed + seed;
                engine.start(std::move(setup));
                engine.run();

                const auto scores = finalScores(engine);
                result.sectionFourTotal += scores[seatA];
                result.heuristicTotal += scores[seatB];
                result.fallbacks += cyborgA->getFallbacksTaken() + cyborgB->getFallbacksTaken();
                ++result.games;
            }
        }
    }

    return result;
}
} // namespace

TEST_CASE("Cyborg tournament: section 4 bidding against heuristic bidding, pinned per table size",
          "[cyborg][tournament]")
{
    // A PIN, NOT A GATE. The plan's gate for Phase 2 was "section 4 bidding
    // averages higher". It does not at five and six players, and that was
    // reported rather than tuned away: heuristic play grabs tricks while short of
    // its bid and ducks once it has them, which rewards bidding low, and section
    // 4's bids have no plan behind them until Phase 3. So the totals are pinned
    // exactly instead. Any change to bidding - or Phase 3's card play - moves
    // them, and the new numbers are read and re-pinned on purpose.
    //
    // No WHIST_PIN_FLOAT_GOLDENS gate: LowRisk has no floats, and Cyborg's odds
    // use only basic arithmetic. CI on all three platforms is what confirms that
    // holds; if one diverges, gate this the way the Reckoner pins are gated.
    //
    // Seeds 30000..30039, every adjacent seat pair, A and B swapped on each deal.
    // Average points per game when pinned (A = section 4 bidding, B = heuristic):
    //
    //     players     A        B      A - B
    //        2      24.6     20.7     +3.9
    //        3      52.8     47.1     +5.7
    //        4      67.9     67.6     +0.3
    //        5      82.9     86.2     -3.3
    //        6      99.6    104.0     -4.5
    struct Expected
    {
        unsigned int players;
        long sectionFourTotal;
        long heuristicTotal;
    };

    constexpr std::array<Expected, 5> expected{{
        {2, 3942, 3316},
        {3, 12666, 11292},
        {4, 21730, 21620},
        {5, 33158, 34465},
        {6, 47792, 49933},
    }};

    for(const Expected& e : expected)
    {
        const MatchResult result = playAbMatch(e.players, 40, 30000);

        const unsigned int players = e.players;
        const long sectionFourTotal = result.sectionFourTotal;
        const long heuristicTotal = result.heuristicTotal;
        const unsigned int games = result.games;
        CAPTURE(players, games, sectionFourTotal, heuristicTotal);

        CHECK(result.fallbacks == 0);
        CHECK(sectionFourTotal == e.sectionFourTotal);
        CHECK(heuristicTotal == e.heuristicTotal);
    }
}
