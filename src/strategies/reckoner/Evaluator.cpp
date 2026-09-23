#include <romanian_whist/strategies/reckoner/Evaluator.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::reckoner
{

float evalHand(Mask hand, unsigned int seat, const EvalContext& ctx)
{
    if(hand == 0)
        return 0.0f;

    const auto& profile = getDeckProfile(ctx.playerCount);
    const Mask U = ctx.live & ~hand;
    const unsigned int n = popcount(U);

    unsigned int others = 0;
    for(unsigned int q = 0; q < ctx.playerCount; ++q)
    {
        if(q != seat)
            others += ctx.handSize[q];
    }

    const float qProb = (n > 0) ? std::min(1.0f, static_cast<float>(others) / static_cast<float>(n)) : 0.0f;
    float E = 0.0f;

    // A plain index and a flag rather than an optional, deliberately: GCC cannot
    // follow that the optional's payload is only read under its own guard and
    // calls it maybe-uninitialized in optimized builds. `trumpSuitIdx` is read
    // only where `hasTrump` is true; the 0 is never used as a suit.
    //
    // The same idiom is still written as an optional elsewhere in this file's
    // neighbours - see the note in BasePolicy.cpp. Only the two that warned were
    // changed, because every rewrite here is a chance to move Reckoner's play.
    const bool hasTrump = ctx.trumpSuit.has_value();
    const unsigned int trumpSuitIdx = hasTrump ? static_cast<unsigned int>(*ctx.trumpSuit) : 0u;

    const unsigned int nT = hasTrump ? popcount(U & maskSuit(trumpSuitIdx, profile)) : 0;

    Mask remaining = hand;
    while(remaining != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(remaining));
        remaining &= remaining - 1ULL;

        const unsigned int s = cardSuit(c, profile.ranksPerSuit);
        const unsigned int hi = popcount(U & maskAbove(c, profile));

        float pTop = 1.0f;
        if(hi > 0)
        {
            const float base = std::max(0.0f, 1.0f - qProb * CATCH);
            pTop = std::pow(base, static_cast<float>(hi));
        }

        float pW = pTop;
        if(hasTrump && s != trumpSuitIdx)
        {
            const unsigned int nS = popcount(U & maskSuit(s, profile));
            float pRuff = 0.0f;
            if(nT > 0)
            {
                for(unsigned int o = 0; o < ctx.playerCount; ++o)
                {
                    if(o != seat && ctx.handSize[o] > 0 && !(ctx.voidMask[o] & (1 << trumpSuitIdx)))
                    {
                        const float pVoid = (ctx.voidMask[o] & (1 << s)) ? 1.0f : hyper0(nS, n, ctx.handSize[o]);
                        const float pHasT = 1.0f - hyper0(nT, n, ctx.handSize[o]);
                        pRuff = 1.0f - (1.0f - pRuff) * (1.0f - pVoid * pHasT);
                    }
                }
            }
            pW = pTop * (1.0f - pRuff);
        }

        E += pW;
    }

    if(hasTrump)
    {
        const unsigned int myT = popcount(hand & maskSuit(trumpSuitIdx, profile));
        const float oppT = qProb * static_cast<float>(nT);

        unsigned int shortSuits = 0;
        for(unsigned int s = 0; s < 4; ++s)
        {
            if(s != trumpSuitIdx)
            {
                const unsigned int myCount = popcount(hand & maskSuit(s, profile));
                const unsigned int uCount = popcount(U & maskSuit(s, profile));
                if(myCount <= 1 && uCount > 0)
                {
                    shortSuits++;
                }
            }
        }

        E += RUFF_W * std::min(static_cast<float>(myT), static_cast<float>(shortSuits));
        E += LONG_W * std::max(0.0f, static_cast<float>(myT) - oppT - 1.0f);
    }

    return std::clamp(E, 0.0f, static_cast<float>(popcount(hand)));
}

} // namespace romanian_whist::reckoner
