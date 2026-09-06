#include <catch2/catch_test_macros.hpp>

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>
#include <romanian_whist/strategies/reckoner/Sampler.h>

using namespace romanian_whist;
using namespace romanian_whist::reckoner;

TEST_CASE("Sampler: samples respect hand sizes and void constraints", "[reckoner][sampler]")
{
    ReckonerTracker tracker;
    const unsigned int n = 4;
    const unsigned int r = 4;
    const Card trumpCard{Rank::Seven, Suit::Spades};

    tracker.initRound(n, r, 0, 1, trumpCard);

    // Give player 1 a void in Hearts
    tracker.voidMask[1] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(Suit::Hearts));

    std::vector<Card> myHandCards = {
        Card{Rank::Ace, Suit::Hearts},
        Card{Rank::King, Suit::Clubs},
        Card{Rank::Eight, Suit::Diamonds},
        Card{Rank::Nine, Suit::Spades}
    };
    const Mask myHand = cardsToMask(myHandCards, n);

    std::mt19937 rng(12345);
    const unsigned int K = 50;
    const auto samples = Sampler::drawSamples(K, tracker, myHand, rng);

    REQUIRE(samples.size() == K);

    const auto& profile = getDeckProfile(n);
    const unsigned int heartsSuit = static_cast<unsigned int>(Suit::Hearts);

    for(const auto& s : samples)
    {
        // Player 1 must have exactly 4 cards
        REQUIRE(popcount(s.hands[1]) == 4);
        REQUIRE(popcount(s.hands[2]) == 4);
        REQUIRE(popcount(s.hands[3]) == 4);

        // Player 1 must NOT hold any Hearts (void constraint)
        REQUIRE((s.hands[1] & maskSuit(heartsSuit, profile)) == 0);

        // All sampled hands plus myHand must be mutually disjoint
        REQUIRE((s.hands[1] & s.hands[2]) == 0);
        REQUIRE((s.hands[1] & s.hands[3]) == 0);
        REQUIRE((s.hands[2] & s.hands[3]) == 0);
        REQUIRE((s.hands[1] & myHand) == 0);
        REQUIRE((s.hands[2] & myHand) == 0);
        REQUIRE((s.hands[3] & myHand) == 0);
    }
}
