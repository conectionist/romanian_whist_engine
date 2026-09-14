#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include <romanian_whist/strategies/cyborg/Halves.h>

#include <stdexcept>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::cyborg;

// The big/small split, which every rule in the strategy is written in terms of.
//
// Worth its own file because the whole thing rests on one subtraction - a card's
// rank minus the lowest rank IN ITS OWN DECK - and getting that offset wrong
// shifts every category by one at exactly one player count, which no downstream
// test would name. The table below is the one in §1 of the design document,
// asserted literally rather than recomputed.

namespace
{
const Card Ace(Rank::Ace, Suit::Hearts);
const Card King(Rank::King, Suit::Hearts);
const Card Queen(Rank::Queen, Suit::Hearts);
const Card Jack(Rank::Jack, Suit::Hearts);
const Card Ten(Rank::Ten, Suit::Hearts);
const Card Nine(Rank::Nine, Suit::Hearts);
const Card Eight(Rank::Eight, Suit::Hearts);
const Card Three(Rank::Three, Suit::Hearts);

// The big hearts each deck should have, lowest first - maskToCards() returns
// ascending card ids, which within one suit is ascending rank.
std::vector<Card> bigHeartsFor(unsigned int playerCount)
{
    switch(playerCount)
    {
        case 2: return { King, Ace };
        case 3: return { Queen, King, Ace };
        case 4: return { Jack, Queen, King, Ace };
        case 5: return { Ten, Jack, Queen, King, Ace };
        default: return { Nine, Ten, Jack, Queen, King, Ace };
    }
}

// The lowest rank each deck contains.
Rank lowestRankFor(unsigned int playerCount)
{
    switch(playerCount)
    {
        case 2: return Rank::Jack;
        case 3: return Rank::Nine;
        case 4: return Rank::Seven;
        case 5: return Rank::Five;
        default: return Rank::Three;
    }
}
} // namespace

TEST_CASE("Cyborg halves: the big cards are exactly the §1 table", "[cyborg]")
{
    for(unsigned int n = 2 ; n <= 6 ; n++)
    {
        INFO(n << " players");

        const auto& profile = common::getDeckProfile(n);
        const common::Mask bigHearts =
            bigHalfMask(profile) & common::maskSuit(static_cast<unsigned int>(Suit::Hearts), profile);

        REQUIRE(common::maskToCards(bigHearts, n) == bigHeartsFor(n));
    }
}

TEST_CASE("Cyborg halves: every suit has exactly N big cards", "[cyborg]")
{
    for(unsigned int n = 2 ; n <= 6 ; n++)
    {
        const auto& profile = common::getDeckProfile(n);
        const common::Mask big = bigHalfMask(profile);

        INFO(n << " players");

        // Half the deck, split evenly across the suits.
        REQUIRE(common::popcount(big) == static_cast<int>(4 * n));

        for(unsigned int s = 0 ; s < 4 ; s++)
        {
            INFO("suit " << s);
            REQUIRE(common::popcount(big & common::maskSuit(s, profile)) == static_cast<int>(n));
        }

        // And they really are the TOP half: every big card outranks every small
        // one within its suit. Asserted through the id-level predicates, since
        // that is what the rules will call.
        for(common::CardId c = 0 ; c < profile.deckSize ; c++)
        {
            const bool inMask = (big & common::cardBit(c)) != 0;
            INFO("card id " << static_cast<unsigned int>(c));
            REQUIRE(inMask == isBigId(c, profile));
            REQUIRE(isSmallId(c, profile) == !isBigId(c, profile));
        }
    }
}

TEST_CASE("Cyborg halves: rank index runs 0 to 2N-1 within each deck", "[cyborg]")
{
    for(unsigned int n = 2 ; n <= 6 ; n++)
    {
        INFO(n << " players");

        REQUIRE(rankIndex(Ace, n) == 2 * n - 1);
        REQUIRE(rankIndex(Card{lowestRankFor(n), Suit::Hearts}, n) == 0);

        // The ace is big and the deck's lowest card is small, at every count.
        REQUIRE(isBig(Ace, n));
        REQUIRE(isSmall(Card{lowestRankFor(n), Suit::Hearts}, n));
    }
}

TEST_CASE("Cyborg halves: a rank this deck does not contain is rejected", "[cyborg]")
{
    // The off-by-one this file exists to catch would make one of these silently
    // answer instead of throwing.
    REQUIRE_THROWS_AS(rankIndex(Three, 2), std::invalid_argument);
    REQUIRE_THROWS_AS(rankIndex(Three, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(isBig(Card{Rank::Two, Suit::Spades}, 6), std::invalid_argument);

    // Three IS in the six-handed deck, and it is the lowest card there.
    REQUIRE(rankIndex(Three, 6) == 0);
}

TEST_CASE("Cyborg halves: categorise names all four kinds, and only two without trump", "[cyborg]")
{
    constexpr unsigned int n = 4; // big is A K Q J, small is 10 9 8 7

    SECTION("with a trump suit")
    {
        REQUIRE(categorise(Ace, n, Suit::Hearts) == Category::BigTrump);
        REQUIRE(categorise(Eight, n, Suit::Hearts) == Category::SmallTrump);
        REQUIRE(categorise(Ace, n, Suit::Spades) == Category::BigNonTrump);
        REQUIRE(categorise(Eight, n, Suit::Spades) == Category::SmallNonTrump);
    }

    SECTION("an 8-trick round has no trump, so only two categories exist")
    {
        const auto& profile = common::getDeckProfile(n);

        for(common::CardId c = 0 ; c < profile.deckSize ; c++)
        {
            const Category category = categoriseId(c, profile, std::nullopt);

            INFO("card " << common::idToCard(c, n).toString());
            REQUIRE((category == Category::BigNonTrump || category == Category::SmallNonTrump));
        }
    }
}
