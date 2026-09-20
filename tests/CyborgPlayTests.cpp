#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/cyborg/Plan.h>
#include <romanian_whist/strategies/cyborg/Play.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::cyborg;

// Decision fixtures, in the style of tests/TrickHeuristicsTests.cpp: one
// position, one expected card, and a comment saying which rule is on trial.
//
// OddsContext is a plain aggregate on purpose (section 3), so these build the
// exact position they want with no engine, no memory and no strategy in sight.
// That also means the fixtures can hold positions a real deal could not - a
// two-card hand at a table whose opponents hold two cards each is fine here,
// and it keeps the arithmetic small enough to check by hand.

namespace
{
OddsContext makeContext(unsigned int playerCount, const std::vector<Card>& unseen,
                        std::optional<Suit> trump, unsigned int handsOutstanding,
                        unsigned int myHandSize)
{
    OddsContext ctx;
    ctx.playerCount = playerCount;
    ctx.mySeat = 0;
    ctx.trumpSuit = trump;
    ctx.unseen = common::cardsToMask(unseen, playerCount);
    ctx.handsOutstanding = handsOutstanding;
    ctx.handSize[0] = myHandSize;

    for(unsigned int seat = 1; seat < playerCount; ++seat)
    {
        ctx.handSize[seat] = handsOutstanding / (playerCount - 1);
    }

    ctx.duckPropensity = 0.60f;
    return ctx;
}

PlaySituation leading(const std::vector<Card>& hand, std::optional<Card> trump,
                      unsigned int playerCount = 4)
{
    PlaySituation situation;
    situation.hand = hand;
    situation.legal = hand; // leading, so every card is legal
    situation.trump = trump;
    situation.playerCount = playerCount;
    return situation;
}

PlaySituation following(const std::vector<Card>& hand, const std::vector<Card>& legal,
                        const std::vector<Card>& playedCards, Suit leadSuit,
                        std::optional<Card> trump, unsigned int playerCount = 4)
{
    PlaySituation situation;
    situation.hand = hand;
    situation.legal = legal;
    situation.playedCards = playedCards;
    situation.leadSuit = leadSuit;
    situation.trump = trump;
    situation.playerCount = playerCount;
    return situation;
}

const Card QueenHearts{Rank::Queen, Suit::Hearts};
const Card SevenHearts{Rank::Seven, Suit::Hearts};
const Card KingHearts{Rank::King, Suit::Hearts};
const Card TenHearts{Rank::Ten, Suit::Hearts};
} // namespace

TEST_CASE("Cyborg play: worked example 7.4, ducking on lead throws the dearer safe card", "[cyborg]")
{
    // DUCK, on lead, holding Q hearts and 7 hearts. Nothing is dead, so a beater
    // that exists is certainly in a hand and hyper0 returns 0 for both cards.
    const std::vector<Card> hand{QueenHearts, SevenHearts};
    const CyborgKnobs knobs{};

    SECTION("with only the king live, both are safe, so lead the queen")
    {
        // The king's holder must follow suit and must therefore beat whatever I
        // lead: both cards score exactly 0. The queen is the one I would struggle
        // to lose later, so it goes now.
        const OddsContext ctx = makeContext(4,
                                            {KingHearts, Card{Rank::Eight, Suit::Spades},
                                             Card{Rank::Nine, Suit::Spades}, Card{Rank::Ten, Suit::Spades},
                                             Card{Rank::Jack, Suit::Spades}, Card{Rank::Queen, Suit::Spades}},
                                            std::nullopt, 6, 2);

        const Plan plan = buildPlan(common::cardsToMask(hand, 4), 0, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Duck);

        REQUIRE(choosePlay(leading(hand, std::nullopt), plan, ctx, knobs) == QueenHearts);
    }

    SECTION("with the king and the ten live, the queen is a real risk, so lead the seven")
    {
        // Now one player may hold both the king and the ten and duck with the
        // ten, so the queen can lose the trick it was meant to lose safely.
        const OddsContext ctx = makeContext(4,
                                            {KingHearts, TenHearts, Card{Rank::Eight, Suit::Spades},
                                             Card{Rank::Nine, Suit::Spades}, Card{Rank::Ten, Suit::Spades},
                                             Card{Rank::Jack, Suit::Spades}},
                                            std::nullopt, 6, 2);

        const Plan plan = buildPlan(common::cardsToMask(hand, 4), 0, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Duck);

        REQUIRE(choosePlay(leading(hand, std::nullopt), plan, ctx, knobs) == SevenHearts);
    }
}

