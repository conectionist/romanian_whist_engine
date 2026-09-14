#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CardStringMaker.h"

#include <romanian_whist/strategies/common/RoundMemory.h>
#include <romanian_whist/strategies/cyborg/Odds.h>

#include <stdexcept>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::cyborg;
using Catch::Matchers::WithinAbs;

// The odds kit, against the worked examples in §7 of the design document.
//
// Nothing here is float-portable by luck: hyper0() is built from multiplication
// and division only, both IEEE-754 exact, so these hold on every toolchain. That
// is unlike ReckonerStrategy's score pins, which go through std::exp/std::pow
// and are compiled on Linux/GCC only - see the comment in tests/CMakeLists.txt.
// Do NOT gate anything in this file on WHIST_PIN_FLOAT_GOLDENS.
//
// Where a claim in the document is a CERTAINTY rather than an estimate, the
// assertion is exact equality. That is deliberate: "cannot win" and "always
// wins" are not approximations.

namespace
{
common::CardId id(const Card& card, unsigned int playerCount)
{
    return common::cardToId(card, playerCount);
}

// §7.1's position: 4 players, one-card round, J-of-hearts turned up as trump.
OddsContext oneCardRound(const Card& myCard, float duckPropensity)
{
    common::RoundMemory memory;
    memory.initRound(4, 1, /*mySeat*/ 0, /*opener*/ 0, Card{Rank::Jack, Suit::Hearts});

    return makeOddsContext(memory, common::cardBit(id(myCard, 4)), Suit::Hearts, duckPropensity);
}
} // namespace

TEST_CASE("Cyborg odds: worked example 7.1, leading the ten of trumps at four players", "[cyborg]")
{
    const Card myCard{Rank::Ten, Suit::Hearts};
    const OddsContext ctx = oneCardRound(myCard, 0.0f);

    // 32-card deck, minus the turned-up jack, minus my own card.
    REQUIRE(common::popcount(ctx.unseen) == 30);

    // Three opponents holding one card each.
    REQUIRE(ctx.handsOutstanding == 3);

    const common::Mask beaterSet = beaters(id(myCard, 4), ctx.unseen, ctx);

    // Naming them is stronger than counting them, and it is what pins the claim
    // that the dead turn-up is not a beater.
    const std::vector<Card> expected = { Card{Rank::Queen, Suit::Hearts},
                                         Card{Rank::King, Suit::Hearts},
                                         Card{Rank::Ace, Suit::Hearts} };

    REQUIRE(common::maskToCards(beaterSet, 4) == expected);
    REQUIRE((beaterSet & common::cardBit(id(Card{Rank::Jack, Suit::Hearts}, 4))) == 0);

    // hyper0(3, 30, 3) = (27/30)(26/29)(25/28)
    REQUIRE_THAT(pLeadWins(id(myCard, 4), ctx), WithinAbs(0.720443, 1e-5));
}

TEST_CASE("Cyborg odds: nobody can duck when nobody has a choice of card", "[cyborg]")
{
    // The same position at the real duckPropensity. The answer is UNCHANGED,
    // and the reason is the boundary that would underflow if hbar were ever
    // allowed to reach zero: hbar = 3/3 = 1, so hyper0(.., .., hbar - 1) is
    // hyper0(.., .., 0) which is exactly 1, so pCanDuck is 0.
    //
    // §4.2 justifies forcing the duck term to zero in one-card rounds by hand;
    // here it falls out of the general formula, which is the better argument.
    const Card myCard{Rank::Ten, Suit::Hearts};

    const float withoutDucking = pLeadWins(id(myCard, 4), oneCardRound(myCard, 0.0f));
    const float withDucking = pLeadWins(id(myCard, 4), oneCardRound(myCard, 0.6f));

    REQUIRE(withDucking == withoutDucking);
}

