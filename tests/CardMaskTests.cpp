#include <catch2/catch_test_macros.hpp>

#include <romanian_whist/strategies/reckoner/CardMask.h>

using namespace romanian_whist;
using namespace romanian_whist::reckoner;

TEST_CASE("CardMask: profile and card conversion for 2 to 6 players", "[reckoner][cardmask]")
{
    for(unsigned int n = 2; n <= 6; ++n)
    {
        const auto& profile = getDeckProfile(n);
        REQUIRE(profile.playerCount == n);
        REQUIRE(profile.deckSize == 8 * n);
        REQUIRE(profile.ranksPerSuit == 2 * n);
        REQUIRE(popcount(profile.fullDeckMask) == static_cast<int>(8 * n));

        // Test all cards round-trip
        for(CardId c = 0; c < profile.deckSize; ++c)
        {
            const Card card = idToCard(c, n);
            const CardId roundtrip = cardToId(card, n);
            REQUIRE(roundtrip == c);

            const unsigned int s = cardSuit(c, profile.ranksPerSuit);
            const unsigned int r = cardRank(c, profile.ranksPerSuit);
            REQUIRE(s == static_cast<unsigned int>(card.suit));
            REQUIRE(r == static_cast<unsigned int>(static_cast<int>(card.rank) - profile.minRankInt));
        }
    }
}

TEST_CASE("CardMask: SUIT, ABOVE, BELOW, and BETWEEN masks", "[reckoner][cardmask]")
{
    const unsigned int n = 4; // 32-card deck, 8 ranks per suit (Seven to Ace)
    const auto& profile = getDeckProfile(n);

    // SUIT masks
    for(unsigned int s = 0; s < 4; ++s)
    {
        const Mask suitM = maskSuit(s, profile);
        REQUIRE(popcount(suitM) == 8);
        for(unsigned int other = 0; other < 4; ++other)
        {
            if(s != other)
            {
                REQUIRE((suitM & maskSuit(other, profile)) == 0);
            }
        }
    }

    // ABOVE and BELOW masks
    for(CardId c = 0; c < profile.deckSize; ++c)
    {
        const unsigned int r = cardRank(c, profile.ranksPerSuit);
        const Mask above = maskAbove(c, profile);
        const Mask below = maskBelow(c, profile);

        REQUIRE(popcount(above) == static_cast<int>(profile.ranksPerSuit - 1 - r));
        REQUIRE(popcount(below) == static_cast<int>(r));
        REQUIRE((above & below) == 0);
        REQUIRE((above & cardBit(c)) == 0);
        REQUIRE((below & cardBit(c)) == 0);
    }

    // BETWEEN masks
    const CardId sevenHearts = cardToId(Card{Rank::Seven, Suit::Hearts}, n);
    const CardId jackHearts = cardToId(Card{Rank::Jack, Suit::Hearts}, n);
    const Mask between = maskBetween(sevenHearts, jackHearts, profile);

    // Between 7 and J are 8, 9, 10 -> 3 cards
    REQUIRE(popcount(between) == 3);

    const CardId eightHearts = cardToId(Card{Rank::Eight, Suit::Hearts}, n);
    const CardId nineHearts = cardToId(Card{Rank::Nine, Suit::Hearts}, n);
    const CardId tenHearts = cardToId(Card{Rank::Ten, Suit::Hearts}, n);

    REQUIRE((between & cardBit(eightHearts)) != 0);
    REQUIRE((between & cardBit(nineHearts)) != 0);
    REQUIRE((between & cardBit(tenHearts)) != 0);
    REQUIRE((between & cardBit(sevenHearts)) == 0);
    REQUIRE((between & cardBit(jackHearts)) == 0);
}
