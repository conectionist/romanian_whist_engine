#ifndef ROMANIAN_WHIST_CARD_MASK_H
#define ROMANIAN_WHIST_CARD_MASK_H

#include <romanian_whist/Card.h>

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace romanian_whist::reckoner
{

using Mask = std::uint64_t;
using CardId = std::uint8_t;

inline constexpr CardId INVALID_CARD_ID = 255;

struct DeckProfile
{
    unsigned int playerCount = 4;
    unsigned int ranksPerSuit = 8;
    unsigned int deckSize = 32;
    int minRankInt = 5;
    Mask fullDeckMask = 0xFFFFFFFFULL;
    Mask suitMasks[4]{};
    Mask aboveMasks[48]{};
    Mask belowMasks[48]{};
};

namespace detail
{

constexpr DeckProfile makeDeckProfile(unsigned int n)
{
    DeckProfile profile{};
    profile.playerCount = n;
    profile.ranksPerSuit = 2 * n;
    profile.deckSize = 8 * n;
    profile.minRankInt = 1 + (6 - static_cast<int>(n)) * 2;
    profile.fullDeckMask = (profile.deckSize >= 64) ? ~0ULL : ((1ULL << profile.deckSize) - 1ULL);

    for(unsigned int s = 0; s < 4; ++s)
    {
        profile.suitMasks[s] = ((1ULL << profile.ranksPerSuit) - 1ULL) << (s * profile.ranksPerSuit);
    }

    for(CardId c = 0; c < profile.deckSize; ++c)
    {
        const unsigned int s = c / profile.ranksPerSuit;
        const unsigned int r = c % profile.ranksPerSuit;

        const unsigned int cardsAbove = profile.ranksPerSuit - 1 - r;
        if(cardsAbove > 0)
        {
            profile.aboveMasks[c] = ((1ULL << cardsAbove) - 1ULL) << (c + 1);
        }
        else
        {
            profile.aboveMasks[c] = 0ULL;
        }

        if(r > 0)
        {
            profile.belowMasks[c] = ((1ULL << r) - 1ULL) << (s * profile.ranksPerSuit);
        }
        else
        {
            profile.belowMasks[c] = 0ULL;
        }
    }

    return profile;
}

struct ProfileTable
{
    DeckProfile profiles[7]{};

    constexpr ProfileTable()
    {
        for(unsigned int n = 2; n <= 6; ++n)
        {
            profiles[n] = makeDeckProfile(n);
        }
    }
};

inline constexpr ProfileTable PROFILES{};

} // namespace detail

inline const DeckProfile& getDeckProfile(unsigned int playerCount)
{
    if(playerCount < 2 || playerCount > 6)
    {
        throw std::invalid_argument("Reckoner: playerCount must be between 2 and 6");
    }
    return detail::PROFILES.profiles[playerCount];
}

inline constexpr Mask cardBit(CardId c)
{
    return 1ULL << c;
}

inline constexpr int popcount(Mask m)
{
    return std::popcount(m);
}

inline CardId cardToId(const Card& card, unsigned int playerCount)
{
    const auto& profile = getDeckProfile(playerCount);
    const int r = static_cast<int>(card.rank) - profile.minRankInt;
    if(r < 0 || r >= static_cast<int>(profile.ranksPerSuit))
    {
        throw std::invalid_argument("Reckoner: card rank " + std::to_string(static_cast<int>(card.rank)) +
                                    " not in deck for " + std::to_string(playerCount) + " players");
    }
    const unsigned int s = static_cast<unsigned int>(card.suit);
    return static_cast<CardId>(s * profile.ranksPerSuit + static_cast<unsigned int>(r));
}

inline Card idToCard(CardId c, unsigned int playerCount)
{
    const auto& profile = getDeckProfile(playerCount);
    if(c >= profile.deckSize)
    {
        throw std::invalid_argument("Reckoner: card ID " + std::to_string(c) +
                                    " exceeds deck size " + std::to_string(profile.deckSize));
    }
    const unsigned int s = c / profile.ranksPerSuit;
    const unsigned int r = c % profile.ranksPerSuit;
    return Card{static_cast<Rank>(profile.minRankInt + static_cast<int>(r)), static_cast<Suit>(s)};
}

inline constexpr unsigned int cardSuit(CardId c, unsigned int ranksPerSuit)
{
    return c / ranksPerSuit;
}

inline constexpr unsigned int cardRank(CardId c, unsigned int ranksPerSuit)
{
    return c % ranksPerSuit;
}

inline constexpr Mask maskSuit(unsigned int suit, const DeckProfile& profile)
{
    return profile.suitMasks[suit];
}

inline constexpr Mask maskAbove(CardId c, const DeckProfile& profile)
{
    return profile.aboveMasks[c];
}

inline constexpr Mask maskBelow(CardId c, const DeckProfile& profile)
{
    return profile.belowMasks[c];
}

inline constexpr Mask maskBetween(CardId a, CardId b, const DeckProfile& profile)
{
    if(cardSuit(a, profile.ranksPerSuit) != cardSuit(b, profile.ranksPerSuit))
        return 0ULL;
    if(cardRank(a, profile.ranksPerSuit) >= cardRank(b, profile.ranksPerSuit))
        return 0ULL;
    return maskAbove(a, profile) & maskBelow(b, profile);
}

inline Mask cardsToMask(const std::vector<Card>& cards, unsigned int playerCount)
{
    Mask m = 0;
    for(const auto& card : cards)
    {
        m |= cardBit(cardToId(card, playerCount));
    }
    return m;
}

inline std::vector<Card> maskToCards(Mask mask, unsigned int playerCount)
{
    std::vector<Card> cards;
    while(mask != 0)
    {
        const int idx = std::countr_zero(mask);
        cards.push_back(idToCard(static_cast<CardId>(idx), playerCount));
        mask &= mask - 1ULL;
    }
    return cards;
}

} // namespace romanian_whist::reckoner

#endif
