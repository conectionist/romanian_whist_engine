#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

using namespace romanian_whist;
using namespace romanian_whist::test;

namespace
{
constexpr std::uint32_t kGoldenSeed = 42;
}

TEST_CASE("Golden game: 2 players, S_181", "[golden]")
{
    const auto engine = playFullGame(GameStructure::S_181, buildRoundRobinProviders(2), kGoldenSeed);
    REQUIRE(finalScores(*engine) == std::vector<int>{ -36, 28 });
}

TEST_CASE("Golden game: 2 players, S_818", "[golden]")
{
    const auto engine = playFullGame(GameStructure::S_818, buildRoundRobinProviders(2), kGoldenSeed);
    REQUIRE(finalScores(*engine) == std::vector<int>{ -19, 4 });
}

TEST_CASE("Golden game: 4 players, S_181, with full round record", "[golden]")
{
    RoundRecorder recorder;

    const auto engine = playFullGame(GameStructure::S_181, buildRoundRobinProviders(4), kGoldenSeed,
                                     { &recorder });
    const RoundRecord& record = recorder.record;

    REQUIRE(finalScores(*engine) == std::vector<int>{ 39, 56, 10, 16 });
    REQUIRE(record.size() == engine->getRoundCount());

    // clang-format off
    const RoundRecord expected{
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {1,0}, {1,1}, {0,0}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,2} },
        { {0,0}, {1,0}, {0,1}, {0,2} },
        { {0,1}, {1,1}, {0,2}, {0,0} },
        { {0,0}, {1,4}, {0,1}, {0,0} },
        { {0,3}, {1,0}, {0,1}, {0,2} },
        { {0,2}, {0,2}, {0,2}, {0,1} },
        { {0,1}, {1,1}, {0,6}, {0,0} },
        { {0,2}, {0,2}, {0,0}, {0,4} },
        { {0,1}, {1,4}, {0,2}, {0,1} },
        { {0,1}, {2,6}, {0,0}, {0,1} },
        { {0,0}, {1,1}, {0,2}, {0,4} },
        { {0,1}, {0,2}, {0,1}, {0,2} },
        { {0,0}, {2,3}, {0,1}, {0,1} },
        { {0,2}, {1,1}, {0,0}, {0,1} },
        { {0,1}, {1,1}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,2}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,1}, {0,0}, {0,0} },
    };
    // clang-format on

    REQUIRE(record == expected);
}

TEST_CASE("Golden game: 4 players, S_818", "[golden]")
{
    const auto engine = playFullGame(GameStructure::S_818, buildRoundRobinProviders(4), kGoldenSeed);
    REQUIRE(finalScores(*engine) == std::vector<int>{ -6, 48, 20, -25 });
}

TEST_CASE("Golden game: 6 players, S_181", "[golden]")
{
    const auto engine = playFullGame(GameStructure::S_181, buildRoundRobinProviders(6), kGoldenSeed);
    REQUIRE(finalScores(*engine) == std::vector<int>{ -8, 78, 55, 82, 75, 114 });
}

TEST_CASE("Golden game: 6 players, S_818", "[golden]")
{
    const auto engine = playFullGame(GameStructure::S_818, buildRoundRobinProviders(6), kGoldenSeed);
    REQUIRE(finalScores(*engine) == std::vector<int>{ 47, 72, 46, 51, 119, 58 });
}

TEST_CASE("Golden game: 4 players, S_181, endWithForeheadAndHidden + all1GamesAreForehead", "[golden]")
{
    // Nothing else exercises these flags through a full playthrough: the
    // schedule-shape unit tests (ScoreboardTests.cpp) cover the round types
    // and count in isolation, but not that scores/bids/tricks still add up,
    // and not the opener rotation continuing correctly across the boundary
    // into the two appended rounds.
    RoundRecorder recorder;

    const auto engine = playFullGame(GameStructure::S_181, buildRoundRobinProviders(4), kGoldenSeed,
                                     { &recorder },
                                     /*endWithForeheadAndHidden=*/true, /*all1GamesAreForehead=*/true);
    const RoundRecord& record = recorder.record;

    REQUIRE(engine->getRoundCount() == 3 * 4 + 12 + 2);
    REQUIRE(record.size() == engine->getRoundCount());
    REQUIRE(finalScores(*engine) == std::vector<int>{ 55, 59, 14, 20 });

    // Re-recorded for ENGINE_V4_PLAN.md Phase 5: LowRiskStrategy (seat 1 in the
    // round-robin) now bids 0 rather than reading its hand in a Forehead/Hidden
    // round. Only round 1 actually moved - every other one-card round already
    // heuristically came out to 0 - and the change is confined to that round's
    // own two bids: seat 1 bids 0 instead of 1, which in turn changes what
    // GameEngine::getForbiddenBet() bars for the final bidder (seat 0, since
    // round 1's leader is seat 1), so seat 0's bid moves from 1 to 0 too. Every
    // row for a Normal round (indices 4-19) is untouched, which is the
    // required scope check per Phase 5's re-baselining rule.
    // clang-format off
    const RoundRecord expected{
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {0,0}, {0,1}, {0,0}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,2} },
        { {0,0}, {1,0}, {0,1}, {0,2} },
        { {0,1}, {1,1}, {0,2}, {0,0} },
        { {0,0}, {1,4}, {0,1}, {0,0} },
        { {0,3}, {1,0}, {0,1}, {0,2} },
        { {0,2}, {0,2}, {0,2}, {0,1} },
        { {0,1}, {1,1}, {0,6}, {0,0} },
        { {0,2}, {0,2}, {0,0}, {0,4} },
        { {0,1}, {1,4}, {0,2}, {0,1} },
        { {0,1}, {2,6}, {0,0}, {0,1} },
        { {0,0}, {1,1}, {0,2}, {0,4} },
        { {0,1}, {0,2}, {0,1}, {0,2} },
        { {0,0}, {2,3}, {0,1}, {0,1} },
        { {0,2}, {1,1}, {0,0}, {0,1} },
        { {0,1}, {1,1}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,2}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,1} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,1}, {0,0}, {0,0} },
        { {0,0}, {0,0}, {0,1}, {0,0} },
        { {0,0}, {0,0}, {0,0}, {0,1} },
    };
    // clang-format on

    REQUIRE(record == expected);
}
