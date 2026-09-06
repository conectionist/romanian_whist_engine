#include <catch2/catch_test_macros.hpp>

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>

using namespace romanian_whist;
using namespace romanian_whist::reckoner;

TEST_CASE("ReckonerTracker: must-trump rule and fail-to-follow void inference", "[reckoner][tracker]")
{
    ReckonerTracker tracker;
    const unsigned int n = 4;
    const unsigned int r = 5;
    const Card trumpCard{Rank::Seven, Suit::Spades};

    tracker.initRound(n, r, 0, 1, trumpCard);

    // Turn-up card is excluded from live
    const CardId trumpId = cardToId(trumpCard, n);
    REQUIRE((tracker.live & cardBit(trumpId)) == 0);

    // Trick 1: Seat 1 leads Hearts
    tracker.recordCardPlayed(1, Card{Rank::Ten, Suit::Hearts});

    // Seat 2 cannot follow Hearts and plays Clubs (an off-suit non-trump discard)
    tracker.recordCardPlayed(2, Card{Rank::Eight, Suit::Clubs});

    // Must-trump rule (§1.3): Seat 2 failed to follow Hearts, AND did not play trump Spades.
    // Proves Seat 2 is void in BOTH Hearts AND Spades!
    const unsigned int heartsSuit = static_cast<unsigned int>(Suit::Hearts);
    const unsigned int spadesSuit = static_cast<unsigned int>(Suit::Spades);

    REQUIRE((tracker.voidMask[2] & (1 << heartsSuit)) != 0);
    REQUIRE((tracker.voidMask[2] & (1 << spadesSuit)) != 0);

    // Seat 3 follows Hearts
    tracker.recordCardPlayed(3, Card{Rank::Jack, Suit::Hearts});
    REQUIRE((tracker.voidMask[3] & (1 << heartsSuit)) == 0);
}

TEST_CASE("ReckonerTracker: turned-up card makes King the master trump", "[reckoner][tracker]")
{
    ReckonerTracker tracker;
    const unsigned int n = 4;
    const unsigned int r = 7;
    const Card aceSpades{Rank::Ace, Suit::Spades};

    tracker.initRound(n, r, 0, 0, aceSpades);

    // Turn-up card Ace of Spades is dead
    const CardId aceId = cardToId(aceSpades, n);
    REQUIRE((tracker.live & cardBit(aceId)) == 0);

    // For King of Spades, cards above it in unseen is 0!
    const Card kingSpades{Rank::King, Suit::Spades};
    const CardId kingId = cardToId(kingSpades, n);
    const auto& profile = getDeckProfile(n);

    const Mask unseen = tracker.getUnseenCards(cardBit(kingId));
    REQUIRE(popcount(unseen & maskAbove(kingId, profile)) == 0);
}

TEST_CASE("ReckonerTracker: rank-gap constraint recording", "[reckoner][tracker]")
{
    ReckonerTracker tracker;
    const unsigned int n = 4;
    const unsigned int r = 4;
    const Card trumpCard{Rank::Eight, Suit::Diamonds};

    tracker.initRound(n, r, 0, 1, trumpCard);
    tracker.recordBid(2, 0); // Seat 2 bid 0 (DUCK mode)

    // Trick 1: Seat 1 leads 10 of Spades
    tracker.recordCardPlayed(1, Card{Rank::Ten, Suit::Spades});

    // Seat 2 follows with 8 of Spades (does not beat 10)
    tracker.recordCardPlayed(2, Card{Rank::Eight, Suit::Spades});

    // Seat 2 is in DUCK mode, followed suit, c did not beat B:
    // Ranked gap inference (§1.4): holds no Spades in (8, 10), i.e. no 9 of Spades
    REQUIRE(!tracker.constraints.empty());
    const auto& c = tracker.constraints.front();
    REQUIRE(c.player == 2);

    const CardId nineSpades = cardToId(Card{Rank::Nine, Suit::Spades}, n);
    REQUIRE((c.excluded & cardBit(nineSpades)) != 0);
}
