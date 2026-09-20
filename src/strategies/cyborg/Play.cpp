#include <romanian_whist/strategies/cyborg/Play.h>

#include <romanian_whist/CardValidator.h>

#include <algorithm>
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
float liability(const Card& card, const OddsContext& odds)
{
    return pLeadWins(common::cardToId(card, odds.playerCount), odds);
}

bool isSure(const Card& card, const OddsContext& odds)
{
    return isSureWinner(common::cardToId(card, odds.playerCount), odds);
}

// Strict comparisons throughout, so a run of equally scored cards yields the
// FIRST of the run - the same tie rule heuristics::mostDangerous() documents,
// and the reason the legal list must keep its input order.
const Card* extremeBy(const std::vector<Card>& cards, const OddsContext& odds, bool wantMax)
{
    const Card* best = nullptr;
    float bestScore = 0.0f;

    for(const Card& card : cards)
    {
        const float score = liability(card, odds);

        if(best == nullptr || (wantMax ? score > bestScore : score < bestScore))
        {
            best = &card;
            bestScore = score;
        }
    }

    return best;
}

std::optional<Card> highestLiability(const std::vector<Card>& cards, const OddsContext& odds)
{
    const Card* card = extremeBy(cards, odds, true);
    return card ? std::optional<Card>(*card) : std::nullopt;
}

std::optional<Card> lowestLiability(const std::vector<Card>& cards, const OddsContext& odds)
{
    const Card* card = extremeBy(cards, odds, false);
    return card ? std::optional<Card>(*card) : std::nullopt;
}

unsigned int suitLength(const std::vector<Card>& hand, Suit suit)
{
    return static_cast<unsigned int>(
        std::count_if(hand.begin(), hand.end(), [suit](const Card& c) { return c.suit == suit; }));
}

// §6.1's DUCK lead. Lowest chance of winning, and among cards of near-equal (low)
// chance the HIGHEST rank: §7.4's first row leads the queen, not the seven,
// because both are safe now and only one of them will still be safe later.
std::optional<Card> leadDucking(const std::vector<Card>& legal, const OddsContext& odds)
{
    const Card* best = nullptr;
    float bestScore = 0.0f;

    for(const Card& card : legal)
    {
        const float score = liability(card, odds);

        if(best == nullptr || score < bestScore - NEAR_EQUAL)
        {
            best = &card;
            bestScore = score;
            continue;
        }

        if(score <= bestScore + NEAR_EQUAL && static_cast<int>(card.rank) > static_cast<int>(best->rank))
        {
            best = &card;
            bestScore = std::min(score, bestScore);
        }
    }

    return best ? std::optional<Card>(*best) : std::nullopt;
}

