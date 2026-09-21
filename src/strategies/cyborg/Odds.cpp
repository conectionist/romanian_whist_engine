#include <romanian_whist/strategies/cyborg/Odds.h>

#include <romanian_whist/strategies/common/Hypergeometric.h>

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <string>

namespace romanian_whist::cyborg
{

namespace
{
// common::popcount returns a signed int and common::hyper0 takes unsigned ints.
// The conversion is well defined for a non-negative value, so this is for the
// reader rather than the compiler - but write it at every site, so that nobody
// later writes `popcount(x) - 1` in int arithmetic and compares it against an
// unsigned.
unsigned int count(common::Mask mask)
{
    return static_cast<unsigned int>(common::popcount(mask));
}

bool isVoidIn(const OddsContext& ctx, unsigned int seat, unsigned int suit)
{
    return (ctx.voidMask[seat] & static_cast<std::uint8_t>(1U << suit)) != 0;
}
} // namespace

OddsContext makeOddsContext(const common::RoundMemory& memory, common::Mask myHand,
                            std::optional<Suit> authoritativeTrump, float duckPropensity)
{
    // The context the decision was handed is the engine's own account of the
    // position; the memory is a replica built from callbacks. Where they
    // disagree the replica is wrong by definition, and continuing would compute
    // a confident answer about a table that does not exist.
    if(memory.trumpSuit != authoritativeTrump)
    {
        throw std::logic_error("cyborg::makeOddsContext: the round memory and the position "
                               "disagree about trump, so the memory is stale");
    }

    OddsContext ctx{};
    ctx.playerCount = memory.playerCount;
    ctx.mySeat = memory.mySeat;
    ctx.trumpSuit = authoritativeTrump;
    ctx.unseen = memory.getUnseenCards(myHand);
    ctx.handsOutstanding = memory.handsOutstanding();
    ctx.handSize = memory.handSize;
    ctx.voidMask = memory.voidMask;
    ctx.duckPropensity = duckPropensity;

    return ctx;
}

common::Mask beaters(common::CardId c, common::Mask pool, const OddsContext& ctx)
{
    const common::DeckProfile& profile = ctx.profile();

    // THE bounds check for this whole header. common::maskAbove() and
    // maskBelow() index aboveMasks[48]/belowMasks[48] with no check of their
    // own, so maskAbove(INVALID_CARD_ID, ...) reads 207 elements past the end.
    // There are no sanitizers configured in this project and CI builds Release
    // only, so that would surface as a plausible-looking probability rather than
    // as a crash.
    if(c >= profile.deckSize)
    {
        throw std::invalid_argument("cyborg::beaters: card id " + std::to_string(c) +
                                    " is not in the " + std::to_string(ctx.playerCount) +
                                    "-player deck");
    }

    common::Mask m = common::maskAbove(c, profile) & pool;

    // Any trump beats any plain card, and a player void in the led suit MUST
    // ruff if they hold one - so leading a plain card is not safe until both the
    // higher cards of its suit and every trump are gone. This clause is the one
    // people forget.
    if(ctx.trumpSuit && common::cardSuit(c, profile.ranksPerSuit) !=
                            static_cast<unsigned int>(*ctx.trumpSuit))
    {
        m |= pool & common::maskSuit(static_cast<unsigned int>(*ctx.trumpSuit), profile);
    }

    return m;
}

common::Mask reachableBeaters(common::CardId c, const OddsContext& ctx)
{
    const common::DeckProfile& profile = ctx.profile();

    // Validates `c` and gives the unrestricted set in one call; everything below
    // only ever removes from it.
    const common::Mask all = beaters(c, ctx.unseen, ctx);
    if(all == 0)
        return 0;

    const unsigned int suit = common::cardSuit(c, profile.ranksPerSuit);
    const common::Mask suitHalf = common::maskAbove(c, profile) & ctx.unseen;
    const common::Mask trumpHalf = all & ~suitHalf;

    common::Mask reachable = 0;

    for(unsigned int seat = 0; seat < ctx.playerCount; ++seat)
    {
        if(seat == ctx.mySeat || ctx.handSize[seat] == 0)
            continue;

        if(!isVoidIn(ctx, seat, suit))
            reachable |= suitHalf;

        if(trumpHalf != 0 && ctx.trumpSuit &&
           !isVoidIn(ctx, seat, static_cast<unsigned int>(*ctx.trumpSuit)))
        {
            reachable |= trumpHalf;
        }
    }

    return reachable;
}

bool isSureWinner(common::CardId c, const OddsContext& ctx)
{
    return reachableBeaters(c, ctx) == 0;
}

bool isSureLoserOnLead(common::CardId c, const OddsContext& ctx)
{
    const common::DeckProfile& profile = ctx.profile();

    // FIRST, and not decoration: every condition below is vacuously true for a
    // card nothing can beat, so without this an ace with no lower cards live
    // reports as a sure loser.
    //
    // And reachableBeaters(), not beaters(), so that this asks exactly the
    // question isSureWinner() asks. In a consistent deal the two agree here -
    // nothing is dead, so every beater is in a hand, and its holder is not void
    // in its suit. They part company only when the memory contradicts itself
    // (every opponent recorded void in a suit whose top card is still out), and
    // then beaters() made the same card a sure winner AND a sure loser.
    // reachableBeaters() also carries the CardId bounds check, which must run
    // before the maskBelow() below.
    if(reachableBeaters(c, ctx) == 0)
        return false;

    // Something is undealt, so the beater need not be in anybody's hand.
    if(count(ctx.unseen) != ctx.handsOutstanding)
        return false;

    // A lower card of the suit is live, so the beater's holder can duck under.
    if((ctx.unseen & common::maskBelow(c, profile)) != 0)
        return false;

    // Deliberately NO void test here. Every beater is in somebody's hand: a
    // higher card of the suit must be followed with, since nothing lower is
    // live, and a trump held by a player with none of the suit must ruff. A
    // third player's void changes neither. An earlier "no opponent is void"
    // guard returned false in exactly those healthy positions - including every
    // one where only trumps beat c, since then nobody holds the suit at all.
    return true;
}

float pLeadWins(common::CardId c, const OddsContext& ctx)
{
    // First statement, and not for the profile: this is the range check that
    // makes (playerCount - 1) safe to divide by further down. RoundMemory
    // clamps playerCount from above but not below, so a memory initialised with
    // a count of 1 reaches here otherwise.
    const common::DeckProfile& profile = ctx.profile();

    // Nobody is holding anything, so whatever I lead takes the trick. Returning
    // here is correct, not merely safe - and it is also the root cause of the
    // underflow the hbar clamp below would otherwise have to absorb.
    if(ctx.handsOutstanding == 0)
        return 1.0f;

    const unsigned int unseenCount = count(ctx.unseen);
    if(unseenCount == 0)
        return 1.0f;

    // Must come BEFORE the maskBelow() below: beaters() carries the CardId
    // bounds check for this function. Reordering these two lines for
    // readability reintroduces an out-of-bounds read.
    const common::Mask beaterSet = reachableBeaters(c, ctx);
    if(beaterSet == 0)
        return 1.0f;

    const float pNoneOut = common::hyper0(count(beaterSet), unseenCount, ctx.handsOutstanding);

    // A beater is out there. I still win if its holder would rather duck, which
    // they can only do holding a lower card of the same suit. maskBelow is
    // already restricted to c's suit, so there is no maskSuit term to add.
    const common::Mask lower = ctx.unseen & common::maskBelow(c, profile);
    const unsigned int lowerCount = count(lower);

    // Clamped, not returned. On correct memory this never fires: every seat
    // holds the same number of cards, so H is (N-1) * tricksRemaining and hbar
    // is tricksRemaining exactly. It fires on DRIFT - recordCardPlayed() floors
    // handSize at zero, so a memory that missed a callback can reach
    // 0 < H < N-1. Unclamped, hbar - 1 would then wrap to 4294967295, hyper0
    // would return 0, pCanDuck would become 1, and this function would return
    // its MAXIMUM at exactly the moment its inputs are least trustworthy.
    const unsigned int hbar = std::max(1u, ctx.handsOutstanding / (ctx.playerCount - 1));

    const float pCanDuck =
        (lowerCount == 0) ? 0.0f : 1.0f - common::hyper0(lowerCount, unseenCount - 1, hbar - 1);

    return pNoneOut + (1.0f - pNoneOut) * ctx.duckPropensity * pCanDuck;
}

float pHolds(common::CardId c, unsigned int seatsYetToAct, Suit leadSuit, const OddsContext& ctx)
{
    const common::DeckProfile& profile = ctx.profile();

    // Last to act: nobody is left to take it away. A certainty, not an estimate.
    if(seatsYetToAct == 0)
        return 1.0f;

    const unsigned int unseenCount = count(ctx.unseen);
    if(unseenCount == 0)
        return 1.0f;

    const unsigned int leadSuitIdx = static_cast<unsigned int>(leadSuit);

    common::Mask pool = 0;
    unsigned int outstanding = 0;

    for(unsigned int step = 1; step <= seatsYetToAct; ++step)
    {
        const unsigned int seat = (ctx.mySeat + step) % ctx.playerCount;

        if(seat == ctx.mySeat || ctx.handSize[seat] == 0)
            continue;

        outstanding += ctx.handSize[seat];

        // Not PROVED void is not the same as holding the suit. A seat with no
        // void shown may follow, or may turn out to be void and be forced to
        // ruff - so it can hurt me with either, exactly as reachableBeaters()
        // counts it.
        if(!isVoidIn(ctx, seat, leadSuitIdx))
            pool |= ctx.unseen & common::maskSuit(leadSuitIdx, profile);

        if(ctx.trumpSuit && !isVoidIn(ctx, seat, static_cast<unsigned int>(*ctx.trumpSuit)))
            pool |= ctx.unseen & common::maskSuit(static_cast<unsigned int>(*ctx.trumpSuit), profile);

        // Void in both: whatever they play is an off-suit discard, which cannot
        // win. They contribute nothing, and that is exact.
    }

    if(outstanding == 0)
        return 1.0f;

    // The marks are pool-restricted while the population stays the whole unseen
    // set - the same approximation reachableBeaters() makes, and for the same
    // reason: it is what a card COULD be, not how many are drawn.
    return common::hyper0(count(beaters(c, pool, ctx)), unseenCount, outstanding);
}

ScoreCache makeScoreCache(common::Mask cards, const OddsContext& ctx)
{
    ScoreCache cache;

    common::Mask rest = cards;
    while(rest != 0)
    {
        const auto id = static_cast<common::CardId>(std::countr_zero(rest));
        rest &= rest - 1ULL;

        if(id >= ScoreCache::MaxDeckSize)
            continue;

        // pLeadWins() carries the deck bounds check, so an id this deck cannot
        // hold throws here rather than being silently cached.
        cache.score[id] = pLeadWins(id, ctx);
        cache.known |= common::cardBit(id);
    }

    return cache;
}

} // namespace romanian_whist::cyborg