TEST_CASE("Cyborg play: DUCK throws the biggest safe card and TAKE the smallest", "[cyborg]")
{
    // THE ASYMMETRY, from one position: the same two cards, the same trick, and
    // the mode is the only thing that differs. The design calls getting this
    // backwards the single most expensive bug available.
    const Card AceSpades{Rank::Ace, Suit::Spades};
    const Card SevenSpades{Rank::Seven, Suit::Spades};

    const std::vector<Card> hand{AceSpades, SevenSpades};
    const CyborgKnobs knobs{};

    // No trump and void in the lead suit, so both cards are legal discards and
    // neither of them can take the trick: one `safe` list, read from both ends.
    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::Ace, Suit::Hearts}, Card{Rank::Queen, Suit::Hearts},
                                         Card{Rank::Jack, Suit::Hearts}, Card{Rank::Ten, Suit::Hearts},
                                         Card{Rank::King, Suit::Spades}, Card{Rank::Queen, Suit::Spades}},
                                        std::nullopt, 6, 2);

    const PlaySituation situation =
        following(hand, hand, {KingHearts}, Suit::Hearts, std::nullopt);

    const common::Mask handMask = common::cardsToMask(hand, 4);

    SECTION("DUCK sheds the ace")
    {
        const Plan plan = buildPlan(handMask, 0, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Duck);
        REQUIRE(choosePlay(situation, plan, ctx, knobs) == AceSpades);
    }

    SECTION("TAKE keeps the ace and throws the seven")
    {
        const Plan plan = buildPlan(handMask, 2, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Take);
        REQUIRE(choosePlay(situation, plan, ctx, knobs) == SevenSpades);
    }
}

TEST_CASE("Cyborg play: TAKE pays for a trick only when it is likely to hold", "[cyborg]")
{
    // Following in TAKE with two cards that both beat the current winner. The
    // nine is cheaper, but with seats still to act it probably will not survive;
    // holdThreshold is what stops it being spent for nothing.
    const Card NineHearts{Rank::Nine, Suit::Hearts};
    const Card AceHearts{Rank::Ace, Suit::Hearts};

    const std::vector<Card> hand{NineHearts, AceHearts};
    const CyborgKnobs knobs{};

    const OddsContext ctx = makeContext(4,
                                        {KingHearts, QueenHearts, Card{Rank::Jack, Suit::Hearts},
                                         TenHearts, Card{Rank::Eight, Suit::Spades},
                                         Card{Rank::Nine, Suit::Spades}},
                                        std::nullopt, 6, 2);

    const Plan plan = buildPlan(common::cardsToMask(hand, 4), 2, 0, ctx, knobs);
    REQUIRE(plan.mode == Mode::Take);

    SECTION("two seats still to act: the nine will not hold, so the ace takes it")
    {
        const PlaySituation situation =
            following(hand, hand, {SevenHearts}, Suit::Hearts, std::nullopt);

        REQUIRE(choosePlay(situation, plan, ctx, knobs) == AceHearts);
    }

    SECTION("last to act: nothing can take it away, so win as cheaply as possible")
    {
        const PlaySituation situation = following(
            hand, hand, {SevenHearts, Card{Rank::Eight, Suit::Hearts}, Card{Rank::Two, Suit::Clubs}},
            Suit::Hearts, std::nullopt);

        REQUIRE(choosePlay(situation, plan, ctx, knobs) == NineHearts);
    }
}

