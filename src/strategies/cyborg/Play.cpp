#include <romanian_whist/strategies/cyborg/Play.h>

#include <romanian_whist/CardValidator.h>
#include <romanian_whist/strategies/TrickHeuristics.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace romanian_whist::cyborg
{

namespace
{
// Two scores closer than this count as equal for §6.1's DUCK tie-break. Big
// enough to absorb the last bits of a product of hypergeometrics, small enough
// that it never merges two genuinely different chances.
constexpr float NEAR_EQUAL = 1e-4f;

// §6. Liability and asset are ONE number read from either end: pLeadWins(c).
// Which end you want depends only on the mode, which is what keeps leading,
// following and discarding from drifting apart into three different orderings.
//
// Read from the decision's ScoreCache rather than recomputed: the rules below ask
// for the same card's score several times over as each one walks the hand.
float liability(const Card& card, const OddsContext& odds, const ScoreCache& scores)
{
    return scores.of(common::cardToId(card, odds.playerCount), odds);
}

bool isSure(const Card& card, const OddsContext& odds)
{
    return isSureWinner(common::cardToId(card, odds.playerCount), odds);
}

// Strict comparisons throughout, so a run of equally scored cards yields the
// FIRST of the run - the same tie rule heuristics::mostDangerous() documents,
// and the reason the legal list must keep its input order.
const Card* extremeBy(const std::vector<Card>& cards, const OddsContext& odds,
                      const ScoreCache& scores, bool wantMax)
{
    const Card* best = nullptr;
    float bestScore = 0.0f;

    for(const Card& card : cards)
    {
        const float score = liability(card, odds, scores);

        if(best == nullptr || (wantMax ? score > bestScore : score < bestScore))
        {
            best = &card;
            bestScore = score;
        }
    }

    return best;
}

std::optional<Card> highestLiability(const std::vector<Card>& cards, const OddsContext& odds,
                                     const ScoreCache& scores)
{
    const Card* card = extremeBy(cards, odds, scores, true);
    return card ? std::optional<Card>(*card) : std::nullopt;
}

std::optional<Card> lowestLiability(const std::vector<Card>& cards, const OddsContext& odds,
                                    const ScoreCache& scores)
{
    const Card* card = extremeBy(cards, odds, scores, false);
    return card ? std::optional<Card>(*card) : std::nullopt;
}

// §6.1's "ahead of plan" test, named because the expression does not say what it
// means.
//
// `expected` is the sum of the `need` best pLeadWins scores and pLeadWins is
// capped at 1, so expected can never EXCEED need. This `>=` is therefore an
// equality, and it holds exactly when every card the plan is counting on is a
// CERTAINTY rather than merely likely - each one having hit one of pLeadWins's
// literal `return 1.0f` early-outs, which sum exactly. A hand one ULP short, with
// a 0.99999994f among its winners, is not ahead of plan and cashes instead.
bool winnersAreCertain(const Plan& plan)
{
    return plan.expected >= static_cast<float>(plan.need);
}

// How many cards the hand holds in each suit, counted ONCE per decision. §6.1's
// TAKE lead asks in two places and would otherwise rescan the whole hand for
// every candidate card.
using SuitLengths = std::array<unsigned int, 4>; // Hearts, Diamonds, Spades, Clubs

SuitLengths suitLengths(const std::vector<Card>& hand)
{
    SuitLengths lengths{};

    for(const Card& card : hand)
    {
        ++lengths[static_cast<std::size_t>(card.suit)];
    }

    return lengths;
}

unsigned int lengthOf(const SuitLengths& lengths, Suit suit)
{
    return lengths[static_cast<std::size_t>(suit)];
}

// §6.1's DUCK lead. Lowest chance of winning, and among cards of near-equal (low)
// chance the DEAREST: §7.4's first row leads the queen, not the seven, because
// both are safe now and only one of them will still be safe later.
//
// "Dearest" is heuristics::isMoreDangerous - ANY TRUMP ABOVE ANY PLAIN CARD, rank
// within that - and not bare rank. §6 says the liability score subsumes that
// ordering, and §7.4's two rows are both single-suit, so they never settle the
// cross-suit case. Bare rank settles it the wrong way: once the big trumps are
// gone a small trump and a plain card both score zero, and throwing the plain
// card retains the trump that is later forced to ruff and take the very trick
// this mode is trying not to win. Measured, that was 9% of DUCK leads in trump
// rounds.
std::optional<Card> leadDucking(const std::vector<Card>& legal, const OddsContext& odds,
                                const ScoreCache& scores, std::optional<Card> trump)
{
    const Card* best = nullptr;
    float bestScore = 0.0f;

    for(const Card& card : legal)
    {
        const float score = liability(card, odds, scores);

        if(best == nullptr || score < bestScore - NEAR_EQUAL)
        {
            best = &card;
            bestScore = score;
            continue;
        }

        if(score <= bestScore + NEAR_EQUAL && heuristics::isMoreDangerous(card, *best, trump))
        {
            best = &card;
            bestScore = std::min(score, bestScore);
        }
    }

    return best ? std::optional<Card>(*best) : std::nullopt;
}

// §6.1's TAKE lead, first rule: a suit already being cashed keeps the lead while
// it still holds a certainty, even once it is no longer the longest.
//
// §6.1's pseudocode says only "the suit where I hold the most cards", but §7.3's
// own worked example leads A, K, J of hearts before touching diamonds - and by the
// king, hearts is the shorter suit. Cashing the ace and switching would abandon
// the assumption that earned the jack its credit at bidding time. This rule wins
// against a longer suit, which is why it is asked FIRST rather than folded into
// the length comparison below.
std::optional<Card> continueCashing(const PlaySituation& situation, const OddsContext& odds)
{
    if(!situation.cashingSuit)
        return std::nullopt;

    const Card* best = nullptr;

    for(const Card& card : situation.legal)
    {
        if(card.suit != *situation.cashingSuit || !isSure(card, odds))
            continue;

        if(best == nullptr || static_cast<int>(card.rank) > static_cast<int>(best->rank))
            best = &card;
    }

    return best ? std::optional<Card>(*best) : std::nullopt;
}

// §6.1's TAKE lead, second rule: start a run by cashing a certainty from the suit
// holding the most cards, highest first. This is what makes §4.5's gap credit
// honest - the jack was credited because the ace and the king would flush the
// queen, and that only happens if they are led in that suit, in order.
std::optional<Card> cashLongestSuit(const PlaySituation& situation, const OddsContext& odds,
                                    const SuitLengths& lengths)
{
    const Card* best = nullptr;
    unsigned int bestLength = 0;

    for(const Card& card : situation.legal)
    {
        if(!isSure(card, odds))
            continue;

        const unsigned int length = lengthOf(lengths, card.suit);
        const bool better = best == nullptr || length > bestLength ||
                            (length == bestLength &&
                             static_cast<int>(card.rank) > static_cast<int>(best->rank));

        if(better)
        {
            best = &card;
            bestLength = length;
        }
    }

    return best ? std::optional<Card>(*best) : std::nullopt;
}

// §6.1's TAKE lead, second branch. The pseudocode says argmax pLeadWins, but the
// prose under it is explicit and wins: with no card that is certain, leading a
// big one into an unknown table just feeds whoever holds the card above it.
// Lead the LOWEST card of the longest suit instead - it flushes the guards and
// gives the big cards a second round of the suit to work in.
std::optional<Card> leadLowestOfLongestSuit(const PlaySituation& situation,
                                            const SuitLengths& lengths)
{
    const Card* best = nullptr;
    unsigned int bestLength = 0;

    for(const Card& card : situation.legal)
    {
        const unsigned int length = lengthOf(lengths, card.suit);
        const bool better = best == nullptr || length > bestLength ||
                            (length == bestLength &&
                             static_cast<int>(card.rank) < static_cast<int>(best->rank));

        if(better)
        {
            best = &card;
            bestLength = length;
        }
    }

    return best ? std::optional<Card>(*best) : std::nullopt;
}

// The legal list split into the cards the plan is counting on and the rest.
//
// Walks the LEGAL list, so both halves are legal by construction and both keep
// its order - which is what the argmin/argmax above rely on to settle ties.
void splitByPlan(const PlaySituation& situation, const Plan& plan, unsigned int playerCount,
                 std::vector<Card>& winners, std::vector<Card>& losers)
{
    for(const Card& card : situation.legal)
    {
        const bool counted =
            (plan.winners & common::cardBit(common::cardToId(card, playerCount))) != 0;

        (counted ? winners : losers).push_back(card);
    }
}

std::optional<Card> leadPlanned(const PlaySituation& situation)
{
    // §6.4. The bid and the opening lead of a two-trick round were one decision,
    // so §6.1 does not get to second-guess it. Two cards in hand and nothing
    // played is trick one of that round; from trick two the general machinery
    // applies, and with one card left there is nothing to decide anyway.
    if(!situation.plannedLead || situation.hand.size() != 2)
        return std::nullopt;

    const auto found = std::find(situation.legal.begin(), situation.legal.end(), *situation.plannedLead);

    return (found != situation.legal.end()) ? std::optional<Card>(*found) : std::nullopt;
}

std::optional<Card> chooseLead(const PlaySituation& situation, const Plan& plan,
                               const OddsContext& odds, const CyborgKnobs& /*knobs*/,
                               const ScoreCache& scores)
{
    if(const std::optional<Card> planned = leadPlanned(situation))
        return planned;

    const unsigned int playerCount = odds.playerCount;

    switch(plan.mode)
    {
    case Mode::Take:
    {
        // §6.1's three rules, in the order §6.1 states them.
        const SuitLengths lengths = suitLengths(situation.hand);

        if(const std::optional<Card> cashed = continueCashing(situation, odds))
            return cashed;

        if(const std::optional<Card> cashed = cashLongestSuit(situation, odds, lengths))
            return cashed;

        return leadLowestOfLongestSuit(situation, lengths);
    }

    case Mode::Shed:
    {
        // A sure winner cannot be shed by leading it - it just wins. Save it for
        // a trick somebody else opens, and lead the dearest card that can still
        // be taken off me while the table still holds cards that beat it.
        std::vector<Card> shakeable;
        for(const Card& card : situation.legal)
        {
            if(!isSure(card, odds))
                shakeable.push_back(card);
        }

        if(!shakeable.empty())
            return highestLiability(shakeable, odds, scores);

        // Every legal card is a certainty, so shedding by leading is impossible.
        // Spend the cheapest of them rather than the dearest.
        return lowestLiability(situation.legal, odds, scores);
    }

    case Mode::Balance:
    {
        std::vector<Card> winners;
        std::vector<Card> losers;
        splitByPlan(situation, plan, playerCount, winners, losers);

        // A non-trump winner is worth less every trick that trumps stay live,
        // so cash it now rather than watch it get ruffed later.
        if(situation.trump && !winners.empty())
        {
            const bool trumpsLive =
                (odds.unseen & common::maskSuit(static_cast<unsigned int>(situation.trump->suit),
                                                odds.profile())) != 0;

            std::vector<Card> plainWinners;
            for(const Card& card : winners)
            {
                if(card.suit != situation.trump->suit)
                    plainWinners.push_back(card);
            }

            if(trumpsLive && !plainWinners.empty())
                return highestLiability(plainWinners, odds, scores);
        }

        // Ahead of plan - every winner the plan counts on is a certainty - so lead
        // the safest loser and keep them for the tricks that need them. Otherwise
        // cash the dearest winner while it is still worth something.
        //
        // Both lists are non-empty here and need no guard: leading means the legal
        // list IS the hand, and BALANCE means 1 <= need < hand size, so buildPlan
        // put at least one card in each.
        return winnersAreCertain(plan) ? lowestLiability(losers, odds, scores)
                                       : highestLiability(winners, odds, scores);
    }

    case Mode::Duck:
        return leadDucking(situation.legal, odds, scores, situation.trump);
    }

    // NO `default:` INSIDE THE SWITCH, in either chooser. A default fused onto a
    // real case suppresses -Wswitch, and the two switches here had it fused onto
    // DIFFERENT cases - so a fifth mode would have led by ducking and followed by
    // balancing, silently. Exhaustive arms plus one fallback below means the
    // compiler names any mode that goes unhandled, and the fallback is stated once.
    return leadDucking(situation.legal, odds, scores, situation.trump);
}

// §6.3's alignment rule, given the trick already split into what can win it and
// what cannot. Its own function so that both the BALANCE arm and the fallback
// after the switch can name it rather than repeat it.
std::optional<Card> followBalancing(const PlaySituation& situation, const Plan& plan,
                                    const OddsContext& odds, const ScoreCache& scores,
                                    const std::vector<Card>& canWin,
                                    const std::vector<Card>& safe)
{
    if(canWin.empty())
        return lowestLiability(safe, odds, scores);

    if(safe.empty())
        return lowestLiability(canWin, odds, scores);

    // Compare the two hands this decision leaves behind rather than the trick in
    // front of me: one ply, no rollouts.
    const std::optional<Card> takeIt = lowestLiability(canWin, odds, scores);
    const std::optional<Card> duckIt = highestLiability(safe, odds, scores);

    const unsigned int playerCount = odds.playerCount;
    const common::Mask handMask = common::cardsToMask(situation.hand, playerCount);
    const common::Mask afterTake =
        handMask & ~common::cardBit(common::cardToId(*takeIt, playerCount));
    const common::Mask afterDuck =
        handMask & ~common::cardBit(common::cardToId(*duckIt, playerCount));

    // `need` is at least 1 in BALANCE - buildPlan sends need == 0 to DUCK - so the
    // subtraction below cannot wrap.
    // Both masks are subsets of the hand the cache was built from, so these two
    // probes cost no fresh odds arithmetic at all - they only re-sort.
    const float taking = feasibility(afterTake, plan.need > 0 ? plan.need - 1 : 0, odds, &scores);
    const float ducking = feasibility(afterDuck, plan.need, odds, &scores);

    // A tie ducks: a taken trick cannot be given back, while a ducked one often
    // comes round again.
    return (taking < ducking) ? takeIt : duckIt;
}

std::optional<Card> chooseFollow(const PlaySituation& situation, const Plan& plan,
                                 const OddsContext& odds, const CyborgKnobs& knobs,
                                 const ScoreCache& scores)
{
    const Suit leadSuit = *situation.leadSuit;
    const std::optional<Card> best =
        CardValidator::getWinningCard(situation.playedCards, leadSuit, situation.trump);

    // §3.2's exactness, borrowed rather than re-derived: a card that does not beat
    // the current best cannot take the trick however the rest of it goes, because
    // the players still to act only push the winner higher. The heuristics own
    // that split, and both halves come back in input order - which is what the
    // argmin/argmax below rely on to settle their ties.
    //
    // `best` is always set, since chooseFollow is only reached with cards already
    // on the table; an empty trick would make every card safe, which is also the
    // right answer.
    const std::vector<Card> canWin =
        best ? heuristics::winningCards(situation.legal, *best, leadSuit, situation.trump)
             : std::vector<Card>{};
    const std::vector<Card> safe =
        best ? heuristics::safeCards(situation.legal, *best, leadSuit, situation.trump)
             : situation.legal;

    const auto seatsYetToAct = static_cast<unsigned int>(
        odds.playerCount - 1 -
        std::min<std::size_t>(situation.playedCards.size(), odds.playerCount - 1));

    switch(plan.mode)
    {
    case Mode::Duck:
    case Mode::Shed:
    {
        // THE ASYMMETRY. A ducking hand throws its BIGGEST safe card, because
        // every card it holds is a liability and the dearest one is the hardest
        // to lose later. A taking hand throws its smallest. Same list, opposite
        // ends; getting this backwards is the single most expensive bug in this
        // file, which is why the two are written as far apart as they are.
        if(!safe.empty())
            return highestLiability(safe, odds, scores);

        return lowestLiability(canWin, odds, scores);
    }

    case Mode::Take:
    {
        if(canWin.empty())
            return lowestLiability(safe, odds, scores); // keep the winners for a trick worth having

        if(seatsYetToAct == 0)
            return lowestLiability(canWin, odds, scores); // last to act: cheapest card that wins

        // Players still to act, so the cheapest winner is often not cheap
        // enough. Pay only for a trick that will probably still be mine.
        std::vector<Card> holds;
        for(const Card& card : canWin)
        {
            const float held =
                pHolds(common::cardToId(card, odds.playerCount), seatsYetToAct, leadSuit, odds);

            if(held >= knobs.holdThreshold)
                holds.push_back(card);
        }

        if(!holds.empty())
            return lowestLiability(holds, odds, scores);

        return lowestLiability(canWin, odds, scores);
    }

    case Mode::Balance:
        return followBalancing(situation, plan, odds, scores, canWin, safe);
    }

    // See chooseLead: no `default:` fused onto a case, so -Wswitch can speak, and
    // the fallback is stated once. BALANCE is the safe one to fall back to when
    // following - it is the arm that neither insists on the trick nor refuses it.
    return followBalancing(situation, plan, odds, scores, canWin, safe);
}
} // namespace

std::optional<Card> choosePlay(const PlaySituation& situation, const Plan& plan,
                               const OddsContext& odds, const CyborgKnobs& knobs,
                               const ScoreCache* scores)
{
    if(situation.legal.empty())
        return std::nullopt;

    // One score per card per decision. The caller may already have built the cache
    // for buildPlan - in which case every card below is a lookup - and if it has
    // not, building it here still saves the rules from rescoring each other's
    // cards. Either way it covers the whole hand, which is a superset of the legal
    // list and of §6.3's two one-ply masks.
    ScoreCache owned;
    if(scores == nullptr)
    {
        owned = makeScoreCache(common::cardsToMask(situation.hand, odds.playerCount), odds);
        scores = &owned;
    }

    if(!situation.leadSuit || situation.playedCards.empty())
        return chooseLead(situation, plan, odds, knobs, *scores);

    return chooseFollow(situation, plan, odds, knobs, *scores);
}

} // namespace romanian_whist::cyborg
