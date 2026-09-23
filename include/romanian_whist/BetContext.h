#ifndef BET_CONTEXT_H
#define BET_CONTEXT_H

#include <romanian_whist/Card.h>
#include <romanian_whist/RoundType.h>

#include <optional>
#include <vector>

namespace romanian_whist
{
// Everything a player needs in order to choose a bid, gathered into one
// argument so that the bidding seam can grow without breaking every
// IMoveProvider and IStrategy implementation again.
struct BetContext
{
    // The bidder's hand. Its size is the round's trick count, so it doubles as
    // the upper bound on a legal bid.
    const std::vector<Card>& hand;

    // Empty in 8-card rounds, which have no trump.
    //
    // Initialised here rather than left to the caller so that `BetContext{hand}`
    // is warning-free: without it, -Wmissing-field-initializers counts every
    // member a braced initialiser stops short of, and this struct is routinely
    // built from the hand alone.
    std::optional<Card> trump{};

    // True for whoever opens the bidding, who bids with nothing to go on.
    bool isFirstPlayer = false;

    // The one bid the final bidder may not make: the value that would bring the
    // round's bids to exactly the trick count. Empty for every other bidder,
    // and empty for the final bidder when the bids so far already exceed the
    // trick count - no single bid can hit the total then.
    std::optional<unsigned int> forbiddenBet{};

    // Forehead and Hidden both mean the bidder cannot use `hand`'s contents to
    // decide - the engine still hands over the real hand (it has no other one
    // to give), but a strategy that reads it in either round type is bidding
    // on information the round says it does not have.
    RoundType roundType = RoundType::Normal;
};

} // namespace romanian_whist

#endif