TEST_CASE("Cyborg play: SHED never leads a sure winner", "[cyborg]")
{
    // A sure winner cannot be shed by leading it - it simply wins. It has to be
    // saved for a trick somebody else opens.
    const Card AceSpades{Rank::Ace, Suit::Spades};
    const Card NineDiamonds{Rank::Nine, Suit::Diamonds};

    const std::vector<Card> hand{AceSpades, NineDiamonds};
    const CyborgKnobs knobs{};

    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::King, Suit::Diamonds}, Card{Rank::Queen, Suit::Diamonds},
                                         Card{Rank::Jack, Suit::Diamonds}, Card{Rank::Ten, Suit::Diamonds},
                                         Card{Rank::King, Suit::Spades}, Card{Rank::Queen, Suit::Spades}},
                                        std::nullopt, 6, 2);

    // Forced, rather than derived from a bid: this fixture is about the lead
    // rule, not about how the mode was reached.
    Plan plan;
    plan.need = 1;
    plan.mode = Mode::Shed;
    plan.winners = common::cardsToMask({AceSpades}, 4);
    plan.losers = common::cardsToMask({NineDiamonds}, 4);

    REQUIRE(isSureWinner(common::cardToId(AceSpades, 4), ctx));
    REQUIRE(choosePlay(leading(hand, std::nullopt), plan, ctx, knobs) == NineDiamonds);
}

TEST_CASE("Cyborg play: TAKE with no certainty leads low in its longest suit", "[cyborg]")
{
    // Leading a big card into an unknown table just feeds whoever holds the card
    // above it. Flush the guards with the cheapest card of the longest suit
    // instead, and give the big ones a second round of the suit to work in.
    const Card KingSpades{Rank::King, Suit::Spades};
    const Card NineSpades{Rank::Nine, Suit::Spades};
    const Card EightSpades{Rank::Eight, Suit::Spades};

    const std::vector<Card> hand{KingSpades, NineSpades, EightSpades};
    const CyborgKnobs knobs{};

    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::Ace, Suit::Spades}, Card{Rank::Queen, Suit::Spades},
                                         Card{Rank::Jack, Suit::Spades}, Card{Rank::Ten, Suit::Spades},
                                         Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts}},
                                        std::nullopt, 6, 3);

    const Plan plan = buildPlan(common::cardsToMask(hand, 4), 3, 0, ctx, knobs);
    REQUIRE(plan.mode == Mode::Take);

    REQUIRE(choosePlay(leading(hand, std::nullopt), plan, ctx, knobs) == EightSpades);
}

TEST_CASE("Cyborg play: the planned lead wins trick one of a two-trick round", "[cyborg]")
{
    // Section 4.3 chose the bid and the opening lead together; section 6.1 does
    // not get to second-guess it.
    const Card KingDiamonds{Rank::King, Suit::Diamonds};
    const Card EightSpades{Rank::Eight, Suit::Spades};

    const std::vector<Card> hand{KingDiamonds, EightSpades};
    const CyborgKnobs knobs{};

    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::Ace, Suit::Diamonds}, Card{Rank::Queen, Suit::Diamonds},
                                         Card{Rank::Ace, Suit::Spades}, Card{Rank::King, Suit::Spades}},
                                        Suit::Diamonds, 4, 2);

    Plan plan;
    plan.need = 1;
    plan.mode = Mode::Duck;
    plan.losers = common::cardsToMask(hand, 4);

    // Nothing here can win, so every card scores 0 and section 6.1's DUCK
    // tie-break would lead the dearest card, the king. The stored lead is the
    // eight, which is exactly how this fixture tells the two rules apart.
    REQUIRE(choosePlay(leading(hand, Card{Rank::Seven, Suit::Diamonds}), plan, ctx, knobs) ==
            KingDiamonds);

    SECTION("the stored lead overrides the mode's own choice")
    {
        PlaySituation situation = leading(hand, Card{Rank::Seven, Suit::Diamonds});
        situation.plannedLead = EightSpades;

        REQUIRE(choosePlay(situation, plan, ctx, knobs) == EightSpades);
    }

    SECTION("and is ignored once the round is past its first trick")
    {
        // Three cards in hand is not a two-trick round's opening lead, whatever
        // the stored card says, so the ducking rule chooses again.
        const std::vector<Card> longerHand{KingDiamonds, EightSpades, Card{Rank::Seven, Suit::Clubs}};
        PlaySituation later = leading(longerHand, Card{Rank::Seven, Suit::Diamonds});
        later.plannedLead = EightSpades;

        const Plan duckPlan = buildPlan(common::cardsToMask(longerHand, 4), 0, 0, ctx, knobs);
        REQUIRE(duckPlan.mode == Mode::Duck);
        REQUIRE(choosePlay(later, duckPlan, ctx, knobs) == KingDiamonds);
    }
}

