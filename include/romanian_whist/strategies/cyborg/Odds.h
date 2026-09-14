#ifndef ROMANIAN_WHIST_CYBORG_ODDS_H
#define ROMANIAN_WHIST_CYBORG_ODDS_H

#include <romanian_whist/strategies/common/CardMask.h>
#include <romanian_whist/strategies/common/RoundMemory.h>

#include <array>
#include <optional>

namespace romanian_whist::cyborg
{

// "Which of the cards I cannot see would beat this one, and how likely is it
// that one of them actually turns up?"
//
// The same question decides a one-card round's bid and which junk card to throw
// on trick six of an eight-card round, which is why it lives in one place. See
// §3 of docs/romanian-whist-cyborg-ai.md.
//
// Everything here is a pure function of an OddsContext. The struct is a plain
// aggregate on purpose: a test can brace-initialise the exact position it wants
// with no engine, no memory and no strategy in sight.
//
// NOTE this header deliberately does not know about CyborgKnobs. duckPropensity
// arrives as a plain float, so the dependency runs Halves <- Odds <- Strategy
// and the knobs exist only at the top.
struct OddsContext
{
    unsigned int playerCount = 4;
    unsigned int mySeat = 0;
    std::optional<Suit> trumpSuit = std::nullopt;

    // Cards I cannot see: live, minus my own hand. Some are in opponents' hands
    // and the rest are the undealt remainder.
    common::Mask unseen = 0;

    // How many of `unseen` are actually in somebody's hand - the exact draw size
    // for "is a card that beats mine out there?". The difference between this
    // and popcount(unseen) is the dead pile, which is zero in 8-trick rounds.
    unsigned int handsOutstanding = 0;

    std::array<unsigned int, common::RoundMemory::MaxPlayers> handSize{};
    std::array<std::uint8_t, common::RoundMemory::MaxPlayers> voidMask{}; // bit s is (1 << s)

    float duckPropensity = 0.60f;

    // Every deck-shaped question goes through here rather than through a cached
    // profile, because this is also the range check: getDeckProfile() throws
    // outside 2..6, and that is what makes (playerCount - 1) safe to divide by.
    const common::DeckProfile& profile() const
    {
        return common::getDeckProfile(playerCount);
    }
};

// The one place round memory becomes odds.
//
// `authoritativeTrump` comes from the PlayContext or BetContext the decision was
// handed, NOT from the memory - the context is the engine's own account of the
// position and the memory is a replica of it. Throws std::logic_error if the two
// disagree, which the strategy turns into its heuristic fallback.
OddsContext makeOddsContext(const common::RoundMemory& memory, common::Mask myHand,
                            std::optional<Suit> authoritativeTrump, float duckPropensity);

// §3.1. Cards in `pool` that would beat `c` if `c` were led: higher cards of its
// own suit, plus - when `c` is not a trump - every trump, because a player void
// in the led suit must ruff if they can.
//
// Throws std::invalid_argument for a CardId outside this deck. That check is
// here and nowhere else BECAUSE every other function in this header reaches
// common::maskAbove()/maskBelow() only through this one, and those index a
// 48-entry array without checking.
common::Mask beaters(common::CardId c, common::Mask pool, const OddsContext& ctx);

// §3.3's void correction. Drops from the beater set every card that no live
// opponent may legally hold, since such a card must be in the dead pile.
//
// With no voids proved this equals beaters(c, ctx.unseen) exactly. Late in a
// round it is what turns a high probability into a certainty: when every seat
// that could beat you is void, pLeadWins() returns exactly 1.
common::Mask reachableBeaters(common::CardId c, const OddsContext& ctx);

// §3.2. Nothing any live opponent could hold beats this card, so leading it
// takes the trick. Note this tests reachableBeaters(), not beaters() - a
// stronger reading than the bare definition, and deliberately so.
bool isSureWinner(common::CardId c, const OddsContext& ctx);

// §3.2, the mirror. Leading this card CANNOT win: a beater exists, nothing is
// dead, nothing of the suit ranks below it, so whoever holds the top card of the
// suit must follow with it - and a trump held by a player out of the suit must
// ruff. Opponents' voids do not enter into it.
//
// The "a beater exists" half is not decoration - without it the other conditions
// are vacuously true for a card nothing can beat. It is tested with
// reachableBeaters(), the same question isSureWinner() asks, so the two can never
// both be true of one card - not even when the memory contradicts itself.
bool isSureLoserOnLead(common::CardId c, const OddsContext& ctx);

// §3.3. P(this card takes the trick if I lead it).
//
// Exact in its first term - the opponents' hands together are a uniform subset
// of the unseen cards, so one hypergeometric answers "is a beater out there at
// all". The second term, "would its holder duck instead?", is the estimate, and
// duckPropensity is the knob that absorbs it.
float pLeadWins(common::CardId c, const OddsContext& ctx);

// §3.4. P(this card still wins if I play it now), given it already beats the
// trick's current best card. Only the seats yet to act can take it away, and
// follow-suit restricts what they may play.
//
// `seatsYetToAct` is a count, not a seat list; the seats are derived here so no
// caller repeats the modular arithmetic. Zero means I am last to act, which is a
// certainty rather than an estimate.
float pHolds(common::CardId c, unsigned int seatsYetToAct, Suit leadSuit, const OddsContext& ctx);

} // namespace romanian_whist::cyborg

#endif