TEST_CASE("Cyborg odds: a plain card is beaten by every live trump", "[cyborg]")
{
    // The clause people forget. The ace of spades is the top card of its suit
    // and still loses to any heart, because a player void in spades must ruff.
    const Card myCard{Rank::Ace, Suit::Spades};
    const OddsContext ctx = oneCardRound(myCard, 0.0f);

    const common::Mask beaterSet = beaters(id(myCard, 4), ctx.unseen, ctx);

    // Eight hearts in the deck, one of them turned up and therefore dead.
    REQUIRE(common::popcount(beaterSet) == 7);

    for(const Card& beater : common::maskToCards(beaterSet, 4))
    {
        INFO(beater.toString());
        REQUIRE(beater.suit == Suit::Hearts);
    }

    // And a trump is not beaten by trumps-in-general, only by higher ones.
    const Card trumpCard{Rank::Ace, Suit::Hearts};
    const OddsContext trumpCtx = oneCardRound(trumpCard, 0.0f);

    REQUIRE(beaters(id(trumpCard, 4), trumpCtx.unseen, trumpCtx) == 0);
    REQUIRE(isSureWinner(id(trumpCard, 4), trumpCtx));
}

TEST_CASE("Cyborg odds: a dead turn-up ace makes my king the top trump", "[cyborg]")
{
    // §2.2's claim, cashed in §3.2. The ace is out of the game before a card is
    // played, so the king is unbeatable for the whole round.
    common::RoundMemory memory;
    memory.initRound(4, 3, 0, 0, Card{Rank::Ace, Suit::Hearts});

    const Card myKing{Rank::King, Suit::Hearts};
    const OddsContext ctx =
        makeOddsContext(memory, common::cardBit(id(myKing, 4)), Suit::Hearts, 0.6f);

    REQUIRE(isSureWinner(id(myKing, 4), ctx));
    REQUIRE(pLeadWins(id(myKing, 4), ctx) == 1.0f);
}

TEST_CASE("Cyborg odds: worked example 7.5, leading a queen that cannot win", "[cyborg]")
{
    // An 8-trick round: the whole deck is dealt, so nothing is dead and every
    // unseen card is definitely in somebody's hand.
    constexpr unsigned int n = 4;

    const Card queen{Rank::Queen, Suit::Hearts};
    const Card seven{Rank::Seven, Suit::Hearts};
    const Card king{Rank::King, Suit::Hearts};
    const Card ten{Rank::Ten, Suit::Hearts};

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = std::nullopt;
    ctx.duckPropensity = 0.6f;
    ctx.handSize = { 0, 4, 4, 4, 0, 0 };

    SECTION("the king is the only heart left, so its holder must play it")
    {
        // Twelve unseen cards, all of them dealt.
        ctx.unseen = common::cardBit(id(king, n)) | common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Jack, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Jack, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Diamonds}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Diamonds}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Diamonds}, n));
        ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));

        // EXACT equality, because the document's claim is certainty, not
        // accuracy: there is no lower heart to duck with, so the king must
        // appear and must win.
        REQUIRE(pLeadWins(id(queen, n), ctx) == 0.0f);
        REQUIRE(pLeadWins(id(seven, n), ctx) == 0.0f);
        REQUIRE(isSureLoserOnLead(id(queen, n), ctx));
    }

    SECTION("add the ten and the queen becomes a real risk")
    {
        ctx.unseen = common::cardBit(id(king, n)) | common::cardBit(id(ten, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Jack, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Jack, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Diamonds}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Diamonds}, n));
        ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));

        // |U| = H = 12, hbar = 12/3 = 4, so pCanDuck = 1 - hyper0(1, 11, 3)
        // = 1 - 8/11 = 3/11, and the answer is 0.6 * 3/11.
        REQUIRE_THAT(pLeadWins(id(queen, n), ctx), WithinAbs(0.6 * 3.0 / 11.0, 1e-6));
        REQUIRE_FALSE(isSureLoserOnLead(id(queen, n), ctx));

        // The seven still cannot win - nothing live is below it - and THAT is
        // §7.4's whole content: duck with the queen, keep the seven.
        REQUIRE(pLeadWins(id(seven, n), ctx) == 0.0f);
        REQUIRE(pLeadWins(id(seven, n), ctx) < pLeadWins(id(queen, n), ctx));
    }
}