TEST_CASE("Cyborg play: buildPlan picks the mode from the gap between bid and tricks", "[cyborg]")
{
    const std::vector<Card> hand{Card{Rank::Ace, Suit::Spades}, Card{Rank::King, Suit::Spades},
                                 Card{Rank::Eight, Suit::Hearts}, Card{Rank::Seven, Suit::Hearts}};
    const common::Mask handMask = common::cardsToMask(hand, 4);
    const CyborgKnobs knobs{};

    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::Queen, Suit::Spades}, Card{Rank::Jack, Suit::Spades},
                                         Card{Rank::Ace, Suit::Hearts}, Card{Rank::King, Suit::Hearts},
                                         Card{Rank::Queen, Suit::Hearts}, Card{Rank::Jack, Suit::Hearts},
                                         Card{Rank::Ace, Suit::Diamonds}, Card{Rank::King, Suit::Diamonds}},
                                        std::nullopt, 8, 4);

    SECTION("bid already met: DUCK, and no card is a winner")
    {
        const Plan plan = buildPlan(handMask, 2, 2, ctx, knobs);
        REQUIRE(plan.mode == Mode::Duck);
        REQUIRE(plan.need == 0);
        REQUIRE(plan.winners == 0);
        REQUIRE(plan.losers == handMask);
    }

    SECTION("every remaining trick is needed: TAKE")
    {
        const Plan plan = buildPlan(handMask, 4, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Take);
        REQUIRE(plan.need == 4);
    }

    SECTION("hand is worth more than the bid: SHED")
    {
        const Plan plan = buildPlan(handMask, 1, 0, ctx, knobs);
        REQUIRE(plan.mode == Mode::Shed);
        REQUIRE(plan.surplus > knobs.slack);
        REQUIRE(common::popcount(plan.winners) == 1);
    }

    SECTION("overshooting the bid still owes nothing")
    {
        const Plan plan = buildPlan(handMask, 1, 3, ctx, knobs);
        REQUIRE(plan.need == 0);
        REQUIRE(plan.mode == Mode::Duck);
    }
}

TEST_CASE("Cyborg play: feasibility measures the distance from taking exactly n", "[cyborg]")
{
    // Section 6.3's yardstick, and note what it does NOT measure: only the n
    // best cards count, so a hand's surplus below that line is invisible here.
    // That is deliberate - BALANCE compares two futures for the SAME n.
    const std::vector<Card> twoAces{Card{Rank::Ace, Suit::Spades}, Card{Rank::Ace, Suit::Hearts}};
    const common::Mask acesMask = common::cardsToMask(twoAces, 4);

    const OddsContext ctx = makeContext(4,
                                        {Card{Rank::King, Suit::Spades}, Card{Rank::Queen, Suit::Spades},
                                         Card{Rank::King, Suit::Hearts}, Card{Rank::Queen, Suit::Hearts}},
                                        std::nullopt, 4, 2);

    // Two certainties fit a bid of two exactly, and either one of them fits a
    // bid of one exactly.
    REQUIRE(feasibility(acesMask, 2, ctx) == 0.0f);
    REQUIRE(feasibility(acesMask, 1, ctx) == 0.0f);

    // One certainty and one card that cannot win is a whole trick short of two.
    const std::vector<Card> aceAndJunk{Card{Rank::Ace, Suit::Spades}, Card{Rank::Seven, Suit::Hearts}};
    const common::Mask junkMask = common::cardsToMask(aceAndJunk, 4);

    REQUIRE(feasibility(junkMask, 2, ctx) == 1.0f);
    REQUIRE(feasibility(junkMask, 1, ctx) == 0.0f);
}

