#ifndef ROMANIAN_WHIST_CYBORG_HALVES_H
#define ROMANIAN_WHIST_CYBORG_HALVES_H

#include <romanian_whist/strategies/common/CardMask.h>

#include <optional>

namespace romanian_whist::cyborg
{

// The one idea the whole strategy is built on: split every suit down the middle.
//
// The deck holds two ranks per player per suit, so the BIG cards are the top
// half - exactly `playerCount` of them in each suit - and the small cards are
// the rest. Two players means A K are big and Q J are small; six means A K Q J
// 10 9 are big and 8 down to 3 are small.
//
// There is no arithmetic here that common::CardMask.h did not already do.
// `cardRank()` returns the 0-based index of a card within ITS OWN deck (rank
// minus the deck's lowest rank), which is exactly the number to compare against
// playerCount - so this header is a two-line proof rather than a second
// derivation, and it is deliberately header-only for the same reason.
//
// Two layers. The id-based primitives are total and constexpr: every CardId
// they take must already be valid for the profile, and they do not check.
// The Card-based conveniences below go through common::cardToId(), which
// throws for a rank that is not in this deck at all.

enum class Category
{
    BigTrump,
    SmallTrump,
    BigNonTrump,
    SmallNonTrump
};

// 0 .. 2N-1, where 0 is the lowest rank in this deck.
// Precondition: c is a valid id for `profile`.
constexpr unsigned int rankIndexOf(common::CardId c, const common::DeckProfile& profile)
{
    return common::cardRank(c, profile.ranksPerSuit);
}

constexpr bool isBigId(common::CardId c, const common::DeckProfile& profile)
{
    return rankIndexOf(c, profile) >= profile.playerCount;
}

constexpr bool isSmallId(common::CardId c, const common::DeckProfile& profile)
{
    return !isBigId(c, profile);
}

// Every big card of every suit: the top `playerCount` ranks of each of the four
// suits. Built by shifting one suit's worth of high bits into each suit block.
constexpr common::Mask bigHalfMask(const common::DeckProfile& profile)
{
    // The top half of one suit, sitting at rank offset `playerCount`.
    const common::Mask oneSuit =
        ((1ULL << profile.playerCount) - 1ULL) << profile.playerCount;

    common::Mask all = 0;
    for(unsigned int s = 0; s < 4; ++s)
    {
        all |= oneSuit << (s * profile.ranksPerSuit);
    }
    return all;
}

constexpr Category categoriseId(common::CardId c, const common::DeckProfile& profile,
                                std::optional<Suit> trumpSuit)
{
    const bool big = isBigId(c, profile);
    const bool trump =
        trumpSuit && common::cardSuit(c, profile.ranksPerSuit) == static_cast<unsigned int>(*trumpSuit);

    if(trump)
        return big ? Category::BigTrump : Category::SmallTrump;

    return big ? Category::BigNonTrump : Category::SmallNonTrump;
}

// The Card-based half. Each throws std::invalid_argument through
// common::cardToId() for a rank this deck does not contain - a Two at any table,
// a Six at a four-handed one - which is the error worth getting rather than a
// silently shifted rank index.
inline unsigned int rankIndex(const Card& card, unsigned int playerCount)
{
    return rankIndexOf(common::cardToId(card, playerCount), common::getDeckProfile(playerCount));
}

inline bool isBig(const Card& card, unsigned int playerCount)
{
    return isBigId(common::cardToId(card, playerCount), common::getDeckProfile(playerCount));
}

inline bool isSmall(const Card& card, unsigned int playerCount)
{
    return !isBig(card, playerCount);
}

inline Category categorise(const Card& card, unsigned int playerCount, std::optional<Suit> trumpSuit)
{
    return categoriseId(common::cardToId(card, playerCount), common::getDeckProfile(playerCount),
                        trumpSuit);
}

} // namespace romanian_whist::cyborg

#endif