TEST_CASE("Cyborg odds: a card nothing can beat is not a sure loser", "[cyborg]")
{
    // The regression for a bug in the document's own §3.2: its three conditions
    // are all vacuously true for a card with no beaters, so an ace with nothing
    // live below it reported as a sure LOSER.
    constexpr unsigned int n = 4;

    const Card ace{Rank::Ace, Suit::Hearts};

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = std::nullopt;
    ctx.handSize = { 0, 2, 2, 2, 0, 0 };
    ctx.unseen = common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Diamonds}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Diamonds}, n));
    ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));

    REQUIRE(isSureWinner(id(ace, n), ctx));
    REQUIRE_FALSE(isSureLoserOnLead(id(ace, n), ctx));
    REQUIRE(pLeadWins(id(ace, n), ctx) == 1.0f);
}

TEST_CASE("Cyborg odds: a beater nobody may hold is not a beater", "[cyborg]")
{
    // The void correction. Two opponents, both proved void in hearts, so the
    // king of hearts they would need to beat my queen cannot be in either hand -
    // it is in the undealt remainder. That is a CERTAINTY, and the point of
    // tracking voids at all.
    constexpr unsigned int n = 3;

    const Card queen{Rank::Queen, Suit::Hearts};

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = std::nullopt;
    ctx.handSize = { 0, 2, 2, 0, 0, 0 };
    ctx.unseen = common::cardBit(id(Card{Rank::King, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Clubs}, n));
    ctx.handsOutstanding = 4;

    const unsigned int hearts = static_cast<unsigned int>(Suit::Hearts);

    // Without the correction there are two beaters and a real risk.
    REQUIRE(common::popcount(beaters(id(queen, n), ctx.unseen, ctx)) == 2);
    REQUIRE(pLeadWins(id(queen, n), ctx) < 1.0f);

    // Prove both live opponents void in hearts.
    ctx.voidMask[1] = static_cast<std::uint8_t>(1U << hearts);
    ctx.voidMask[2] = static_cast<std::uint8_t>(1U << hearts);

    REQUIRE(common::popcount(beaters(id(queen, n), ctx.unseen, ctx)) == 2);
    REQUIRE(reachableBeaters(id(queen, n), ctx) == 0);
    REQUIRE(isSureWinner(id(queen, n), ctx));
    REQUIRE(pLeadWins(id(queen, n), ctx) == 1.0f);
}

TEST_CASE("Cyborg odds: pHolds is exact at both ends", "[cyborg]")
{
    constexpr unsigned int n = 4;

    const Card myCard{Rank::Queen, Suit::Hearts};

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = Suit::Spades;
    ctx.handSize = { 2, 2, 2, 2, 0, 0 };
    ctx.unseen = common::cardBit(id(Card{Rank::King, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Seven, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Eight, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Nine, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Ten, Suit::Clubs}, n));
    ctx.handsOutstanding = 6;

    SECTION("last to act is a certainty, not an estimate")
    {
        REQUIRE(pHolds(id(myCard, n), 0, Suit::Hearts, ctx) == 1.0f);
    }

    SECTION("a seat void in the lead suit and in trump cannot hurt me")
    {
        ctx.voidMask[1] = static_cast<std::uint8_t>((1U << static_cast<unsigned int>(Suit::Hearts)) |
                                                    (1U << static_cast<unsigned int>(Suit::Spades)));

        REQUIRE(pHolds(id(myCard, n), 1, Suit::Hearts, ctx) == 1.0f);
    }

    SECTION("a seat that must follow, holding the only card above mine, is a certain loss")
    {
        // Seat 1 must follow hearts, and the only live heart beats my queen.
        ctx.handSize = { 2, 1, 0, 0, 0, 0 };
        ctx.unseen = common::cardBit(id(Card{Rank::King, Suit::Hearts}, n));
        ctx.handsOutstanding = 1;

        REQUIRE(pHolds(id(myCard, n), 1, Suit::Hearts, ctx) == 0.0f);
    }
}

