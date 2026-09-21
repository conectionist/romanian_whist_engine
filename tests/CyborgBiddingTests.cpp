#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CardStringMaker.h"

#include <romanian_whist/strategies/cyborg/Bidding.h>
#include <romanian_whist/strategies/cyborg/Odds.h>

using namespace romanian_whist;
using namespace romanian_whist::cyborg;
using Catch::Matchers::WithinAbs;

TEST_CASE("Cyborg bidding: resolveFraction rounding behavior", "[cyborg]")
{
    // The literal section 4.1 rule. useExpectedRemaining is on by default, so
    // switch it off explicitly; the refinement has its own section below.
    CyborgKnobs knobs{};
    knobs.useExpectedRemaining = false;
    Rounding out = Rounding::Exact;

    SECTION("exact integers are not rounded")
    {
        REQUIRE(resolveFraction(2.0f, 0, 5, 3, 4, knobs, out) == 2);
        REQUIRE(out == Rounding::Exact);

        REQUIRE(resolveFraction(0.0f, 4, 5, 1, 4, knobs, out) == 0);
        REQUIRE(out == Rounding::Exact);

        REQUIRE(resolveFraction(5.0f, 1, 5, 0, 4, knobs, out) == 5);
        REQUIRE(out == Rounding::Exact);
    }

    SECTION("over-subscribed rounds down (floored)")
    {
        // previousBidSum (4) + raw (1.5) = 5.5 > R (5) -> floor to 1
        REQUIRE(resolveFraction(1.5f, 4, 5, 1, 4, knobs, out) == 1);
        REQUIRE(out == Rounding::Floored);
    }

    SECTION("under-subscribed rounds up (ceiled)")
    {
        // previousBidSum (1) + raw (1.5) = 2.5 <= R (5) -> ceil to 2
        REQUIRE(resolveFraction(1.5f, 1, 5, 1, 4, knobs, out) == 2);
        REQUIRE(out == Rounding::Ceiled);
    }

    SECTION("a sum just below trickCount is under-subscribed so ceiled")
    {
        // previousBidSum (2) + raw (1.5) = 3.5 <= R (4) -> ceil to 2
        REQUIRE(resolveFraction(1.5f, 2, 4, 1, 4, knobs, out) == 2);
        REQUIRE(out == Rounding::Ceiled);
    }

    SECTION("useExpectedRemaining refinement adds fair share of unbid seats")
    {
        CyborgKnobs expKnobs{};
        expKnobs.useExpectedRemaining = true;

        // Worked example 7.3: R = 8, N = 4, opener (previousBidSum = 0, seatsStillToBid = 3), raw = 4.5
        // Without refinement: 0 + 4.5 <= 8 -> ceil -> 5
        REQUIRE(resolveFraction(4.5f, 0, 8, 3, 4, knobs, out) == 5);
        REQUIRE(out == Rounding::Ceiled);

        // With refinement: 0 + 4.5 + 3 * (8/4) = 10.5 > 8 -> floor -> 4
        REQUIRE(resolveFraction(4.5f, 0, 8, 3, 4, expKnobs, out) == 4);
        REQUIRE(out == Rounding::Floored);
    }

    SECTION("clamping at 0 and R")
    {
        // Over-subscribed floor clamped at 0
        REQUIRE(resolveFraction(0.4f, 5, 5, 0, 4, knobs, out) == 0);
        REQUIRE(out == Rounding::Floored);

        // Ceil clamped at R
        REQUIRE(resolveFraction(4.8f, 0, 5, 2, 4, knobs, out) == 5);
        REQUIRE(out == Rounding::Ceiled);
    }
}

TEST_CASE("Cyborg bidding: section 4.2 one-trick leader threshold", "[cyborg]")
{
    // Threshold is 6/13 approx 0.461538f
    constexpr float thresh = 6.0f / 13.0f;

    REQUIRE(bidOneTrickAsLeader(thresh + 0.001f) == 1);
    REQUIRE(bidOneTrickAsLeader(thresh - 0.001f) == 0);
    REQUIRE(bidOneTrickAsLeader(thresh) == 0);
    REQUIRE(bidOneTrickAsLeader(1.0f) == 1);
    REQUIRE(bidOneTrickAsLeader(0.0f) == 0);
}

