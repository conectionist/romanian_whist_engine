#ifndef ROMANIAN_WHIST_CYBORG_PLAY_H
#define ROMANIAN_WHIST_CYBORG_PLAY_H

#include <romanian_whist/Card.h>
#include <romanian_whist/strategies/cyborg/Plan.h>

#include <optional>
#include <vector>

namespace romanian_whist::cyborg
{

// §6. The position one decision is made in, and nothing else.
//
// NOTE WHAT IS MISSING: there is no RoundMemory here. Everything in this struct
// comes from the PlayContext the engine handed the strategy, and `legal` is the
// list CardValidator already filtered - so the rule that the context is the
// authority on what may be played cannot be broken by a mistake in this file.
// The memory reaches the decision only through the OddsContext, which is about
// what the OTHER hands might hold.
struct PlaySituation
{
    std::vector<Card> hand;        // every card held, legal or not
    std::vector<Card> legal;       // already filtered by CardValidator::getLegalCards()
    std::vector<Card> playedCards; // the trick so far; empty when leading

    std::optional<Card> trump;    // empty in 8-trick rounds
    std::optional<Suit> leadSuit; // empty when leading, and so setting it

    // §4.3's opening lead, when the bid chose one. Honoured on trick one of a
    // two-trick round and ignored everywhere else.
    std::optional<Card> plannedLead;

    // The suit this hand is part-way through cashing - the suit of the last card
    // it led. §7.3 turns on it: the jack of hearts was credited at bidding time
    // because the ace and the king would flush the queen, which only happens if
    // the suit is finished before another is started. A plain value, not a peek
    // at the memory.
    std::optional<Suit> cashingSuit;

    // NOTE there is no playerCount here. PlayContext does not carry one, so a
    // field here could only be filled from the memory - which is exactly what the
    // note at the top says this struct does not do. The seat count reaches the
    // rules through OddsContext::playerCount, the same place every card id comes
    // from, so the two can never disagree about which deck is in play.
};

// §6.1-§6.4. The card to play, or empty if `legal` is empty.
//
// Every argmax and argmin below iterates the LEGAL list, so the answer is legal
// by construction. The strategy still checks it against the list afterwards -
// belt and braces, and the check is what makes a bug here cost a fallback
// rather than a refused move.
//
// `scores` is an optional per-decision ScoreCache. Pass the one buildPlan was
// given and no card is scored twice in a decision; pass nothing and this builds
// its own over the hand, which is still cheaper than letting each rule rescore.
std::optional<Card> choosePlay(const PlaySituation& situation, const Plan& plan,
                               const OddsContext& odds, const CyborgKnobs& knobs,
                               const ScoreCache* scores = nullptr);

} // namespace romanian_whist::cyborg

#endif