TEST_CASE("Cyborg odds: pHolds counts trumps from seats not yet proved void", "[cyborg]")
{
    // The regression for pHolds treating "no void shown" as "must follow". My
    // ace of hearts has no heart above it, but two seats are still to act and
    // either may be out of hearts without having shown it - and then must ruff.
    constexpr unsigned int n = 4;

    const Card ace{Rank::Ace, Suit::Hearts};

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = Suit::Spades;
    ctx.handSize = { 2, 2, 2, 2, 0, 0 };
    ctx.unseen = common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Seven, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Eight, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Nine, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Ten, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Diamonds}, n));
    ctx.handsOutstanding = 6;

    SECTION("no voids shown: the live trump is a real risk")
    {
        // One beater (A♠), six unseen, the two seats to act hold four:
        // hyper0(1, 6, 4) = 2/6.
        REQUIRE_THAT(pHolds(id(ace, n), 2, Suit::Hearts, ctx), WithinAbs(1.0 / 3.0, 1e-6));
    }

    SECTION("both seats proved void in trump: nothing can take it")
    {
        const std::uint8_t spades = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(Suit::Spades));
        ctx.voidMask[1] = spades;
        ctx.voidMask[2] = spades;

        REQUIRE(pHolds(id(ace, n), 2, Suit::Hearts, ctx) == 1.0f);
    }
}

TEST_CASE("Cyborg odds: another player's void does not rescue a sure loser", "[cyborg]")
{
    // The regression for a "no opponent is void in the suit" condition that
    // §3.2 once carried. Everything is dealt and nothing below the card is live,
    // so the beater's holder must play it or a player out of the suit must ruff.
    constexpr unsigned int n = 4;

    const unsigned int hearts = static_cast<unsigned int>(Suit::Hearts);

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.duckPropensity = 0.6f;
    ctx.handSize = { 0, 2, 2, 2, 0, 0 };

    SECTION("a higher card of the suit is out, and a third seat is void in it")
    {
        const Card king{Rank::King, Suit::Hearts};

        ctx.trumpSuit = std::nullopt;
        ctx.unseen = common::cardBit(id(Card{Rank::Ace, Suit::Hearts}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Diamonds}, n));
        ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));
        ctx.voidMask[2] = static_cast<std::uint8_t>(1U << hearts);

        REQUIRE(isSureLoserOnLead(id(king, n), ctx));
        REQUIRE(pLeadWins(id(king, n), ctx) == 0.0f);
    }

    SECTION("only trumps beat it, so every opponent is out of the suit")
    {
        const Card ace{Rank::Ace, Suit::Hearts};

        ctx.trumpSuit = Suit::Spades;
        ctx.unseen = common::cardBit(id(Card{Rank::Seven, Suit::Spades}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Queen, Suit::Clubs}, n)) |
                     common::cardBit(id(Card{Rank::Ace, Suit::Diamonds}, n)) |
                     common::cardBit(id(Card{Rank::King, Suit::Diamonds}, n));
        ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));
        ctx.voidMask[1] = static_cast<std::uint8_t>(1U << hearts);
        ctx.voidMask[2] = static_cast<std::uint8_t>(1U << hearts);
        ctx.voidMask[3] = static_cast<std::uint8_t>(1U << hearts);

        REQUIRE(isSureLoserOnLead(id(ace, n), ctx));
        REQUIRE(pLeadWins(id(ace, n), ctx) == 0.0f);
    }
}

TEST_CASE("Cyborg odds: nothing dead means the answer is 0 or 1, never between", "[cyborg]")
{
    // In an 8-trick round every unseen card is in somebody's hand, so the first
    // term of pLeadWins collapses and only the duck estimate remains.
    constexpr unsigned int n = 4;

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.trumpSuit = std::nullopt;
    ctx.handSize = { 0, 3, 3, 3, 0, 0 };
    ctx.duckPropensity = 0.0f; // no duck term, so only the exact half is left
    ctx.unseen = common::cardBit(id(Card{Rank::Ace, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::Seven, Suit::Hearts}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Queen, Suit::Spades}, n)) |
                 common::cardBit(id(Card{Rank::Ace, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::King, Suit::Clubs}, n)) |
                 common::cardBit(id(Card{Rank::Queen, Suit::Clubs}, n));
    ctx.handsOutstanding = static_cast<unsigned int>(common::popcount(ctx.unseen));

    const float beaten = pLeadWins(id(Card{Rank::Queen, Suit::Hearts}, n), ctx);
    const float unbeatable = pLeadWins(id(Card{Rank::Ace, Suit::Diamonds}, n), ctx);

    REQUIRE(beaten == 0.0f);
    REQUIRE(unbeatable == 1.0f);
}