TEST_CASE("Cyborg bidding: section 7.1 worked example", "[cyborg]")
{
    // 4 players, 1 trick, trump J hearts (turned up). Round leader holds 10 hearts.
    // deck 32 -> live = 31 (turn-up out) -> unseen = 30 (10 hearts out)
    // beaters(10 hearts) = Q hearts, K hearts, A hearts (3 beaters, J hearts is dead)
    // H = 3
    // p = hyper0(3, 30, 3) = (27/30)*(26/29)*(25/28) approx 0.720443
    common::RoundMemory mem;
    const Card trumpCard{Rank::Jack, Suit::Hearts};
    mem.initRound(4, 1, 0, 0, trumpCard);

    const Card myCard{Rank::Ten, Suit::Hearts};
    const std::vector<Card> hand{myCard};
    const common::Mask myHand = common::cardsToMask(hand, 4);

    const OddsContext ctx = makeOddsContext(mem, myHand, Suit::Hearts, 0.60f);

    const common::CardId cid = common::cardToId(myCard, 4);
    const float p = pLeadWins(cid, ctx);

    // Exact fraction: 27*26*25 / (30*29*28) = 17550 / 24360 = 2925 / 4060 = 585 / 812 approx 0.72044335
    constexpr float expectedP = (27.0f / 30.0f) * (26.0f / 29.0f) * (25.0f / 28.0f);
    REQUIRE_THAT(p, WithinAbs(expectedP, 1e-5f));

    BidInputs inputs;
    inputs.hand = hand;
    inputs.playerCount = 4;
    inputs.trickCount = 1;
    inputs.trump = trumpCard;
    inputs.biddingPosition = 0;
    inputs.previousBidSum = 0;
    inputs.odds = ctx;

    CyborgKnobs knobs{};
    const BidResult res = chooseBid(inputs, knobs);
    REQUIRE(res.bid == 1);
    REQUIRE(res.rounding == Rounding::Exact);
}

TEST_CASE("Cyborg bidding: section 4.2 user one-card leader examples", "[cyborg]")
{
    // 4 players, trump J hearts, round leader:
    // 10 hearts -> 1
    // A hearts  -> 1
    // A spades  -> 0 (p approx 0.4362, just under 6/13)
    // 7 spades  -> 0
    // 10 spades -> 0
    // 10 diamonds -> 0 (10 spades and 10 diamonds must give identical p approx 0.2387)
    const Card trumpCard{Rank::Jack, Suit::Hearts};
    CyborgKnobs knobs{};

    auto evalOneCard = [&](const Card& c) -> std::pair<float, unsigned int> {
        common::RoundMemory mem;
        mem.initRound(4, 1, 0, 0, trumpCard);
        const std::vector<Card> hand{c};
        const common::Mask myHand = common::cardsToMask(hand, 4);
        const OddsContext ctx = makeOddsContext(mem, myHand, Suit::Hearts, 0.60f);
        const common::CardId cid = common::cardToId(c, 4);
        const float p = pLeadWins(cid, ctx);

        BidInputs inputs;
        inputs.hand = hand;
        inputs.playerCount = 4;
        inputs.trickCount = 1;
        inputs.trump = trumpCard;
        inputs.biddingPosition = 0;
        inputs.odds = ctx;

        const BidResult res = chooseBid(inputs, knobs);
        return {p, res.bid};
    };

    const auto [p10H, bid10H] = evalOneCard(Card{Rank::Ten, Suit::Hearts});
    REQUIRE(bid10H == 1);

    const auto [pAH, bidAH] = evalOneCard(Card{Rank::Ace, Suit::Hearts});
    REQUIRE(bidAH == 1);
    REQUIRE_THAT(pAH, WithinAbs(1.0f, 1e-5f)); // Unbeatable

    const auto [pAS, bidAS] = evalOneCard(Card{Rank::Ace, Suit::Spades});
    // Beaters of A spades: all live trumps (7, 8, 9, 10, Q, K, A of hearts = 7 trumps)
    // p = hyper0(7, 30, 3) = (23/30)*(22/29)*(21/28) = 10626 / 24360 approx 0.436207
    constexpr float expectedPAS = (23.0f / 30.0f) * (22.0f / 29.0f) * (21.0f / 28.0f);
    REQUIRE_THAT(pAS, WithinAbs(expectedPAS, 1e-5f));
    REQUIRE(bidAS == 0); // Just under 6/13 approx 0.4615

    const auto [p7S, bid7S] = evalOneCard(Card{Rank::Seven, Suit::Spades});
    REQUIRE(bid7S == 0);

    const auto [p10S, bid10S] = evalOneCard(Card{Rank::Ten, Suit::Spades});
    const auto [p10D, bid10D] = evalOneCard(Card{Rank::Ten, Suit::Diamonds});
    REQUIRE(bid10S == 0);
    REQUIRE(bid10D == 0);
    REQUIRE_THAT(p10S, WithinAbs(p10D, 1e-6f));
    REQUIRE_THAT(p10S, WithinAbs(0.23867f, 1e-4f));
}

