#ifndef ROMANIAN_WHIST_CARD_MASK_H
#define ROMANIAN_WHIST_CARD_MASK_H

#include <romanian_whist/strategies/common/CardMask.h>

// The bitmask helpers moved to strategies/common/ when the round tracker was
// extracted for Cyborg to share - they were never Reckoner-specific, they were
// just written here first.
//
// This header keeps `reckoner::Mask`, `reckoner::cardToId` and the rest naming
// the same entities, so every file under strategies/reckoner/ compiles
// unchanged. Qualified lookup follows a using-declaration, so `reckoner::x` and
// `common::x` are one name, not two types that happen to match.
//
// New code should include <romanian_whist/strategies/common/CardMask.h> and say
// `common::`. This exists so that moving the file did not become a rewrite of
// the reckoner subtree, and it can go once nothing spells these `reckoner::`.
namespace romanian_whist::reckoner
{

using common::CardId;
using common::DeckProfile;
using common::INVALID_CARD_ID;
using common::Mask;

using common::cardBit;
using common::cardRank;
using common::cardsToMask;
using common::cardSuit;
using common::cardToId;
using common::getDeckProfile;
using common::idToCard;
using common::maskAbove;
using common::maskBelow;
using common::maskBetween;
using common::maskSuit;
using common::maskToCards;
using common::popcount;

} // namespace romanian_whist::reckoner

#endif
