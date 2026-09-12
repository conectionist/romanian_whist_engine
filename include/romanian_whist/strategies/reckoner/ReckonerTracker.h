#ifndef ROMANIAN_WHIST_RECKONER_TRACKER_H
#define ROMANIAN_WHIST_RECKONER_TRACKER_H

#include <romanian_whist/strategies/common/RoundMemory.h>
#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/Evaluator.h>

#include <array>
#include <optional>
#include <vector>

namespace romanian_whist::reckoner
{

// The round-memory core lives in strategies/common/ so that more than one
// strategy can count cards without two copies of the must-trump inference
// drifting apart. These aliases keep the names spelled `reckoner::` here.
using common::CompletedTrick;
using common::PrePlayState;
using common::TrickCard;
using common::UNBID;

struct SoftConstraint
{
    unsigned int player = 0;
    Mask excluded = 0;
    float conf = 0.8f;
};

struct OpponentModel
{
    float bias = 0.0f;
    float sigma = 1.2f;
    unsigned int sampleCount = 0;
    float sumResiduals = 0.0f;
    float sumSqResiduals = 0.0f;
    bool isDeterministic = false; // true if LowRisk or Ducking
};

// Round memory plus the two things only Reckoner does with it: soft "rank-gap"
// constraints inferred from how each opponent played, and a cross-round model
// of how each opponent bids.
//
// Both are guesses. The base class holds only what is certain, which is why it
// is the part worth sharing - see common::RoundMemory.
//
// initRound() and recordCardPlayed() extend their base versions rather than
// override them: they are not virtual, because nothing here is ever reached
// through a common::RoundMemory& and a vtable would buy nothing. Callers hold a
// ReckonerTracker, so they get these.
class ReckonerTracker : public common::RoundMemory
{
public:
    // "p holds none of `excluded`", believed with probability conf. Consumed by
    // the Sampler when it weights candidate deals; nothing else reads them.
    std::vector<SoftConstraint> constraints;

    // Kept across the whole game, unlike everything in the base.
    std::array<OpponentModel, 6> opponentModels{};

    std::array<int, 6> totalScores{};
    unsigned int scoreLeaderSeat = 0;
    unsigned int myConsecutiveWins = 0;
    unsigned int myConsecutiveLosses = 0;

    // 0 switches the soft inference off entirely - no constraints are recorded
    // and applyRankGapInferences() returns before it allocates.
    float rankGapConfScale = 1.0f;

    EvalContext initialContext{};

    ReckonerTracker() = default;

    void initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                   unsigned int openerSeat, std::optional<Card> trump);

    PrePlayState recordCardPlayed(unsigned int seat, const Card& card);

    void recordRoundEnd(const std::vector<int>& roundScores, const std::vector<int>& runningTotals);

    EvalContext makeCurrentEvalContext() const;

private:
    void applyRankGapInferences(unsigned int p, CardId c, CardId bestBefore,
                                unsigned int bestHolderBefore, std::optional<Suit> leadSuit);
};

} // namespace romanian_whist::reckoner

#endif