TEST_CASE("Cyborg bidding: section 4.2 middle and last seat table", "[cyborg]")
{
    const Card trumpCard{Rank::Jack, Suit::Hearts};
    CyborgKnobs knobs{};

    common::RoundMemory mem;
    mem.initRound(4, 1, 1, 0, trumpCard);

    auto makeInputs = [&](const Card& c, unsigned int pos, const std::vector<int>& bids) {
        const std::vector<Card> hand{c};
        const common::Mask myHand = common::cardsToMask(hand, 4);
        const OddsContext ctx = makeOddsContext(mem, myHand, Suit::Hearts, 0.60f);

        BidInputs inputs;
        inputs.hand = hand;
        inputs.playerCount = 4;
        inputs.trickCount = 1;
        inputs.trump = trumpCard;
        inputs.biddingPosition = pos;
        inputs.bidsInBiddingOrder = bids;
        inputs.odds = ctx;
        return inputs;
    };

    SECTION("middle seat: no trump in hand -> bid 0")
    {
        // Holding Ace of Spades in middle seat (pos 1)
        const BidInputs inputs = makeInputs(Card{Rank::Ace, Suit::Spades}, 1, {0});
        REQUIRE(bidOneTrick(inputs, knobs) == 0);
    }

    SECTION("middle seat: holding trump, someone after opener bid 1, p above 6/13 -> bid 1")
    {
        // Pos 2, bids = {0, 1} -> someone after opener bid 1
        // Holding Ten of Hearts (p approx 0.72 > 6/13) -> bid 1
        const BidInputs inputs1 = makeInputs(Card{Rank::Ten, Suit::Hearts}, 2, {0, 1});
        REQUIRE(bidOneTrick(inputs1, knobs) == 1);
    }

    SECTION("middle seat: holding trump, someone after opener bid 1, p below 6/13 -> bid 0")
    {
        // At four players every trump has p > 6/13 (even the 7 is approx 0.4985), so
        // only a bigger table can show this row bidding 0 - and bidding 0 is what
        // tells it apart from "everyone before me bid 0 -> 1".
        //
        // 6 players, turn-up J hearts, seat 2 holds the 3 of hearts:
        // deck 48 -> unseen 46, H = 5 opponents with one card each
        // beaters(3 hearts) = 4 5 6 7 8 9 10 Q K A of hearts = 10 (the jack is dead)
        // p = hyper0(10, 46, 5) = (36/46)(35/45)(34/44)(33/43)(32/42) approx 0.2750
        common::RoundMemory mem6;
        mem6.initRound(6, 1, 2, 0, trumpCard);

        const Card threeOfHearts{Rank::Three, Suit::Hearts};
        const std::vector<Card> hand{threeOfHearts};
        const OddsContext ctx = makeOddsContext(mem6, common::cardsToMask(hand, 6), Suit::Hearts, 0.60f);

        constexpr float expectedP =
            (36.0f / 46.0f) * (35.0f / 45.0f) * (34.0f / 44.0f) * (33.0f / 43.0f) * (32.0f / 42.0f);
        REQUIRE_THAT(pLeadWins(common::cardToId(threeOfHearts, 6), ctx), WithinAbs(expectedP, 1e-5f));

        BidInputs inputs;
        inputs.hand = hand;
        inputs.playerCount = 6;
        inputs.trickCount = 1;
        inputs.trump = trumpCard;
        inputs.biddingPosition = 2;
        inputs.bidsInBiddingOrder = {0, 1};
        inputs.odds = ctx;
        REQUIRE(bidOneTrick(inputs, knobs) == 0);
    }

    SECTION("middle seat: holding trump, everyone before me bid 0 -> bid 1")
    {
        const BidInputs inputs = makeInputs(Card{Rank::Seven, Suit::Hearts}, 1, {0});
        REQUIRE(bidOneTrick(inputs, knobs) == 1);
    }

    SECTION("middle seat: holding trump, only opener bid 1, small trump -> bid 0")
    {
        // 4 players: big cards are A, K, Q, J. J is trump turn-up.
        // Small cards are 10, 9, 8, 7.
        const BidInputs inputs = makeInputs(Card{Rank::Ten, Suit::Hearts}, 1, {1});
        REQUIRE(bidOneTrick(inputs, knobs) == 0);
    }

    SECTION("middle seat: holding trump, only opener bid 1, big trump -> bid 1")
    {
        const BidInputs inputs = makeInputs(Card{Rank::Queen, Suit::Hearts}, 1, {1});
        REQUIRE(bidOneTrick(inputs, knobs) == 1);
    }

    SECTION("last seat: 0 legal -> 0; 0 barred -> 1")
    {
        BidInputs inputs0 = makeInputs(Card{Rank::Ace, Suit::Hearts}, 3, {0, 0, 0});
        inputs0.forbiddenBet = 1; // 0 is legal
        REQUIRE(bidOneTrick(inputs0, knobs) == 0);

        BidInputs inputs1 = makeInputs(Card{Rank::Seven, Suit::Spades}, 3, {0, 0, 0});
        inputs1.forbiddenBet = 0; // 0 is barred
        REQUIRE(bidOneTrick(inputs1, knobs) == 1);
    }
}

