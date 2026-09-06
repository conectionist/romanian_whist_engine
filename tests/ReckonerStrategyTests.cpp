#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>

using namespace romanian_whist;
using namespace romanian_whist::test;

TEST_CASE("ReckonerStrategy: full game integration across player counts and presets", "[reckoner]")
{
    // Test 2 players with Easy preset
    {
        GameEngine engine;
        GameSetup setup;
        setup.seats.push_back(makeReckonerSeat("ReckonerEasy", engine, reckoner::ReckonerKnobs::easy(), 42));
        setup.seats.push_back({"LowRisk", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.shuffleSeed = 100;

        engine.start(std::move(setup));
        REQUIRE_NOTHROW(engine.run());
        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }

    // Test 4 players with Hard preset
    {
        GameEngine engine;
        GameSetup setup;
        setup.seats.push_back(makeReckonerSeat("ReckonerHard", engine, reckoner::ReckonerKnobs::hard(), 42));
        setup.seats.push_back({"LowRisk1", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"LowRisk2", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"Ducking", std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>())});
        setup.shuffleSeed = 200;

        engine.start(std::move(setup));
        REQUIRE_NOTHROW(engine.run());
        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }

    // Test 4 players with Brutal preset
    {
        GameEngine engine;
        GameSetup setup;
        setup.seats.push_back(makeReckonerSeat("ReckonerBrutal", engine, reckoner::ReckonerKnobs::brutal(), 42));
        setup.seats.push_back({"LowRisk1", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"LowRisk2", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"FirstCard", std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>())});
        setup.shuffleSeed = 300;

        engine.start(std::move(setup));
        REQUIRE_NOTHROW(engine.run());
        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }
}

TEST_CASE("ReckonerStrategy: deterministic replay given same seed", "[reckoner]")
{
    auto runGame = [](std::uint32_t seed) {
        GameEngine engine;
        GameSetup setup;
        setup.seats.push_back(makeReckonerSeat("Reckoner", engine, reckoner::ReckonerKnobs::medium(), 999));
        setup.seats.push_back({"LowRisk1", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"LowRisk2", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        setup.seats.push_back({"Ducking", std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>())});
        setup.shuffleSeed = seed;

        engine.start(std::move(setup));
        engine.run();
        return finalScores(engine);
    };

    const auto scores1 = runGame(42);
    const auto scores2 = runGame(42);

    REQUIRE(scores1 == scores2);
}