TEST_CASE("Cyborg play: section 6.3 compares the two hands the decision leaves behind", "[cyborg]")
{
    // BALANCE, following, able to win and not sure it should. The rule compares
    // the SHAPE of the two futures rather than the trick in front of it.
    const Card AceHearts{Rank::Ace, Suit::Hearts};
    const Card EightClubs{Rank::Eight, Suit::Clubs};
    const CyborgKnobs knobs{};

    SECTION("a tie ducks, because a taken trick cannot be given back")
    {
        // Taking leaves {8 clubs} needing 0 - a perfect fit. Ducking leaves the
        // ace needing 1 - also perfect. Equal, so the trick goes.
        const std::vector<Card> hand{AceHearts, EightClubs};

        const OddsContext ctx = makeContext(4,
                                            {QueenHearts, Card{Rank::Jack, Suit::Hearts}, TenHearts,
                                             Card{Rank::King, Suit::Clubs}},
                                            std::nullopt, 4, 2);

        Plan plan;
        plan.need = 1;
        plan.mode = Mode::Balance;
        plan.winners = common::cardsToMask({AceHearts}, 4);
        plan.losers = common::cardsToMask({EightClubs}, 4);

        const PlaySituation situation =
            following(hand, hand, {KingHearts}, Suit::Hearts, std::nullopt);

        REQUIRE(choosePlay(situation, plan, ctx, knobs) == EightClubs);
    }

    SECTION("taking wins when ducking would leave the bid on a card that may not deliver")
    {
        // The queen beats the jack that leads, but it is not certain: the king
        // and the ace are still out. Ducking leaves the whole bid resting on it,
        // which is a worse shape than taking the trick now and owing nothing.
        const std::vector<Card> hand{QueenHearts, EightClubs};

        const OddsContext ctx = makeContext(4,
                                            {KingHearts, AceHearts, TenHearts,
                                             Card{Rank::King, Suit::Clubs}},
                                            std::nullopt, 4, 2);

        Plan plan;
        plan.need = 1;
        plan.mode = Mode::Balance;
        plan.winners = common::cardsToMask({QueenHearts}, 4);
        plan.losers = common::cardsToMask({EightClubs}, 4);

        const PlaySituation situation =
            following(hand, hand, {Card{Rank::Jack, Suit::Hearts}}, Suit::Hearts, std::nullopt);

        REQUIRE(choosePlay(situation, plan, ctx, knobs) == QueenHearts);
    }
}