TEST_CASE("Cyborg bidding: section 4.3 two-trick table as leader", "[cyborg]")
{
    // 4 players, trump suit Diamonds (turn-up 7 of diamonds). Big ranks are A K Q J.
    // BT: A K Q J of diamonds    ST: 10 9 8 of diamonds
    // BN: A K Q J of spades      SN: 10 9 8 7 of spades
    const Suit TS = Suit::Diamonds;

    // Every row is checked with the hand in both orders, so a rule that returns
    // "the first card" or "the second card" cannot pass by accident.
    auto testRow = [&](const Card& c1, const Card& c2, unsigned int expectedBid, const Card& expectedLead) {
        for(const auto& hand : {std::vector<Card>{c1, c2}, std::vector<Card>{c2, c1}})
        {
            BidInputs inputs;
            inputs.hand = hand;
            inputs.playerCount = 4;
            inputs.trickCount = 2;
            inputs.trump = Card{Rank::Seven, TS};
            inputs.biddingPosition = 0;
            inputs.previousBidSum = 0;

            CAPTURE(hand);
            const BidResult res = bidTwoTricksAsLeader(inputs);
            REQUIRE(res.bid == expectedBid);
            REQUIRE(res.plannedLead.has_value());
            REQUIRE(*res.plannedLead == expectedLead);
        }
    };

    SECTION("1. BT + BT -> bid 2, lead higher BT")
    {
        testRow(Card{Rank::King, TS}, Card{Rank::Queen, TS}, 2, Card{Rank::King, TS});
    }

    SECTION("2. BT + BN -> bid 2, lead the BT")
    {
        testRow(Card{Rank::King, TS}, Card{Rank::Ace, Suit::Spades}, 2, Card{Rank::King, TS});
    }

    SECTION("3. BT + ST -> bid 1, lead the ST")
    {
        testRow(Card{Rank::King, TS}, Card{Rank::Nine, TS}, 1, Card{Rank::Nine, TS});
    }

    SECTION("4. BT + SN -> bid 1, lead the SN")
    {
        testRow(Card{Rank::King, TS}, Card{Rank::Eight, Suit::Spades}, 1, Card{Rank::Eight, Suit::Spades});
    }

    SECTION("5. ST + BN -> bid 1, lead the BN")
    {
        testRow(Card{Rank::Nine, TS}, Card{Rank::King, Suit::Spades}, 1, Card{Rank::King, Suit::Spades});
    }

    SECTION("6. ST + ST -> bid 1, lead lower ST")
    {
        testRow(Card{Rank::Ten, TS}, Card{Rank::Eight, TS}, 1, Card{Rank::Eight, TS});
    }

    SECTION("7. ST + SN -> bid 1, lead the SN")
    {
        testRow(Card{Rank::Nine, TS}, Card{Rank::Eight, Suit::Spades}, 1, Card{Rank::Eight, Suit::Spades});
    }

    SECTION("8. BN + BN -> bid 1, lead lower BN")
    {
        testRow(Card{Rank::Ace, Suit::Spades}, Card{Rank::Queen, Suit::Spades}, 1, Card{Rank::Queen, Suit::Spades});
    }

    SECTION("9. BN + SN -> bid 1, lead the SN")
    {
        testRow(Card{Rank::Queen, Suit::Spades}, Card{Rank::Eight, Suit::Spades}, 1, Card{Rank::Eight, Suit::Spades});
    }

    SECTION("10. SN + SN -> bid 0, lead lower SN")
    {
        testRow(Card{Rank::Nine, Suit::Spades}, Card{Rank::Seven, Suit::Spades}, 0, Card{Rank::Seven, Suit::Spades});
    }
}

