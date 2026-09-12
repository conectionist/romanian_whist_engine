#ifndef ROMANIAN_WHIST_COMMON_ROUND_MEMORY_H
#define ROMANIAN_WHIST_COMMON_ROUND_MEMORY_H

#include <romanian_whist/strategies/common/CardMask.h>

#include <array>
#include <optional>
#include <vector>

namespace romanian_whist::common
{

// A bid that has not been made yet. Distinct from a bid of 0, which is a real
// and common bid - a strategy that confuses the two reads an empty seat as a
// player who wants no tricks.
inline constexpr int UNBID = -1;

struct TrickCard
{
    unsigned int seat = 0;
    CardId card = INVALID_CARD_ID;
};

struct CompletedTrick
{
    unsigned int leader = 0;
    unsigned int winner = 0;
    std::vector<TrickCard> cards;
};

// The trick as it stood immediately BEFORE a card was played, handed back by
// recordCardPlayed() so a caller that wants to reason about the choice a player
// just made does not have to reconstruct the position they made it in.
//
// `leadSuit` is empty when the card led the trick, and `bestBefore` is
// INVALID_CARD_ID in the same case - there was nothing to beat.
struct PrePlayState
{
    std::optional<Suit> leadSuit = std::nullopt;
    CardId bestBefore = INVALID_CARD_ID;
    unsigned int bestHolderBefore = 0;
};

// What a strategy knows about the round it is playing, from watching it.
//
// This is the shared floor under every card-counting strategy: which cards are
// still live, who holds how many, who has proved a void, who bid what, who has
// won what. It infers only what is CERTAIN - see recordCardPlayed() - and
// leaves guesswork to whoever is built on top. Reckoner adds soft constraints
// and a cross-round opponent model; Cyborg adds neither and wants exactly this.
//
// Every field is public and there is no engine handle, no RNG and no
// configuration, so it is cheap to construct, trivial to copy, and testable by
// driving it with bare method calls - which is how tests/ReckonerTrackerTests
// and tests/CyborgMemoryTests both use it.
//
// It does NOT know the round type, whether its owner is seated, or whether it
// was wired up at all. Those are the owning strategy's business, because what
// counts as "wired up correctly" differs between them.
class RoundMemory
{
public:
    unsigned int playerCount = 4;
    unsigned int roundTrickCount = 1; // R
    unsigned int mySeat = 0;
    unsigned int opener = 0; // round leader

    std::optional<CardId> trumpCard = std::nullopt;
    std::optional<Suit> trumpSuit = std::nullopt;

    // Neither played nor turned up as trump: could be in a hand, or undealt.
    Mask live = 0;

    std::array<Mask, 6> playedBy{};
    std::array<unsigned int, 6> handSize{};
    std::array<std::uint8_t, 6> voidMask{}; // bit s is (1 << s)
    std::array<int, 6> bids{};              // UNBID, or 0..R
    std::array<unsigned int, 6> won{};

    std::vector<CompletedTrick> completedTricks;
    std::vector<TrickCard> currentTrickCards;
    unsigned int currentTrickLeader = 0;

    RoundMemory() = default;

    // Resets everything round-scoped. The turned-up trump is removed from
    // `live` - it is dead, nobody holds it, and its rank is exactly what makes
    // a king the top trump for the rest of the round.
    void initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                   unsigned int openerSeat, std::optional<Card> trump);

    void recordBid(unsigned int seat, unsigned int value);

    // Records the card and draws the two inferences that are certain rather
    // than likely:
    //
    //   - a player who did not follow the lead suit does not hold it;
    //   - a player who discarded off-suit and did not trump holds no trump
    //     either, because the rules would have forced the ruff.
    //
    // The second is worth more than it looks: one discard rules out two whole
    // suits at once, and it is what turns a maybe in your hand into a certainty
    // for the rest of the round.
    PrePlayState recordCardPlayed(unsigned int seat, const Card& card);

    void recordTrickWon(unsigned int winner);

    // Live cards that are not mine: in an opponent's hand, or in the dead pile.
    Mask getUnseenCards(Mask myHand) const;

    // Live cards in nobody's hand - the undealt remainder. Zero in 8-trick
    // rounds, where the whole deck is dealt, which is what lets a probability
    // over unseen cards collapse into a certainty there.
    unsigned int getDeadCount() const;

    // The ranking rule: highest trump, else highest card of the lead suit, else
    // the incumbent stays. Agrees with CardValidator::beats() by construction -
    // both encode rule 7 - but works on ids and the tracked trump suit, so it
    // can be asked about cards nobody has played.
    bool cardBeats(CardId candidate, CardId currentBest, Suit leadSuit) const;

    // How many seats have bid so far. For a seat that is about to bid, this is
    // also its own 0-based position in the bidding order, which is what the
    // bidding rules key off - there is no need to ask the engine.
    unsigned int bidsPlaced() const;

    unsigned int previousBidSum() const;

    // Cards held by everyone except me. The exact draw size for "is a card that
    // beats mine in somebody's hand?".
    unsigned int handsOutstanding() const;
};

} // namespace romanian_whist::common

#endif
