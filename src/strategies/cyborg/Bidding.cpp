#include <romanian_whist/strategies/cyborg/Bidding.h>

#include <romanian_whist/strategies/common/Hypergeometric.h>
#include <romanian_whist/strategies/cyborg/Halves.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::cyborg
{

namespace
{
constexpr float THRESHOLD_ONE_TRICK = 6.0f / 13.0f;

bool isInteger(float val)
{
    return std::fabs(val - std::round(val)) < 1e-4f;
}

unsigned int clampBid(int bid, unsigned int trickCount)
{
    if(bid < 0)
        return 0;
    if(static_cast<unsigned int>(bid) > trickCount)
        return trickCount;
    return static_cast<unsigned int>(bid);
}

// common::popcount returns a signed int; every count here is used as unsigned.
unsigned int count(common::Mask mask)
{
    return static_cast<unsigned int>(common::popcount(mask));
}
} // namespace

unsigned int resolveFraction(float raw, unsigned int previousBidSum, unsigned int trickCount,
                             unsigned int seatsStillToBid, unsigned int playerCount,
                             const CyborgKnobs& knobs, Rounding& out)
{
    if(isInteger(raw))
    {
        out = Rounding::Exact;
        return clampBid(static_cast<int>(std::round(raw)), trickCount);
    }

    float leftSide = static_cast<float>(previousBidSum) + raw;
    if(knobs.useExpectedRemaining && playerCount > 0)
    {
        leftSide += (static_cast<float>(seatsStillToBid) * static_cast<float>(trickCount)) /
                    static_cast<float>(playerCount);
    }

    if(leftSide > static_cast<float>(trickCount))
    {
        out = Rounding::Floored;
        return clampBid(static_cast<int>(std::floor(raw)), trickCount);
    }
    else
    {
        out = Rounding::Ceiled;
        return clampBid(static_cast<int>(std::ceil(raw)), trickCount);
    }
}

unsigned int bidOneTrickAsLeader(float p)
{
    return (p > THRESHOLD_ONE_TRICK) ? 1u : 0u;
}

unsigned int bidOneTrick(const BidInputs& inputs, const CyborgKnobs& /*knobs*/)
{
    if(inputs.hand.empty())
        return (inputs.forbiddenBet && *inputs.forbiddenBet == 0) ? 1u : 0u;

    const Card& card = inputs.hand.front();

    // Position 0: First / round leader
    if(inputs.biddingPosition == 0)
    {
        const common::CardId cid = common::cardToId(card, inputs.playerCount);
        const float p = pLeadWins(cid, inputs.odds);
        return bidOneTrickAsLeader(p);
    }

    // Position N-1: Last
    if(inputs.biddingPosition + 1 >= inputs.playerCount)
    {
        if(inputs.forbiddenBet && *inputs.forbiddenBet == 0)
            return 1u;
        return 0u;
    }

    // Middle seats
    const std::optional<Suit> trumpSuit = inputs.trump ? std::optional(inputs.trump->suit) : std::nullopt;
    const bool isTrump = trumpSuit && (card.suit == *trumpSuit);

    if(!isTrump)
        return 0u;

    // Holding a trump. Who has bid 1 so far? bidsInBiddingOrder[0] is the opener.
    bool openerBid1 = false;
    bool someoneAfterOpenerBid1 = false;

    for(std::size_t i = 0; i < inputs.bidsInBiddingOrder.size(); ++i)
    {
        if(inputs.bidsInBiddingOrder[i] != 1)
            continue;

        if(i == 0)
            openerBid1 = true;
        else
            someoneAfterOpenerBid1 = true;
    }

    // A middle seat's 1 is evidence about the cards, so back only the arithmetic.
    if(someoneAfterOpenerBid1)
    {
        const common::CardId cid = common::cardToId(card, inputs.playerCount);
        const float p = pLeadWins(cid, inputs.odds);
        return (p > THRESHOLD_ONE_TRICK) ? 1u : 0u;
    }

    // Only the opener bid 1: back a big trump, not a small one.
    if(openerBid1)
        return isBig(card, inputs.playerCount) ? 1u : 0u;

    // Nobody bid 1, and a one-trick bid is only ever 0 or 1: everyone before me
    // bid 0.
    return 1u;
}

BidResult bidTwoTricksAsLeader(const BidInputs& inputs)
{
    if(inputs.hand.size() < 2)
        return BidResult{0u, Rounding::Exact, std::nullopt};

    const std::optional<Suit> trumpSuit = inputs.trump ? std::optional(inputs.trump->suit) : std::nullopt;

    const Card& c1 = inputs.hand[0];
    const Card& c2 = inputs.hand[1];

    const Category cat1 = categorise(c1, inputs.playerCount, trumpSuit);
    const Category cat2 = categorise(c2, inputs.playerCount, trumpSuit);

    // Helpers to compare ranks within the same category / suit
    auto higherCard = [](const Card& a, const Card& b) -> Card {
        return (static_cast<int>(a.rank) >= static_cast<int>(b.rank)) ? a : b;
    };
    auto lowerCard = [](const Card& a, const Card& b) -> Card {
        return (static_cast<int>(a.rank) <= static_cast<int>(b.rank)) ? a : b;
    };

    BidResult res{};
    res.rounding = Rounding::Exact;

    // Helper to order two categories consistently
    const bool cat1IsBigTrump = (cat1 == Category::BigTrump);
    const bool cat2IsBigTrump = (cat2 == Category::BigTrump);
    const bool cat1IsSmallTrump = (cat1 == Category::SmallTrump);
    const bool cat2IsSmallTrump = (cat2 == Category::SmallTrump);
    const bool cat1IsBigNonTrump = (cat1 == Category::BigNonTrump);
    const bool cat2IsBigNonTrump = (cat2 == Category::BigNonTrump);
    const bool cat1IsSmallNonTrump = (cat1 == Category::SmallNonTrump);
    const bool cat2IsSmallNonTrump = (cat2 == Category::SmallNonTrump);

    // 1. BT + BT -> bid 2, lead higher BT
    if(cat1IsBigTrump && cat2IsBigTrump)
    {
        res.bid = 2;
        res.plannedLead = higherCard(c1, c2);
        return res;
    }

    // 2. BT + BN -> bid 2, lead the BT
    if((cat1IsBigTrump && cat2IsBigNonTrump) || (cat2IsBigTrump && cat1IsBigNonTrump))
    {
        res.bid = 2;
        res.plannedLead = cat1IsBigTrump ? c1 : c2;
        return res;
    }

    // 3. BT + ST -> bid 1, lead the ST
    if((cat1IsBigTrump && cat2IsSmallTrump) || (cat2IsBigTrump && cat1IsSmallTrump))
    {
        res.bid = 1;
        res.plannedLead = cat1IsSmallTrump ? c1 : c2;
        return res;
    }

    // 4. BT + SN -> bid 1, lead the SN
    if((cat1IsBigTrump && cat2IsSmallNonTrump) || (cat2IsBigTrump && cat1IsSmallNonTrump))
    {
        res.bid = 1;
        res.plannedLead = cat1IsSmallNonTrump ? c1 : c2;
        return res;
    }

    // 5. ST + BN -> bid 1, lead the BN
    if((cat1IsSmallTrump && cat2IsBigNonTrump) || (cat2IsSmallTrump && cat1IsBigNonTrump))
    {
        res.bid = 1;
        res.plannedLead = cat1IsBigNonTrump ? c1 : c2;
        return res;
    }

    // 6. ST + ST -> bid 1, lead the lower ST
    if(cat1IsSmallTrump && cat2IsSmallTrump)
    {
        res.bid = 1;
        res.plannedLead = lowerCard(c1, c2);
        return res;
    }

    // 7. ST + SN -> bid 1, lead the SN
    if((cat1IsSmallTrump && cat2IsSmallNonTrump) || (cat2IsSmallTrump && cat1IsSmallNonTrump))
    {
        res.bid = 1;
        res.plannedLead = cat1IsSmallNonTrump ? c1 : c2;
        return res;
    }

    // 8. BN + BN -> bid 1, lead lower BN
    if(cat1IsBigNonTrump && cat2IsBigNonTrump)
    {
        res.bid = 1;
        res.plannedLead = lowerCard(c1, c2);
        return res;
    }

    // 9. BN + SN -> bid 1, lead the SN
    if((cat1IsBigNonTrump && cat2IsSmallNonTrump) || (cat2IsBigNonTrump && cat1IsSmallNonTrump))
    {
        res.bid = 1;
        res.plannedLead = cat1IsSmallNonTrump ? c1 : c2;
        return res;
    }

    // 10. SN + SN -> bid 0, lead lower SN
    if(cat1IsSmallNonTrump && cat2IsSmallNonTrump)
    {
        res.bid = 0;
        res.plannedLead = lowerCard(c1, c2);
        return res;
    }

    // Catch-all
    res.bid = 0;
    res.plannedLead = c1;
    return res;
}

float rawPointsTwoTricks(const std::vector<Card>& hand, std::optional<Suit> trumpSuit,
                         unsigned int playerCount)
{
    float raw = 0.0f;
    for(const Card& c : hand)
    {
        if(trumpSuit && c.suit == *trumpSuit)
        {
            if(isBig(c, playerCount))
                raw += 1.0f;
            else
                raw += 0.5f;
        }
    }
    return raw;
}

std::vector<float> cardWinOdds(const std::vector<Card>& hand, const OddsContext& odds)
{
    const common::DeckProfile& profile = odds.profile();
    const unsigned int unseenCount = count(odds.unseen);

    // beaters() with trump switched off answers "which unseen cards of the SAME
    // suit rank above this one" - for a trump that is its whole beater set, and
    // for a plain card the ruff is priced separately below, as a void. Going
    // through beaters() rather than maskAbove() keeps its CardId bounds check.
    OddsContext sameSuitOnly = odds;
    sameSuitOnly.trumpSuit = std::nullopt;

    std::vector<float> cardOdds;
    cardOdds.reserve(hand.size());

    for(const Card& card : hand)
    {
        const common::CardId c = common::cardToId(card, odds.playerCount);
        const unsigned int suit = common::cardSuit(c, profile.ranksPerSuit);

        // No opponent holds a higher card of the suit.
        float p = common::hyper0(count(beaters(c, odds.unseen, sameSuitOnly)), unseenCount,
                                 odds.handsOutstanding);

        // A plain card also needs every opponent to still hold its suit: one who
        // is void can ruff it. Each opponent's hand is priced as its own draw.
        const bool isTrump = odds.trumpSuit && suit == static_cast<unsigned int>(*odds.trumpSuit);
        if(!isTrump)
        {
            const unsigned int suitUnseen = count(odds.unseen & common::maskSuit(suit, profile));
            for(unsigned int seat = 0; seat < odds.playerCount; ++seat)
            {
                if(seat == odds.mySeat || odds.handSize[seat] == 0)
                    continue;

                p *= 1.0f - common::hyper0(suitUnseen, unseenCount, odds.handSize[seat]);
            }
        }

        cardOdds.push_back(p);
    }

    return cardOdds;
}

float rawPointsMidRound(const std::vector<Card>& hand, const OddsContext& odds)
{
    // Summed in hand order, exactly as before the odds were split out, so the
    // expected trick count is bit-for-bit what it was.
    float raw = 0.0f;
    for(const float p : cardWinOdds(hand, odds))
    {
        raw += p;
    }
    return raw;
}

std::vector<double> trickDistribution(const std::vector<float>& cardOdds, unsigned int trickCount)
{
    // dist[t] = P(exactly t tricks so far), one card folded in at a time: each
    // either wins (t moves up by one) or does not.
    std::vector<double> dist(trickCount + 1, 0.0);
    dist[0] = 1.0;

    unsigned int cards = 0;
    for(const float odds : cardOdds)
    {
        const double p = odds;
        for(int t = static_cast<int>(std::min(cards + 1, trickCount)); t >= 0; --t)
        {
            dist[t] = dist[t] * (1.0 - p) + (t > 0 ? dist[t - 1] * p : 0.0);
        }
        ++cards;
    }

    return dist;
}

unsigned int expectedScoreBid(const std::vector<double>& distribution, std::optional<unsigned int> forbidden)
{
    if(distribution.empty())
        return 0u;

    const auto trickCount = static_cast<unsigned int>(distribution.size() - 1);

    std::optional<unsigned int> best;
    double bestScore = 0.0;

    for(unsigned int bid = 0; bid <= trickCount; ++bid)
    {
        if(forbidden && bid == *forbidden)
            continue;

        double expected = 0.0;
        for(unsigned int tricks = 0; tricks <= trickCount; ++tricks)
        {
            const double score = (tricks == bid) ? 5.0 + bid
                                                 : -std::fabs(static_cast<double>(bid) - static_cast<double>(tricks));
            expected += distribution[tricks] * score;
        }

        // Strictly better by more than rounding noise, so a tie keeps the lower
        // bid - the one a ducking hand can more easily arrange.
        if(!best || expected > bestScore + 1e-12)
        {
            best = bid;
            bestScore = expected;
        }
    }

    // R >= 1 and only one bid is ever barred, so something is always allowed.
    return best.value_or(0u);
}

float countTricksNoTrump(const std::vector<Card>& hand, unsigned int playerCount,
                        float gapCredit, bool useLengthCredit)
{
    const common::DeckProfile& profile = common::getDeckProfile(playerCount);

    // Group cards by suit
    std::vector<Card> bySuit[4];
    for(const Card& c : hand)
    {
        bySuit[static_cast<unsigned int>(c.suit)].push_back(c);
    }

    float raw = 0.0f;

    for(unsigned int s = 0; s < 4; ++s)
    {
        auto& suitCards = bySuit[s];
        if(suitCards.empty())
            continue;

        // Sort high to low by rank
        std::sort(suitCards.begin(), suitCards.end(), [](const Card& a, const Card& b) {
            return static_cast<int>(a.rank) > static_cast<int>(b.rank);
        });

        unsigned int winnersSoFar = 0;
        unsigned int cardsCreditedInSuit = 0;

        for(std::size_t i = 0; i < suitCards.size(); ++i)
        {
            const Card& c = suitCards[i];

            // g = number of cards of this suit ranked above c that are NOT mine.
            // (ranksPerSuit - 1 - rankIndex) ranks sit above c, and because the
            // suit is sorted high to low, exactly i of them are in my hand.
            const unsigned int totalRanksAbove = (profile.ranksPerSuit - 1) - rankIndex(c, playerCount);
            const unsigned int g = totalRanksAbove - static_cast<unsigned int>(i);

            if(g == 0)
            {
                raw += 1.0f;
                winnersSoFar += 1;
                cardsCreditedInSuit += 1;
            }
            else if(g <= winnersSoFar)
            {
                raw += gapCredit;
                winnersSoFar += 1;
                cardsCreditedInSuit += 1;
            }
            else if(g == 1)
            {
                // One stranger above: a fair chance it falls first. Two or more
                // almost never do - a suit of 2N cards goes round only about twice
                // when every trick takes N of them.
                raw += 0.5f;
            }
        }

        if(useLengthCredit && playerCount > 1)
        {
            const unsigned int myCount = static_cast<unsigned int>(suitCards.size());
            const unsigned int others = profile.ranksPerSuit - myCount;
            // maxOtherLength ≈ ceil(others / (N - 1)) + 1
            const unsigned int denom = playerCount - 1;
            const unsigned int maxOtherLength = ((others + denom - 1) / denom) + 1;

            if(myCount > maxOtherLength + cardsCreditedInSuit)
            {
                raw += 0.5f * static_cast<float>(myCount - maxOtherLength - cardsCreditedInSuit);
            }
        }
    }

    return raw;
}

unsigned int stepOffForbidden(unsigned int bid, Rounding rounding,
                              std::optional<unsigned int> forbidden, unsigned int trickCount)
{
    if(!forbidden || bid != *forbidden)
        return bid;

    int stepped = static_cast<int>(bid);
    if(rounding == Rounding::Floored)
    {
        stepped += 1;
    }
    else if(rounding == Rounding::Ceiled)
    {
        stepped -= 1;
    }
    else // Exact
    {
        stepped = (bid > 0) ? static_cast<int>(bid) - 1 : 1;
    }

    const unsigned int clamped = clampBid(stepped, trickCount);
    if(clamped != *forbidden)
        return clamped;

    // The clamp put us straight back on the barred bid, which only happens at
    // the ends of [0, R]: floored at R, or ceiled at 0. Step the other way
    // instead. R >= 1 always, so that direction is always legal.
    return (bid > 0) ? bid - 1u : 1u;
}

BidResult chooseBid(const BidInputs& inputs, const CyborgKnobs& knobs)
{
    BidResult result{};

    // §4.7 Forehead and Hidden rounds
    if(inputs.roundType == RoundType::Forehead || inputs.roundType == RoundType::Hidden)
    {
        result.rounding = Rounding::Exact;
        result.bid = (inputs.forbiddenBet && *inputs.forbiddenBet == 0) ? 1u : 0u;
        result.plannedLead = std::nullopt;
        return result;
    }

    const unsigned int R = inputs.trickCount;
    const unsigned int N = inputs.playerCount;
    const unsigned int seatsStillToBid = (inputs.biddingPosition < N) ? (N - 1 - inputs.biddingPosition) : 0u;

    if(R == 1)
    {
        result.bid = bidOneTrick(inputs, knobs);
        result.rounding = Rounding::Exact;
        result.plannedLead = std::nullopt;
    }
    else if(R == 2)
    {
        if(inputs.biddingPosition == 0)
        {
            result = bidTwoTricksAsLeader(inputs);
        }
        else
        {
            const std::optional<Suit> trumpSuit = inputs.trump ? std::optional(inputs.trump->suit) : std::nullopt;
            const float raw = rawPointsTwoTricks(inputs.hand, trumpSuit, N);
            result.bid = resolveFraction(raw, inputs.previousBidSum, R, seatsStillToBid, N, knobs, result.rounding);
            result.plannedLead = std::nullopt;
        }
    }
    else if(R >= 3 && R <= 7)
    {
        // §4.4: bid the trick count with the highest expected score over the
        // distribution the per-card odds imply, rather than rounding their sum.
        // It never lands on the barred bid, so the step-off below leaves it be.
        const std::vector<double> distribution = trickDistribution(cardWinOdds(inputs.hand, inputs.odds), R);
        result.bid = expectedScoreBid(distribution, inputs.forbiddenBet);
        result.rounding = Rounding::Exact;
        result.plannedLead = std::nullopt;
    }
    else // R == 8
    {
        const float gap = (inputs.biddingPosition == 0) ? knobs.gapCredit : knobs.gapCreditFollower;
        const float raw = countTricksNoTrump(inputs.hand, N, gap, knobs.useLengthCredit);
        result.bid = resolveFraction(raw, inputs.previousBidSum, R, seatsStillToBid, N, knobs, result.rounding);
        result.plannedLead = std::nullopt;
    }

    // §4.6 Step off forbidden bet if needed
    result.bid = stepOffForbidden(result.bid, result.rounding, inputs.forbiddenBet, R);

    return result;
}

} // namespace romanian_whist::cyborg