TEST_CASE("Cyborg bidding: section 4.3 non-leader in two-trick round", "[cyborg]")
{
    // raw = 1.0 * bigTrumps + 0.5 * smallTrumps (non-trumps score 0)
    // 4 players, trump is Diamonds.
    const std::vector<Card> hand{Card{Rank::Ace, Suit::Diamonds}, Card{Rank::Ace, Suit::Spades}};
    // Ace Diamonds = BT -> 1.0; Ace Spades = BN -> 0.0 -> raw = 1.0
    REQUIRE(rawPointsTwoTricks(hand, Suit::Diamonds, 4) == 1.0f);

    const std::vector<Card> hand2{Card{Rank::Eight, Suit::Diamonds}, Card{Rank::King, Suit::Spades}};
    // Eight Diamonds = ST -> 0.5; King Spades = BN -> 0.0 -> raw = 0.5
    REQUIRE(rawPointsTwoTricks(hand2, Suit::Diamonds, 4) == 0.5f);
}

TEST_CASE("Cyborg bidding: section 7.2 worked example mid-round", "[cyborg]")
{
    // Hand: A spades, 9 spades, K hearts, 8 hearts, 7 diamonds.
    // 4 players, 5 tricks, trump spades (turn-up 8 spades), third to bid.
    // unseen = 32 - 1 (turn-up) - 5 (mine) = 26; handsOutstanding H = 3 * 5 = 15.
    const Card turnUp{Rank::Eight, Suit::Spades};
    const std::vector<Card> hand{
        Card{Rank::Ace, Suit::Spades}, Card{Rank::Nine, Suit::Spades},
        Card{Rank::King, Suit::Hearts}, Card{Rank::Eight, Suit::Hearts},
        Card{Rank::Seven, Suit::Diamonds}
    };

    common::RoundMemory mem;
    mem.initRound(4, 5, 2, 0, turnUp);
    const OddsContext ctx = makeOddsContext(mem, common::cardsToMask(hand, 4), Suit::Spades, 0.60f);

    auto pointsFor = [&](const Card& card) { return rawPointsMidRound(std::vector<Card>{card}, ctx); };

    // P(an opponent's five cards miss all k marked cards among the 26 unseen).
    constexpr float noVoidHearts = 1.0f - (15504.0f / 65780.0f);   // 1 - C(20,5)/C(26,5): 6 hearts unseen
    constexpr float noVoidDiamonds = 1.0f - (11628.0f / 65780.0f); // 1 - C(19,5)/C(26,5): 7 diamonds unseen

    SECTION("each card is priced by what could beat it")
    {
        // A spades: no higher trump exists. A certainty.
        REQUIRE(pointsFor(hand[0]) == 1.0f);

        // 9 spades: 10 J Q K of spades unseen above it -> hyper0(4, 26, 15) = C(22,15)/C(26,15)
        REQUIRE_THAT(pointsFor(hand[1]),
                     WithinAbs((11.0f * 10 * 9 * 8) / (26.0f * 25 * 24 * 23), 1e-5f));

        // K hearts: the ace above it, and three opponents who must all still hold hearts.
        REQUIRE_THAT(pointsFor(hand[2]),
                     WithinAbs((11.0f / 26.0f) * noVoidHearts * noVoidHearts * noVoidHearts, 1e-5f));

        // 8 hearts: 9 10 J Q A above it (the king is mine).
        REQUIRE_THAT(pointsFor(hand[3]),
                     WithinAbs((11.0f * 10 * 9 * 8 * 7) / (26.0f * 25 * 24 * 23 * 22) *
                                   noVoidHearts * noVoidHearts * noVoidHearts,
                               1e-5f));

        // 7 diamonds: all seven other diamonds above it.
        REQUIRE_THAT(pointsFor(hand[4]),
                     WithinAbs((11.0f * 10 * 9 * 8 * 7 * 6 * 5) / (26.0f * 25 * 24 * 23 * 22 * 21 * 20) *
                                   noVoidDiamonds * noVoidDiamonds * noVoidDiamonds,
                               1e-5f));

        // raw approx 1.21438
        REQUIRE_THAT(rawPointsMidRound(hand, ctx), WithinAbs(1.214384f, 1e-4f));
    }

    SECTION("it bids the count with the highest expected score, whatever the table has bid")
    {
        // The per-card odds above as independent trials. The ace is certain, so
        // exactly one trick carries most of the weight:
        // P(1) 0.790, P(2) 0.205, P(3) 0.005.
        const std::vector<double> dist = trickDistribution(cardWinOdds(hand, ctx), 5);
        REQUIRE(dist[0] == 0.0);
        REQUIRE_THAT(dist[1], WithinAbs(0.79049, 1e-4));
        REQUIRE_THAT(dist[2], WithinAbs(0.20465, 1e-4));
        REQUIRE_THAT(dist[3], WithinAbs(0.00485, 1e-4));

        // Expected scores: bid 0 -1.214, bid 1 +4.529, bid 2 +0.637. So 1 - and
        // the bids already made do not move it, where the old rounding rule bid
        // 1 or 2 depending on them.
        CyborgKnobs knobs{};

        BidInputs inputs;
        inputs.hand = hand;
        inputs.playerCount = 4;
        inputs.trickCount = 5;
        inputs.trump = turnUp;
        inputs.biddingPosition = 2;
        inputs.odds = ctx;

        inputs.previousBidSum = 4;
        inputs.bidsInBiddingOrder = {2, 2};
        REQUIRE(chooseBid(inputs, knobs).bid == 1);

        inputs.previousBidSum = 1;
        inputs.bidsInBiddingOrder = {0, 1};
        REQUIRE(chooseBid(inputs, knobs).bid == 1);

        // Last to bid with 4 already claimed, 1 is barred. The next best is 2,
        // not 0: the ace makes a bid of 0 a certain miss.
        inputs.biddingPosition = 3;
        inputs.previousBidSum = 4;
        inputs.bidsInBiddingOrder = {2, 1, 1};
        inputs.forbiddenBet = 1;
        REQUIRE(chooseBid(inputs, knobs).bid == 2);
    }
}