TEST_CASE("Cyborg play: worked example 7.3 finishes hearts before starting diamonds", "[cyborg]")
{
    // The design's "bid and play can disagree" risk, turned into a test. Section
    // 4.5 credited the jack of hearts because leading the ace and the king would
    // flush the queen; if play cashes the ace and switches suits, that credit was
    // a lie. So the leads are A hearts, K hearts, J hearts, then A diamonds -
    // even though from the king onward diamonds is the longer suit.
    //
    // The mode is forced rather than derived from a bid: this is a fixture for
    // section 6.1's TAKE branch, and mode selection is tested separately.
    const Card AceHearts{Rank::Ace, Suit::Hearts};
    const Card JackHearts{Rank::Jack, Suit::Hearts};
    const Card AceDiamonds{Rank::Ace, Suit::Diamonds};
    const Card QueenDiamonds{Rank::Queen, Suit::Diamonds};
    const Card NineDiamonds{Rank::Nine, Suit::Diamonds};
    const Card EightSpades{Rank::Eight, Suit::Spades};
    const Card SevenClubs{Rank::Seven, Suit::Clubs};

    const CyborgKnobs knobs{};

    auto leadWith = [&knobs](const std::vector<Card>& hand, const std::vector<Card>& unseen,
                             unsigned int handsOutstanding, std::optional<Suit> cashing) {
        const OddsContext ctx = makeContext(4, unseen, std::nullopt, handsOutstanding,
                                            static_cast<unsigned int>(hand.size()));

        Plan plan;
        plan.mode = Mode::Take;
        plan.need = 4;
        plan.losers = common::cardsToMask(hand, 4);

        PlaySituation situation = leading(hand, std::nullopt);
        situation.cashingSuit = cashing;

        return choosePlay(situation, plan, ctx, knobs);
    };

    // Trick one: the whole deck bar my eight cards is unseen.
    const std::vector<Card> hand1{AceHearts,     KingHearts,     JackHearts, AceDiamonds,
                                  QueenDiamonds, NineDiamonds,   EightSpades, SevenClubs};
    const std::vector<Card> unseen1{
        SevenHearts, Card{Rank::Eight, Suit::Hearts}, Card{Rank::Nine, Suit::Hearts}, TenHearts, QueenHearts,
        Card{Rank::Seven, Suit::Diamonds}, Card{Rank::Eight, Suit::Diamonds}, Card{Rank::Ten, Suit::Diamonds},
        Card{Rank::Jack, Suit::Diamonds}, Card{Rank::King, Suit::Diamonds},
        Card{Rank::Seven, Suit::Spades}, Card{Rank::Nine, Suit::Spades}, Card{Rank::Ten, Suit::Spades},
        Card{Rank::Jack, Suit::Spades}, Card{Rank::Queen, Suit::Spades}, Card{Rank::King, Suit::Spades},
        Card{Rank::Ace, Suit::Spades},
        Card{Rank::Eight, Suit::Clubs}, Card{Rank::Nine, Suit::Clubs}, Card{Rank::Ten, Suit::Clubs},
        Card{Rank::Jack, Suit::Clubs}, Card{Rank::Queen, Suit::Clubs}, Card{Rank::King, Suit::Clubs},
        Card{Rank::Ace, Suit::Clubs}};

    REQUIRE(leadWith(hand1, unseen1, 24, std::nullopt) == AceHearts);

    // Trick two: the queen, ten and nine of hearts fell under the ace, which is
    // the assumption section 4.5 made when it credited the jack.
    const std::vector<Card> hand2{KingHearts,  JackHearts,  AceDiamonds, QueenDiamonds,
                                  NineDiamonds, EightSpades, SevenClubs};
    std::vector<Card> unseen2 = unseen1;
    for(const Card& gone : {QueenHearts, TenHearts, Card{Rank::Nine, Suit::Hearts}})
    {
        unseen2.erase(std::find(unseen2.begin(), unseen2.end(), gone));
    }

    REQUIRE(leadWith(hand2, unseen2, 21, Suit::Hearts) == KingHearts);

    // Trick three: hearts is now SHORTER than diamonds, and the hint is the only
    // thing keeping the jack in front of the ace of diamonds.
    const std::vector<Card> hand3{JackHearts,  AceDiamonds, QueenDiamonds,
                                  NineDiamonds, EightSpades, SevenClubs};
    std::vector<Card> unseen3 = unseen2;
    for(const Card& gone : {SevenHearts, Card{Rank::Eight, Suit::Hearts}, Card{Rank::Seven, Suit::Spades}})
    {
        unseen3.erase(std::find(unseen3.begin(), unseen3.end(), gone));
    }

    REQUIRE(leadWith(hand3, unseen3, 18, Suit::Hearts) == JackHearts);
    REQUIRE(leadWith(hand3, unseen3, 18, std::nullopt) == AceDiamonds); // without it, the longest suit wins

    // Trick four: hearts is spent, so the hint has nothing to offer and the
    // longest-suit rule takes over.
    const std::vector<Card> hand4{AceDiamonds, QueenDiamonds, NineDiamonds, EightSpades, SevenClubs};
    std::vector<Card> unseen4 = unseen3;
    for(const Card& gone : {Card{Rank::Nine, Suit::Spades}, Card{Rank::Ten, Suit::Spades},
                            Card{Rank::Eight, Suit::Clubs}})
    {
        unseen4.erase(std::find(unseen4.begin(), unseen4.end(), gone));
    }

    REQUIRE(leadWith(hand4, unseen4, 15, Suit::Hearts) == AceDiamonds);
}