TEST_CASE("Cyborg odds: the guards nothing else would catch", "[cyborg]")
{
    constexpr unsigned int n = 4;

    OddsContext ctx{};
    ctx.playerCount = n;
    ctx.mySeat = 0;
    ctx.handSize = { 1, 1, 1, 1, 0, 0 };
    ctx.unseen = common::cardBit(id(Card{Rank::Ace, Suit::Hearts}, n));
    ctx.handsOutstanding = 1;

    SECTION("a card id outside the deck throws rather than reading past an array")
    {
        // maskAbove() indexes a 48-entry table with no check of its own, and
        // this project builds Release with no sanitizers - so unguarded this
        // would return a plausible number rather than crash.
        REQUIRE_THROWS_AS(beaters(common::INVALID_CARD_ID, ctx.unseen, ctx), std::invalid_argument);
        REQUIRE_THROWS_AS(beaters(static_cast<common::CardId>(common::getDeckProfile(n).deckSize),
                                  ctx.unseen, ctx),
                          std::invalid_argument);
    }

    SECTION("a player count outside the rules throws rather than dividing by zero")
    {
        OddsContext bad = ctx;
        bad.playerCount = 1;
        REQUIRE_THROWS_AS(pLeadWins(0, bad), std::invalid_argument);

        bad.playerCount = 7;
        REQUIRE_THROWS_AS(pLeadWins(0, bad), std::invalid_argument);
    }

    SECTION("nobody holding a card means whatever I lead wins")
    {
        OddsContext empty = ctx;
        empty.handsOutstanding = 0;
        empty.handSize = { 1, 0, 0, 0, 0, 0 };

        REQUIRE(pLeadWins(id(Card{Rank::Seven, Suit::Hearts}, n), empty) == 1.0f);
    }

    SECTION("a drifted memory still answers in range")
    {
        // Fewer cards outstanding than there are opponents: unreachable in a
        // healthy game, but recordCardPlayed() floors hand sizes at zero, so a
        // memory that missed a callback can reach it. Unclamped, the mean hand
        // size would be zero, hbar - 1 would wrap, and this would return its
        // MAXIMUM at the moment it is least trustworthy.
        OddsContext drifted{};
        drifted.playerCount = 6;
        drifted.mySeat = 0;
        drifted.handsOutstanding = 3;
        drifted.handSize = { 2, 1, 1, 1, 0, 0 };
        drifted.duckPropensity = 0.6f;
        drifted.unseen = common::cardBit(common::cardToId(Card{Rank::Ace, Suit::Hearts}, 6)) |
                         common::cardBit(common::cardToId(Card{Rank::King, Suit::Hearts}, 6)) |
                         common::cardBit(common::cardToId(Card{Rank::Three, Suit::Hearts}, 6)) |
                         common::cardBit(common::cardToId(Card{Rank::Four, Suit::Hearts}, 6));

        const float p = pLeadWins(common::cardToId(Card{Rank::Queen, Suit::Hearts}, 6), drifted);

        REQUIRE(p >= 0.0f);
        REQUIRE(p <= 1.0f);
    }
}

TEST_CASE("Cyborg odds: makeOddsContext refuses a memory that disagrees about trump", "[cyborg]")
{
    common::RoundMemory memory;
    memory.initRound(4, 4, 0, 0, Card{Rank::Jack, Suit::Hearts});

    const common::Mask myHand = common::cardBit(id(Card{Rank::Ace, Suit::Spades}, 4));

    REQUIRE_NOTHROW(makeOddsContext(memory, myHand, Suit::Hearts, 0.6f));

    // The position says spades are trump; the memory says hearts. The memory is
    // the replica, so it is the one that is wrong.
    REQUIRE_THROWS_AS(makeOddsContext(memory, myHand, Suit::Spades, 0.6f), std::logic_error);
    REQUIRE_THROWS_AS(makeOddsContext(memory, myHand, std::nullopt, 0.6f), std::logic_error);
}