TEST_CASE("Cyborg bidding: section 4.4 bids the most valuable count, not the rounded average", "[cyborg]")
{
    // Three cards at 0.2, 0.2 and 0.1 average half a trick. Rounding that average
    // at an under-subscribed table ceils it to 1; but no tricks at all is by far
    // the likeliest outcome, and a bid of 0 is the most valuable one to make.
    const std::vector<double> dist = trickDistribution({0.2f, 0.2f, 0.1f}, 3);
    REQUIRE_THAT(dist[0], WithinAbs(0.576, 1e-6));
    REQUIRE_THAT(dist[1], WithinAbs(0.352, 1e-6));
    REQUIRE_THAT(dist[2], WithinAbs(0.068, 1e-6));
    REQUIRE_THAT(dist[3], WithinAbs(0.004, 1e-6));

    // Expected scores: bid 0 +2.38, bid 1 +1.46, bid 2 -1.03.
    REQUIRE(expectedScoreBid(dist, std::nullopt) == 0);

    // The section 4.1 rounding of the same average, for contrast.
    CyborgKnobs literal{};
    literal.useExpectedRemaining = false;
    Rounding out = Rounding::Exact;
    REQUIRE(resolveFraction(0.5f, 0, 3, 3, 4, literal, out) == 1);

    // Barred from 0, the next best is 1.
    REQUIRE(expectedScoreBid(dist, 0u) == 1);
}

TEST_CASE("Cyborg bidding: section 4.4 a card is worth less at a bigger table", "[cyborg]")
{
    // The reason section 4.4 is priced from the odds: more opponents means more
    // hands a higher card can be in and more chances someone is void. The old
    // half-point rule scored these cards the same at every table size.
    const Card turnUp{Rank::Seven, Suit::Spades};
    const Card aceOfSpades{Rank::Ace, Suit::Spades};
    const Card kingOfHearts{Rank::King, Suit::Hearts};
    const std::vector<Card> hand{aceOfSpades, kingOfHearts, Card{Rank::Eight, Suit::Clubs}};

    auto contextFor = [&](unsigned int players) {
        common::RoundMemory mem;
        mem.initRound(players, 3, 0, 0, turnUp);
        return makeOddsContext(mem, common::cardsToMask(hand, players), Suit::Spades, 0.60f);
    };

    const OddsContext four = contextFor(4);
    const OddsContext six = contextFor(6);

    // The top trump is certain at any table.
    REQUIRE(rawPointsMidRound(std::vector<Card>{aceOfSpades}, four) == 1.0f);
    REQUIRE(rawPointsMidRound(std::vector<Card>{aceOfSpades}, six) == 1.0f);

    // A side king falls in value as the table grows.
    const float kingAtFour = rawPointsMidRound(std::vector<Card>{kingOfHearts}, four);
    const float kingAtSix = rawPointsMidRound(std::vector<Card>{kingOfHearts}, six);
    REQUIRE(kingAtFour > kingAtSix);
    REQUIRE(kingAtSix > 0.0f);
}

namespace
{
// Worked example 7.3: A K J of hearts, A Q 9 of diamonds, 8 of spades, 7 of clubs.
std::vector<Card> workedExample73Hand()
{
    return {
        Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts}, Card{Rank::Jack, Suit::Hearts},
        Card{Rank::Ace, Suit::Diamonds}, Card{Rank::Queen, Suit::Diamonds}, Card{Rank::Nine, Suit::Diamonds},
        Card{Rank::Eight, Suit::Spades}, Card{Rank::Seven, Suit::Clubs}
    };
}
} // namespace

