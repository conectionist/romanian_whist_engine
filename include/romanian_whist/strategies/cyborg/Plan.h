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
    // The `need` best cards, by pLeadWins. Empty when need == 0.
    //
    // The losers are the rest of the hand, and are NOT stored: a second mask
    // would be an invariant (winners | losers == hand) that nothing enforces and
    // that a hand-built Plan can quietly break. Whoever needs the losers has the
    // hand in front of them and can say `& ~winners`.
    common::Mask winners = 0;

    unsigned int need = 0; // bet - tricksWon, clamped at 0
    Mode mode = Mode::Duck;

    // What the plan expects its winners to deliver. §6.1 reads this to tell
    // whether the bid is already paid for.
    //
    // It can never EXCEED `need` - it is a sum of `need` probabilities - so
    // `expected >= need` is an equality test for "every winner is a certainty",
    // which is how Play.cpp names it.
    float expected = 0.0f;
};

// §5. Rebuild this at EVERY decision rather than once per round: cards leave the
// live set constantly, and a queen that was a maybe on trick one is a certainty
// on trick five.
//
// Throws std::invalid_argument through the odds kit for a card id outside the
// deck, which the strategy turns into its heuristic fallback.
// `scores` is an optional per-decision ScoreCache, shared with choosePlay so that
// a card is scored once rather than once per rule that asks. Omit it and this
// builds its own.
Plan buildPlan(common::Mask hand, unsigned int bet, unsigned int tricksWon,
               const OddsContext& odds, const CyborgKnobs& knobs,
               const ScoreCache* scores = nullptr);

// §6.3. How far a hand is from taking exactly `n` tricks: the sum of its `n`
// best pLeadWins scores, minus `n`, as a distance. Zero is a hand whose best n
// cards are all certainties and whose bid is exactly n.
//
// BALANCE compares two of these - the hand after taking this trick against the
// hand after ducking it - and prefers the smaller.
float feasibility(common::Mask hand, unsigned int n, const OddsContext& odds,
                  const ScoreCache* scores = nullptr);

} // namespace romanian_whist::cyborg

#endif
