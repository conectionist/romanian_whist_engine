#ifndef TRICK_HEURISTICS_H
#define TRICK_HEURISTICS_H

#include <romanian_whist/Card.h>
#include <romanian_whist/PlayContext.h>

#include <optional>
#include <vector>

namespace romanian_whist::heuristics
{
// The judgements a player makes when it would rather not take tricks, kept out
// of the strategies themselves so that more than one can be built on them
// without either inheriting from the other.

// Orders cards by how much trouble they are to a player trying not to win: any
// trump is worse than any plain card, and rank decides within that. It is a
// strict weak ordering, so it can be handed to the standard algorithms.
bool isMoreDangerous(const Card& a, const Card& b, std::optional<Card> trump);

// The extremes of that ordering. Both return empty for an empty list.
//
// The ordering is weak, not total: two plain cards of the same rank in
// different suits compare equivalent, and so do two such trumps. Which one
// comes back is therefore decided by POSITION - both of these return the first
// of a run of equivalents - so every list handed to them must preserve the
// hand's order. getLegalCards(), safeCards() and winningCards() all push in
// input order for exactly this reason; reordering any of them changes which
// card gets played without changing a single rule.
std::optional<Card> mostDangerous(const std::vector<Card>& cards, std::optional<Card> trump);
std::optional<Card> leastDangerous(const std::vector<Card>& cards, std::optional<Card> trump);

// The two halves of a legal list, split on the card currently winning the trick.
// "Safe" is exact rather than a guess: a card that does not beat the current best
// cannot take the trick however the rest of it goes, because the players still to
// act only push the winner higher.
//
// Both push in INPUT ORDER, for the reason given above - every list handed to
// mostDangerous()/leastDangerous() must preserve the hand's order, and these are
// the lists that get handed to them.
std::vector<Card> safeCards(const std::vector<Card>& legalCards, const Card& currentBest,
                            Suit leadSuit, std::optional<Card> trump);
std::vector<Card> winningCards(const std::vector<Card>& legalCards, const Card& currentBest,
                               Suit leadSuit, std::optional<Card> trump);

// The play that best avoids taking the trick.
//
// Leading, nothing is safe - whatever goes down might hold - so it leads its
// least dangerous card. Following, a card that does not beat the one currently
// winning cannot take the trick no matter who plays after us, since later
// players only push the winner higher. That makes "safe" exact rather than a
// guess, so it dumps the most dangerous card that is still safe, and only when
// every legal card would win does it settle for winning as cheaply as it can.
std::optional<Card> chooseDuckingCard(const PlayContext& context,
                                     const std::vector<Card>& legalCards);

// The mirror image, for a player that still owes tricks on its bid: the
// cheapest card that takes the trick, or - when none of them can - the least
// dangerous card, holding the good ones back for a trick still worth having.
std::optional<Card> chooseWinningCard(const PlayContext& context,
                                     const std::vector<Card>& legalCards);

// A rough count of the tricks a hand will take whether its holder wants them or
// not, which for a low-risk player is the same thing as the bid it should make.
// Deliberately crude: aces, the top trumps, and the length of a long trump
// holding that will still be out when everyone else has run dry.
unsigned int countLikelyWinners(const std::vector<Card>& hand, std::optional<Card> trump);

} // namespace romanian_whist::heuristics

#endif
