#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>

#include <chrono>
#include <iostream>

using namespace romanian_whist;
using namespace romanian_whist::test;

TEST_CASE("Reckoner beats LowRisk in multi-game tournament and satisfies latency budget", "[reckoner][tournament]")
{
    // 1 Reckoner vs 3 LowRisk across 15 full games (15 * 24 = 360 rounds)
    constexpr unsigned int kGames = 15;
    int reckonerTotalScore = 0;
    int lowRiskTotalScore = 0;

    auto startTime = std::chrono::steady_clock::now();

    for(unsigned int g = 0; g < kGames; ++g)
    {
        GameEngine engine;
        GameSetup setup;

        setup.seats.push_back(makeReckonerSeat("Reckoner", engine, reckoner::ReckonerKnobs::medium(), 1000 + g));
        setup.seats.push_back({"LowRisk1", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"LowRisk2", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"LowRisk3", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.shuffleSeed = 2000 + g;

        engine.start(std::move(setup));
        engine.run();

        const auto scores = finalScores(engine);
        reckonerTotalScore += scores[0];
        lowRiskTotalScore += (scores[1] + scores[2] + scores[3]);
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    const double reckonerAvg = static_cast<double>(reckonerTotalScore) / kGames;
    const double lowRiskAvg = static_cast<double>(lowRiskTotalScore) / (3.0 * kGames);

    // Goal 1: Reckoner consistently beats LowRisk
    REQUIRE(reckonerAvg > lowRiskAvg);

    // Goal 3: Average move latency well under 100 ms
    // In 15 games of 24 rounds, each round has average ~4 tricks.
    // That's ~15 * 24 = 360 bids and ~15 * 24 * 4 ≈ 1440 plays, total ~1800 decisions.
    // Total elapsed time should be well under 1800 * 100ms = 180 seconds.
    // In practice with Medium preset on 1 core, it takes only ~2-4 seconds total (~1-2 ms per decision).
    REQUIRE(elapsedMs < 60000);
}
