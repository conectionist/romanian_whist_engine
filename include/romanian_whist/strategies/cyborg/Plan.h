#ifndef ROMANIAN_WHIST_CYBORG_PLAN_H
#define ROMANIAN_WHIST_CYBORG_PLAN_H

#include <romanian_whist/strategies/cyborg/CyborgKnobs.h>
#include <romanian_whist/strategies/cyborg/Odds.h>

namespace romanian_whist::cyborg
{

// §5. What the hand is FOR: which cards are meant to win, which are meant to be
// thrown, and what the hand is trying to do about the gap between the bid and
// the tricks already taken.
//
// This header NAMES CARDS AND NEVER CHOOSES ONE. That split is the safety
// mechanism: everything here is a pure function of a hand mask and an
// OddsContext, so it can be tested without a legal-card list, a trick or an
// engine - and it cannot accidentally play a card that is not legal, because it
// does not play cards at all. Play.h does the choosing, from the legal list.
enum class Mode
{
    Duck,    // the bid is already met: never take another trick
    Take,    // must win the rest, or is behind schedule: win what can be won, cheaply
    Shed,    // the hand will take more than it bid: lose the surplus early
    Balance  // on schedule with slack: take tricks in the order that keeps the plan feasible
};

struct Plan
{
    common::Mask winners = 0; // the `need` best cards, by pLeadWins. Empty when need == 0.
    common::Mask losers = 0;  // everything else in the hand

    unsigned int need = 0; // bet - tricksWon, clamped at 0
    Mode mode = Mode::Duck;

    // What the plan expects the winners to deliver, and how far the WHOLE hand
    // is above the bid. `surplus` is what SHED reacts to: a hand that bid 1 and
    // holds two aces is one trick from a miss.
    float expected = 0.0f;
    float surplus = 0.0f;
};

// §5. Rebuild this at EVERY decision rather than once per round: cards leave the
// live set constantly, and a queen that was a maybe on trick one is a certainty
// on trick five.
//
// Throws std::invalid_argument through the odds kit for a card id outside the
// deck, which the strategy turns into its heuristic fallback.
Plan buildPlan(common::Mask hand, unsigned int bet, unsigned int tricksWon,
               const OddsContext& odds, const CyborgKnobs& knobs);

// §6.3. How far a hand is from taking exactly `n` tricks: the sum of its `n`
// best pLeadWins scores, minus `n`, as a distance. Zero is a hand whose best n
// cards are all certainties and whose bid is exactly n.
//
// BALANCE compares two of these - the hand after taking this trick against the
// hand after ducking it - and prefers the smaller.
float feasibility(common::Mask hand, unsigned int n, const OddsContext& odds);

} // namespace romanian_whist::cyborg

#endif
