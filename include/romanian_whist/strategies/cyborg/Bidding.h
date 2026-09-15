#ifndef ROMANIAN_WHIST_CYBORG_BIDDING_H
#define ROMANIAN_WHIST_CYBORG_BIDDING_H

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/strategies/cyborg/CyborgKnobs.h>
#include <romanian_whist/strategies/cyborg/Odds.h>

#include <optional>
#include <vector>

namespace romanian_whist::cyborg
{

enum class Rounding
{
    Exact,
    Floored,
    Ceiled
};

struct BidInputs
{
    std::vector<Card> hand; // leave empty for Forehead/Hidden (§4.7)
    unsigned int playerCount = 4;
    unsigned int trickCount = 1;
    std::optional<Card> trump; // from BetContext - authoritative
    RoundType roundType = RoundType::Normal;
    unsigned int biddingPosition = 0; // 0 = round leader, N-1 = last
    unsigned int previousBidSum = 0;
    std::vector<int> bidsInBiddingOrder; // bids already placed, leader first
    std::optional<unsigned int> forbiddenBet;
    OddsContext odds; // for the probability rules
};

struct BidResult
{
    unsigned int bid = 0;
    Rounding rounding = Rounding::Exact;
    std::optional<Card> plannedLead; // §4.3 only
};

// §4.1. Resolves a fractional raw point total by comparing against remaining tricks.
unsigned int resolveFraction(float raw, unsigned int previousBidSum, unsigned int trickCount,
                             unsigned int seatsStillToBid, unsigned int playerCount,
                             const CyborgKnobs& knobs, Rounding& out);

// §4.2. Round leader in 1-trick round. Bid 1 iff p > 6/13.
unsigned int bidOneTrickAsLeader(float p);

// §4.2. Complete 1-trick bidding logic (leader, middle seats table, last seat).
unsigned int bidOneTrick(const BidInputs& inputs, const CyborgKnobs& knobs);

// §4.3. Two-trick rounds as round leader: 10-row lookup table.
BidResult bidTwoTricksAsLeader(const BidInputs& inputs);

// §4.3. Raw points for 2-trick rounds when not the leader:
// raw = 1.0 * bigTrumps + 0.5 * smallTrumps (non-trumps score 0).
float rawPointsTwoTricks(const std::vector<Card>& hand, std::optional<Suit> trumpSuit,
                         unsigned int playerCount);

// §4.4. Raw points for 3- to 7-trick rounds. Each card scores the probability
// that nothing an opponent holds beats it: no opponent holds a higher card of its
// suit, and - for a plain card - no opponent is void in that suit to ruff it.
// Priced from `odds`, so it falls as the table grows. Throws
// std::invalid_argument for a card outside the deck.
float rawPointsMidRound(const std::vector<Card>& hand, const OddsContext& odds);

// §4.5. Raw points for 8-trick rounds (no trump). A card that is not credited as
// a winner scores 0.5 only when exactly one card it does not hold ranks above it.
float countTricksNoTrump(const std::vector<Card>& hand, unsigned int playerCount,
                        float gapCredit, bool useLengthCredit);

// §4.6. Step against the rounding direction if bid hits forbiddenBet. Never
// returns `forbidden`: where the clamp to [0, R] would land back on it, the step
// goes the other way. Precondition: trickCount >= 1.
unsigned int stepOffForbidden(unsigned int bid, Rounding rounding,
                              std::optional<unsigned int> forbidden, unsigned int trickCount);

// Master dispatcher for §4 bidding.
BidResult chooseBid(const BidInputs& inputs, const CyborgKnobs& knobs);

} // namespace romanian_whist::cyborg

#endif
