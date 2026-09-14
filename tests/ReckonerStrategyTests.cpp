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

// Reckoner's exact scores, pinned - on ONE toolchain.
//
// This exists to guard the machinery underneath the strategy against changes
// that leave it working but playing worse. The tournament test next door only
// asserts that Reckoner beats LowRisk on average, which a change that made it
// 15% worse would sail straight through. These vectors would not.
//
// Every deck profile is covered (2 through 6 players, so every ranksPerSuit from
// 4 to 12) and both round structures, because the mask arithmetic is indexed by
// player count and an off-by-one there shows up in one profile and no others.
//
// WHY IT IS GUARDED: Reckoner chooses by argmax over floats fed by std::exp and
// std::pow, so the same source legitimately plays a different game on a
// different libm. CI measured all three - see the comment on
// WHIST_PIN_FLOAT_GOLDENS in tests/CMakeLists.txt for the numbers. The vectors
// below are pinned against Linux/GCC and are not checked elsewhere, which means
// they do NOT guard the shared round tracker on Windows or macOS: that job
// belongs to CyborgMemoryTests.cpp, which pins integer state and runs everywhere.
//
// A deliberate change to how Reckoner decides SHOULD break this. Re-pin it in the
// same commit, and say in the message what moved and why - on a Linux/GCC build,
// because that is the only place these numbers mean anything.
#if defined(WHIST_PIN_FLOAT_GOLDENS)
TEST_CASE("ReckonerStrategy: pinned scores across every deck profile", "[reckoner][golden][fp-pinned]")
{
    auto run = [](unsigned int playerCount, GameStructure structure, std::uint32_t shuffleSeed) {
        GameEngine engine;
        GameSetup setup;
        setup.structure = structure;
        setup.shuffleSeed = shuffleSeed;

        setup.seats.push_back(makeReckonerSeat("Reckoner", engine, reckoner::ReckonerKnobs::medium(), 999));
        for(unsigned int i = 1 ; i < playerCount ; i++)
        {
            const std::string name = "LowRisk" + std::to_string(i);
            setup.seats.push_back({name, std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
        }

        engine.start(std::move(setup));
        engine.run();
        return finalScores(engine);
    };

    REQUIRE(run(2, GameStructure::S_181, 10) == std::vector<int>{ 76, -18 });
    REQUIRE(run(3, GameStructure::S_181, 11) == std::vector<int>{ 41, 43, 40 });
    REQUIRE(run(4, GameStructure::S_181, 12) == std::vector<int>{ 71, 86, 86, 70 });
    REQUIRE(run(4, GameStructure::S_818, 13) == std::vector<int>{ 57, 49, 99, 1 });
    REQUIRE(run(5, GameStructure::S_181, 14) == std::vector<int>{ 117, 105, 83, 92, 96 });
    REQUIRE(run(6, GameStructure::S_818, 15) == std::vector<int>{ 146, 110, 105, 84, 121, 117 });
}
#endif

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