// §6.1's TAKE lead, first branch: cash a sure winner from the suit holding the
// most cards, highest first. This is what makes §4.5's gap credit honest - the
// jack was credited because the ace and the king would flush the queen, and
// that only happens if they are led in that suit, in order.
std::optional<Card> cashSureWinner(const PlaySituation& situation, const OddsContext& odds)
{
    // A suit already being cashed keeps the lead while it still holds a
    // certainty, even once it is no longer the longest. §6.1's pseudocode says
    // only "the suit where I hold the most cards", but §7.3's own worked example
    // leads A, K, J of hearts before touching diamonds - and by the king, hearts
    // is the shorter suit. Cashing the ace and switching would abandon the
    // assumption that earned the jack its credit at bidding time.
    if(situation.cashingSuit)
    {
        const Card* inSuit = nullptr;

        for(const Card& card : situation.legal)
        {
            if(card.suit != *situation.cashingSuit || !isSure(card, odds))
                continue;

            if(inSuit == nullptr || static_cast<int>(card.rank) > static_cast<int>(inSuit->rank))
                inSuit = &card;
        }

        if(inSuit != nullptr)
            return *inSuit;
    }

    const Card* best = nullptr;
    unsigned int bestLength = 0;

    for(const Card& card : situation.legal)
    {
        if(!isSure(card, odds))
            continue;

        const unsigned int length = suitLength(situation.hand, card.suit);
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
std::optional<Card> leadLowestOfLongestSuit(const PlaySituation& situation)
{
    const Card* best = nullptr;
    unsigned int bestLength = 0;

    for(const Card& card : situation.legal)
    {
        const unsigned int length = suitLength(situation.hand, card.suit);
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

std::vector<Card> cardsIn(const std::vector<Card>& cards, common::Mask mask, unsigned int playerCount)
{
    std::vector<Card> picked;

    for(const Card& card : cards)
    {
        if((mask & common::cardBit(common::cardToId(card, playerCount))) != 0)
            picked.push_back(card);
    }

    return picked;
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
                               const OddsContext& odds, const CyborgKnobs& /*knobs*/)
{
    if(const std::optional<Card> planned = leadPlanned(situation))
        return planned;

    const unsigned int playerCount = odds.playerCount;

    switch(plan.mode)
    {
    case Mode::Take:
    {
        if(const std::optional<Card> cashed = cashSureWinner(situation, odds))
            return cashed;

        return leadLowestOfLongestSuit(situation);
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
            return highestLiability(shakeable, odds);

        // Every legal card is a certainty, so shedding by leading is impossible.
        // Spend the cheapest of them rather than the dearest.
        return lowestLiability(situation.legal, odds);
    }

    case Mode::Balance:
    {
        const common::Mask legalMask = common::cardsToMask(situation.legal, playerCount);
        const std::vector<Card> winners = cardsIn(situation.legal, plan.winners & legalMask, playerCount);
        const std::vector<Card> losers = cardsIn(situation.legal, plan.losers & legalMask, playerCount);

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
                return highestLiability(plainWinners, odds);
        }

        // Ahead of plan - the winners already cover what is owed - so lead the
        // safest loser and keep them for the tricks that need them.
        if(plan.expected >= static_cast<float>(plan.need) && !losers.empty())
            return lowestLiability(losers, odds);

        if(!winners.empty())
            return highestLiability(winners, odds);

        return lowestLiability(situation.legal, odds);
    }

    case Mode::Duck:
    default:
        return leadDucking(situation.legal, odds);
    }
}

std::optional<Card> chooseFollow(const PlaySituation& situation, const Plan& plan,
                                 const OddsContext& odds, const CyborgKnobs& knobs)
{
    const Suit leadSuit = *situation.leadSuit;
    const std::optional<Card> best =
        CardValidator::getWinningCard(situation.playedCards, leadSuit, situation.trump);

    std::vector<Card> canWin;
    std::vector<Card> safe;

    for(const Card& card : situation.legal)
    {
        // "Safe" is exact rather than a guess (§3.2): a card that does not beat
        // the current best cannot take the trick however the rest of it goes,
        // because later players only push the winner higher.
        if(best && CardValidator::beats(card, *best, leadSuit, situation.trump))
            canWin.push_back(card);
        else
            safe.push_back(card);
    }

    const auto seatsYetToAct = static_cast<unsigned int>(
        situation.playerCount - 1 - std::min<std::size_t>(situation.playedCards.size(),
                                                          situation.playerCount - 1));

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
            return highestLiability(safe, odds);

        return lowestLiability(canWin, odds);
    }

    case Mode::Take:
    {
        if(canWin.empty())
            return lowestLiability(safe, odds); // keep the winners for a trick worth having

        if(seatsYetToAct == 0)
            return lowestLiability(canWin, odds); // last to act: cheapest card that wins

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
            return lowestLiability(holds, odds);

        return lowestLiability(canWin, odds);
    }

    case Mode::Balance:
    default:
    {
        if(canWin.empty())
            return lowestLiability(safe, odds);

        if(safe.empty())
            return lowestLiability(canWin, odds);

        // §6.3's alignment rule. Compare the two hands this decision leaves
        // behind rather than the trick in front of me: one ply, no rollouts.
        const std::optional<Card> takeIt = lowestLiability(canWin, odds);
        const std::optional<Card> duckIt = highestLiability(safe, odds);

        const unsigned int playerCount = odds.playerCount;
        const common::Mask handMask = common::cardsToMask(situation.hand, playerCount);
        const common::Mask afterTake =
            handMask & ~common::cardBit(common::cardToId(*takeIt, playerCount));
        const common::Mask afterDuck =
            handMask & ~common::cardBit(common::cardToId(*duckIt, playerCount));

        const float taking = feasibility(afterTake, plan.need - 1, odds);
        const float ducking = feasibility(afterDuck, plan.need, odds);

        // A tie ducks: a taken trick cannot be given back, while a ducked one
        // often comes round again.
        return (taking < ducking) ? takeIt : duckIt;
    }
    }
}
} // namespace

std::optional<Card> choosePlay(const PlaySituation& situation, const Plan& plan,
                               const OddsContext& odds, const CyborgKnobs& knobs)
{
    if(situation.legal.empty())
        return std::nullopt;

    if(!situation.leadSuit || situation.playedCards.empty())
        return chooseLead(situation, plan, odds, knobs);

    return chooseFollow(situation, plan, odds, knobs);
}

} // namespace romanian_whist::cyborg
