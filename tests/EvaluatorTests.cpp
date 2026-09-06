#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/Evaluator.h>

using namespace romanian_whist;
using namespace romanian_whist::reckoner;
using Catch::Matchers::WithinAbs;

TEST_CASE("Evaluator: hyper0 matches exact combinatorics", "[reckoner][evaluator]")
{
    // If h == 0, p = 1.0
    REQUIRE(hyper0(3, 10, 0) == 1.0f);

    // If n - k < h, p = 0.0
    REQUIRE(hyper0(8, 10, 3) == 0.0f);

    // n = 10, k = 3, h = 2:
    // C(7, 2) / C(10, 2) = 21 / 45 = 7 / 15 ≈ 0.4666667
    REQUIRE_THAT(hyper0(3, 10, 2), WithinAbs(7.0 / 15.0, 1e-5));

    // n = 32, k = 1, h = 8:
    // P(none of 1 card in 8 draws from 32) = (31/32) * (30/31) * ... * (24/25) = 24 / 32 = 0.75
    REQUIRE_THAT(hyper0(1, 32, 8), WithinAbs(0.75, 1e-5));
}

TEST_CASE("Evaluator: evalHand in 8-trick round vs short round", "[reckoner][evaluator]")
{
    const unsigned int n = 4;
    const auto& profile = getDeckProfile(n);

    // Bare Ace in 8-trick no-trump round
    const Card aceHearts{Rank::Ace, Suit::Hearts};
    const Mask hand = cardBit(cardToId(aceHearts, n));

    EvalContext ctx8{};
    ctx8.playerCount = n;
    ctx8.live = profile.fullDeckMask;
    for(unsigned int p = 0; p < n; ++p)
    {
        ctx8.handSize[p] = 8;
        ctx8.voidMask[p] = 0;
    }
    ctx8.trumpSuit = std::nullopt;

    const float e8 = evalHand(hand, 0, ctx8);
    // In 8-trick no-trump round, higher cards unaccounted for = 0, no trump, pTop = 1.0, e = 1.0
    REQUIRE_THAT(e8, WithinAbs(1.0, 1e-4));

    // In a 2-trick round with Spades trump:
    // Ace of Hearts has threat of ruff by opponents
    EvalContext ctx2{};
    ctx2.playerCount = n;
    ctx2.live = profile.fullDeckMask;
    for(unsigned int p = 0; p < n; ++p)
    {
        ctx2.handSize[p] = 2;
        ctx2.voidMask[p] = 0;
    }
    ctx2.trumpSuit = Suit::Spades;

    const float e2 = evalHand(hand, 0, ctx2);
    // In short round with trump, bare non-trump ace is significantly discounted due to ruff risk
    REQUIRE(e2 < 0.7f);
    REQUIRE(e2 > 0.1f);
}