TEST_CASE("Cyborg bidding: section 7.3 worked example eight tricks", "[cyborg]")
{
    // 4 players, no trump.
    // Hearts: A (g=0 -> 1.0), K (g=0 -> 1.0), J (g=1 <= 2 -> 0.75) = 2.75
    // Diamonds: A (g=0 -> 1.0), Q (g=1 <= 1 -> 0.75), 9 (g=3, not credited) = 1.75
    // Spades: 8 (g=6) -> 0
    // Clubs: 7 (g=7) -> 0
    // Total raw = 4.50
    const std::vector<Card> hand = workedExample73Hand();

    const float raw = countTricksNoTrump(hand, 4, 0.75f, false);
    REQUIRE_THAT(raw, WithinAbs(4.50f, 1e-5f));

    // Per-suit checks
    const std::vector<Card> heartsOnly{
        Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts}, Card{Rank::Jack, Suit::Hearts}
    };
    REQUIRE_THAT(countTricksNoTrump(heartsOnly, 4, 0.75f, false), WithinAbs(2.75f, 1e-5f));

    const std::vector<Card> ak10Hearts{
        Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts}, Card{Rank::Ten, Suit::Hearts}
    };
    // Ten Hearts has g = 2 (Q, J), winnersSoFar = 2 -> credited -> 2.75
    REQUIRE_THAT(countTricksNoTrump(ak10Hearts, 4, 0.75f, false), WithinAbs(2.75f, 1e-5f));

    const std::vector<Card> akqjHearts{
        Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts},
        Card{Rank::Queen, Suit::Hearts}, Card{Rank::Jack, Suit::Hearts}
    };
    REQUIRE_THAT(countTricksNoTrump(akqjHearts, 4, 0.75f, false), WithinAbs(4.00f, 1e-5f));

    const std::vector<Card> aqDiamonds{
        Card{Rank::Ace, Suit::Diamonds}, Card{Rank::Queen, Suit::Diamonds}
    };
    REQUIRE_THAT(countTricksNoTrump(aqDiamonds, 4, 0.75f, false), WithinAbs(1.75f, 1e-5f));

    // Round leader bid
    BidInputs inputs;
    inputs.hand = hand;
    inputs.playerCount = 4;
    inputs.trickCount = 8;
    inputs.trump = std::nullopt;
    inputs.biddingPosition = 0;
    inputs.previousBidSum = 0;

    // The literal section 4.1 rule ceils the opener's 4.5 to 5...
    CyborgKnobs literalKnobs{};
    literalKnobs.useExpectedRemaining = false;
    REQUIRE(chooseBid(inputs, literalKnobs).bid == 5);

    // ...and the default, with useExpectedRemaining, floors it to 4.
    CyborgKnobs defaultKnobs{};
    REQUIRE(chooseBid(inputs, defaultKnobs).bid == 4);
}

TEST_CASE("Cyborg bidding: section 4.5 an uncredited card scores 0.5 only with exactly one stranger above it", "[cyborg]")
{
    SECTION("six players: a nine has five strangers above it and scores nothing")
    {
        // The old rule scored any big card 0.5, and at six players the nine is big.
        REQUIRE(countTricksNoTrump({Card{Rank::Nine, Suit::Spades}}, 6, 0.75f, false) == 0.0f);
    }

    SECTION("six players: a king under an unseen ace scores 0.5")
    {
        REQUIRE(countTricksNoTrump({Card{Rank::King, Suit::Spades}}, 6, 0.75f, false) == 0.5f);
    }

    SECTION("two players: K Q under an unseen ace both score 0.5")
    {
        // Deck J Q K A. The king has g = 1 (the ace); so does the queen, because
        // the king is mine. Neither is credited as a winner (winnersSoFar = 0).
        REQUIRE(countTricksNoTrump({Card{Rank::King, Suit::Hearts}, Card{Rank::Queen, Suit::Hearts}}, 2, 0.75f,
                                   false) == 1.0f);
    }
}