TEST_CASE("CyborgStrategy: the cashing suit follows this seat's own leads", "[cyborg]")
{
    // The hint section 6.1 needs is the only thing card play carries between
    // tricks, so it is worth pinning where it comes from: a card this seat LED,
    // and nothing else.
    CyborgStrategy strategy;
    strategy.onRoundStart(4, 2, std::nullopt, 0, 0);

    REQUIRE_FALSE(strategy.getCashingSuit().has_value());

    const std::vector<Card> hand{Card{Rank::Ace, Suit::Spades}, SevenHearts};
    const std::vector<Card> nothingPlayed{};
    const PlayContext leadContext{hand, nothingPlayed, std::nullopt, std::nullopt, 1, 0};

    const std::optional<Card> led = strategy.getBestChoice(leadContext);
    REQUIRE(led.has_value());
    REQUIRE(strategy.getCashingSuit() == led->suit);

    // Following somebody else's lead says nothing about which suit this hand is
    // working through, so the hint must not move.
    const Suit before = *strategy.getCashingSuit();
    const std::vector<Card> remaining{SevenHearts};
    const std::vector<Card> played{KingHearts};
    const PlayContext followContext{remaining, played, std::nullopt, Suit::Hearts, 1, 0};

    REQUIRE(strategy.getBestChoice(followContext).has_value());
    REQUIRE(strategy.getCashingSuit() == before);

    // And a new round starts from nothing.
    strategy.onRoundStart(4, 3, std::nullopt, 0, 0);
    REQUIRE_FALSE(strategy.getCashingSuit().has_value());
}

TEST_CASE("Cyborg play: a whole game of section 6 play is legal and needs no fallback", "[cyborg]")
{
    // The counterweight to every fixture above: the rules have to survive real
    // deals at every table size. The engine rejects an illegal card rather than
    // obeying it, so a game that runs to the end is the legality assertion, and
    // fallbacksTaken == 0 says section 6 answered every decision itself rather
    // than quietly handing them to the heuristic.
    for(unsigned int playerCount = 2; playerCount <= 6; ++playerCount)
    {
        for(std::uint32_t seed : {7001u, 7002u, 7003u})
        {
            cyborg::CyborgKnobs knobs{};
            knobs.useHeuristicBid = false;
            knobs.useHeuristicPlay = false; // the point of this test

            GameEngine engine;
            auto strategy = std::make_unique<CyborgStrategy>(knobs);
            CyborgStrategy* observer = strategy.get();
            strategy->setPlayerName("P0");
            engine.addObserver(observer);

            GameSetup setup = test::buildSetup(GameStructure::S_181,
                                               test::buildRoundRobinProviders(playerCount), seed);
            setup.seats[0] = SeatSetup{"P0", std::make_unique<AiMoveProvider>(std::move(strategy))};

            engine.start(std::move(setup));

            CAPTURE(playerCount, seed);
            REQUIRE_NOTHROW(engine.run());
            REQUIRE(engine.getStatus() == GameStatus::Finished);
            REQUIRE(observer->getFallbacksTaken() == 0);
        }
    }
}
