#include <romanian_whist/strategies/cyborg/Plan.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace romanian_whist::cyborg
{

namespace
{
struct ScoredCard
{
    common::CardId id = 0;
    float score = 0.0f;
};

// Every card in the hand with its pLeadWins score, best first.
//
// Ties break on the card id, which makes the order total rather than weak. The
// heuristics deliberately leave their ordering weak and let input position
// settle ties (see tests/TrickHeuristicsTests.cpp); a plan is rebuilt from a
// MASK, which has no input order to lean on, so the tie rule has to be in the
// comparator or the plan would depend on nothing at all.
std::vector<ScoredCard> scoreHand(common::Mask hand, const OddsContext& odds,
                                  const ScoreCache& scores)
{
    std::vector<ScoredCard> scored;
    scored.reserve(static_cast<std::size_t>(common::popcount(hand)));

    common::Mask rest = hand;
    while(rest != 0)
    {
        const auto id = static_cast<common::CardId>(std::countr_zero(rest));
        rest &= rest - 1ULL;

        // ScoreCache::of() falls through to pLeadWins() for a card it does not
        // hold, and pLeadWins() validates the id through beaters() - the only
        // CardId bounds check in the odds kit.
        scored.push_back(ScoredCard{id, scores.of(id, odds)});
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredCard& a, const ScoredCard& b) {
        if(a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    });

    return scored;
}
} // namespace

Plan buildPlan(common::Mask hand, unsigned int bet, unsigned int tricksWon,
               const OddsContext& odds, const CyborgKnobs& knobs, const ScoreCache* scores)
{
    ScoreCache owned;
    if(scores == nullptr)
    {
        owned = makeScoreCache(hand, odds);
        scores = &owned;
    }

    Plan plan;

    // Clamped rather than signed: a hand that has already overshot its bid owes
    // nothing more, and the overshoot is water under the bridge.
    plan.need = (bet > tricksWon) ? bet - tricksWon : 0u;

    const std::vector<ScoredCard> scored = scoreHand(hand, odds, *scores);
    const auto rem = static_cast<unsigned int>(scored.size());

    float total = 0.0f;
    for(std::size_t i = 0; i < scored.size(); ++i)
    {
        total += scored[i].score;

        if(i < plan.need)
        {
            plan.winners |= common::cardBit(scored[i].id);
            plan.expected += scored[i].score;
        }
    }

    // How far the WHOLE hand is above the bid, and what SHED reacts to: a hand
    // that bid 1 and holds two aces is one trick from a miss. A local rather than
    // a field on Plan - the mode below is the only thing that reads it, and
    // publishing it would invite a caller to re-derive the mode decision instead
    // of trusting `mode`.
    const float surplus = total - static_cast<float>(plan.need);

    if(plan.need == 0)
    {
        plan.mode = Mode::Duck;
    }
    else if(plan.need >= rem || plan.expected < static_cast<float>(plan.need) - knobs.slack)
    {
        // Either every remaining trick is needed, or the hand is behind what it
        // promised. Both mean: take what can be taken.
        plan.mode = Mode::Take;
    }
    else if(surplus > knobs.slack)
    {
        plan.mode = Mode::Shed;
    }
    else
    {
        plan.mode = Mode::Balance;
    }

    return plan;
}

float feasibility(common::Mask hand, unsigned int n, const OddsContext& odds,
                  const ScoreCache* scores)
{
    ScoreCache owned;
    if(scores == nullptr)
    {
        owned = makeScoreCache(hand, odds);
        scores = &owned;
    }

    const std::vector<ScoredCard> scored = scoreHand(hand, odds, *scores);

    float sum = 0.0f;
    for(std::size_t i = 0; i < scored.size() && i < n; ++i)
    {
        sum += scored[i].score;
    }

    return std::fabs(sum - static_cast<float>(n));
}

} // namespace romanian_whist::cyborg