TEST_CASE("Cyborg bidding: section 4.5 the leader reads gapCredit and a follower gapCreditFollower", "[cyborg]")
{
    // The 7.3 hand carries two gap credits (J hearts, Q diamonds) on top of 3.0 of
    // certain winners. With the real 0.75 / 0.60 both totals (4.5, 4.2) round to
    // the same bid, so zero one knob at a time to make the choice visible:
    // a zeroed credit gives raw 3.0 exactly -> bid 3; a live 0.75 gives 4.5, which
    // the default useExpectedRemaining floors -> bid 4.
    BidInputs inputs;
    inputs.hand = workedExample73Hand();
    inputs.playerCount = 4;
    inputs.trickCount = 8;
    inputs.trump = std::nullopt;
    inputs.previousBidSum = 0;

    CyborgKnobs followerZeroed{};
    followerZeroed.gapCredit = 0.75f;
    followerZeroed.gapCreditFollower = 0.0f;

    CyborgKnobs leaderZeroed{};
    leaderZeroed.gapCredit = 0.0f;
    leaderZeroed.gapCreditFollower = 0.75f;

    SECTION("round leader")
    {
        inputs.biddingPosition = 0;
        inputs.bidsInBiddingOrder = {};
        REQUIRE(chooseBid(inputs, followerZeroed).bid == 4);
        REQUIRE(chooseBid(inputs, leaderZeroed).bid == 3);
    }

    SECTION("second to bid")
    {
        inputs.biddingPosition = 1;
        inputs.bidsInBiddingOrder = {0};
        REQUIRE(chooseBid(inputs, followerZeroed).bid == 3);
        REQUIRE(chooseBid(inputs, leaderZeroed).bid == 4);
    }
}

TEST_CASE("Cyborg bidding: section 4.6 barred bid stepping", "[cyborg]")
{
    // If forbidden matches bid:
    // Floored -> bid + 1
    // Ceiled  -> bid - 1
    // Exact   -> (bid > 0) ? bid - 1 : 1
    // Clamped to [0, R] - and if the clamp lands back on the barred bid, step the
    // other way instead.

    SECTION("floored steps up")
    {
        REQUIRE(stepOffForbidden(2, Rounding::Floored, 2, 5) == 3);
        // Up from R would clamp back onto the barred R, so it steps down.
        REQUIRE(stepOffForbidden(5, Rounding::Floored, 5, 5) == 4);
    }

    SECTION("ceiled steps down")
    {
        REQUIRE(stepOffForbidden(2, Rounding::Ceiled, 2, 5) == 1);
        // Down from 0 would clamp back onto the barred 0, so it steps up.
        REQUIRE(stepOffForbidden(0, Rounding::Ceiled, 0, 5) == 1);
    }

    SECTION("exact steps down when > 0, steps up to 1 from 0")
    {
        REQUIRE(stepOffForbidden(3, Rounding::Exact, 3, 5) == 2);
        REQUIRE(stepOffForbidden(0, Rounding::Exact, 0, 5) == 1);
    }

    SECTION("unforbidden bid is unchanged")
    {
        REQUIRE(stepOffForbidden(2, Rounding::Floored, 1, 5) == 2);
        REQUIRE(stepOffForbidden(2, Rounding::Ceiled, std::nullopt, 5) == 2);
    }

    SECTION("never returns the barred bid, never exceeds R")
    {
        for(unsigned int trickCount = 1; trickCount <= 8; ++trickCount)
        {
            for(unsigned int bid = 0; bid <= trickCount; ++bid)
            {
                for(const Rounding rounding : {Rounding::Exact, Rounding::Floored, Rounding::Ceiled})
                {
                    const int roundingIndex = static_cast<int>(rounding);
                    CAPTURE(trickCount, bid, roundingIndex);

                    const unsigned int stepped = stepOffForbidden(bid, rounding, bid, trickCount);
                    REQUIRE(stepped != bid);
                    REQUIRE(stepped <= trickCount);
                }
            }
        }
    }
}

TEST_CASE("Cyborg bidding: section 4.7 blind rounds", "[cyborg]")
{
    BidInputs inputs;
    inputs.hand = {};
    inputs.playerCount = 4;
    inputs.trickCount = 1;
    inputs.roundType = RoundType::Forehead;

    CyborgKnobs knobs{};

    SECTION("Forehead round with 0 legal -> bid 0")
    {
        inputs.forbiddenBet = 1;
        REQUIRE(chooseBid(inputs, knobs).bid == 0);
    }

    SECTION("Forehead round with 0 barred -> bid 1")
    {
        inputs.forbiddenBet = 0;
        REQUIRE(chooseBid(inputs, knobs).bid == 1);
    }

    SECTION("Hidden round with 0 legal -> bid 0")
    {
        inputs.roundType = RoundType::Hidden;
        inputs.forbiddenBet = 1;
        REQUIRE(chooseBid(inputs, knobs).bid == 0);
    }

    SECTION("Hidden round with 0 barred -> bid 1")
    {
        inputs.roundType = RoundType::Hidden;
        inputs.forbiddenBet = 0;
        REQUIRE(chooseBid(inputs, knobs).bid == 1);
    }
}
