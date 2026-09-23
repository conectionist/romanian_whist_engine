#ifndef PLAY_CONTEXT_H
#define PLAY_CONTEXT_H

#include <romanian_whist/Card.h>

#include <optional>
#include <vector>

namespace romanian_whist
{
// Everything a player needs in order to choose a card, gathered into one
// argument so that the playing seam can grow without breaking every
// IMoveProvider and IStrategy implementation again.
struct PlayContext
{
    // The player's hand, legal and illegal cards alike. Run it through
    // CardValidator::getLegalCards() before choosing from it.
    const std::vector<Card>& hand;

    // The trick so far, in the order the cards were played. Empty when this
    // player is leading.
    const std::vector<Card>& playedCards;

    // Empty in 8-card rounds, which have no trump. Initialised here for the
    // reason BetContext's members are: without it, a caller writing
    // `PlayContext{hand, played}` gets a -Wmissing-field-initializers warning in
    // their own build, which is not theirs to fix.
    std::optional<Card> trump{};

    // Empty when this player is leading the trick, and so is the one setting it.
    std::optional<Suit> leadSuit{};

    // What this player bid for the round, and how many tricks they have taken
    // of it so far, so a strategy can tell whether it still owes tricks.
    unsigned int bet = 0;
    unsigned int tricksWon = 0;
};

} // namespace romanian_whist

#endif
