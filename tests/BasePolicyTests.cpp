#include <catch2/catch_test_macros.hpp>

#include <romanian_whist/strategies/reckoner/BasePolicy.h>
#include <romanian_whist/strategies/reckoner/CardMask.h>

using namespace romanian_whist;
using namespace romanian_whist::reckoner;

TEST_CASE("BasePolicy: safeFromLaterPlayers sure win test", "[reckoner][basepolicy]")
{
    const unsigned int n = 4;
    const auto& profile = getDeckProfile(n);

    // Trump is Spades. Trick lead is Hearts.
    // I hold Ace of Hearts. Best card before me is Queen of Hearts.
    const Card aceHearts{Rank::Ace, Suit::Hearts};
    const CardId aceHId = cardToId(aceHearts, n);
    const Mask hand = cardBit(aceHId);

    TrickState trick{};
    trick.leadSuit = Suit::Hearts;
    trick.bestCard = cardToId(Card{Rank::Queen, Suit::Hearts}, n);
    trick.bestHolder = 1;
    trick.playersYetToPlay = {3}; // Player 3 is yet to play

    EvalContext ctx{};
    ctx.playerCount = n;
    ctx.live = profile.fullDeckMask;
    ctx.trumpSuit = Suit::Spades;
    for(unsigned int p = 0; p < n; ++p)
    {
        ctx.handSize[p] = 2;
        ctx.voidMask[p] = 0;
    }

    Mask unseen = ctx.live & ~hand;

    // Case 1: Player 3 might have trump Spades -> Ace of Hearts is NOT safe from later players
    REQUIRE(!BasePolicy::safeFromLaterPlayers(aceHId, unseen, trick, ctx, profile));

    // Case 2: Player 3 is known to be void in trumps
    ctx.voidMask[3] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(Suit::Spades));
    // And Ace is top of Hearts (no cards above Ace in Hearts)
    REQUIRE(BasePolicy::safeFromLaterPlayers(aceHId, unseen, trick, ctx, profile));
}

TEST_CASE("BasePolicy: shed in DUCK mode sheds most dangerous loser", "[reckoner][basepolicy]")
{
    const unsigned int n = 4;
    const auto& profile = getDeckProfile(n);

    // In DUCK mode, following suit when unable to beat current best:
    // Sheds highest card in Lo
    const Card nineClubs{Rank::Nine, Suit::Clubs};
    const Card jackClubs{Rank::Jack, Suit::Clubs};
    const CardId c9 = cardToId(nineClubs, n);
    const CardId cJ = cardToId(jackClubs, n);
    const Mask Lo = cardBit(c9) | cardBit(cJ);

    const CardId chosen = BasePolicy::shed(Lo, Lo, profile.fullDeckMask,
                                          /*isFollowingSuitOrUnderTrumping=*/true,
                                          /*trumpSuit=*/std::nullopt,
                                          profile);
    REQUIRE(chosen == cJ);
}
