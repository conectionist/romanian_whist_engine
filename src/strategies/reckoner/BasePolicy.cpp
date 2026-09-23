#include <romanian_whist/strategies/reckoner/BasePolicy.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::reckoner
{

namespace
{

CardId highestCardInMask(Mask m, const DeckProfile& profile)
{
    CardId bestC = INVALID_CARD_ID;
    int maxRank = -1;
    Mask rem = m;
    while(rem != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(rem));
        rem &= rem - 1ULL;
        const int r = static_cast<int>(cardRank(c, profile.ranksPerSuit));
        if(r > maxRank)
        {
            maxRank = r;
            bestC = c;
        }
    }
    return bestC;
}

CardId lowestCardInMask(Mask m, const DeckProfile& profile)
{
    CardId bestC = INVALID_CARD_ID;
    int minRank = 1000;
    Mask rem = m;
    while(rem != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(rem));
        rem &= rem - 1ULL;
        const int r = static_cast<int>(cardRank(c, profile.ranksPerSuit));
        if(r < minRank)
        {
            minRank = r;
            bestC = c;
        }
    }
    return bestC;
}

bool cardBeatsHelper(CardId candidate, CardId currentBest, Suit leadSuit,
                     std::optional<Suit> trumpSuit, const DeckProfile& profile)
{
    const unsigned int candSuit = cardSuit(candidate, profile.ranksPerSuit);
    const unsigned int bestSuit = cardSuit(currentBest, profile.ranksPerSuit);
    const unsigned int candRank = cardRank(candidate, profile.ranksPerSuit);
    const unsigned int bestRank = cardRank(currentBest, profile.ranksPerSuit);

    const std::optional<unsigned int> trumpSuitIdx =
        trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*trumpSuit)) : std::nullopt;

    const bool candIsTrump = trumpSuitIdx && candSuit == *trumpSuitIdx;
    const bool bestIsTrump = trumpSuitIdx && bestSuit == *trumpSuitIdx;

    if(candIsTrump || bestIsTrump)
    {
        if(candIsTrump != bestIsTrump)
            return candIsTrump;
        return candRank > bestRank;
    }

    const unsigned int leadSuitIdx = static_cast<unsigned int>(leadSuit);
    const bool candIsLead = candSuit == leadSuitIdx;
    const bool bestIsLead = bestSuit == leadSuitIdx;

    if(candIsLead != bestIsLead)
        return candIsLead;

    if(candIsLead)
        return candRank > bestRank;

    return false;
}

} // namespace

Mask BasePolicy::legalCards(Mask hand, std::optional<Suit> leadSuit,
                            std::optional<Suit> trumpSuit, const DeckProfile& profile)
{
    if(hand == 0 || !leadSuit)
        return hand;

    const Mask leadMask = hand & maskSuit(static_cast<unsigned int>(*leadSuit), profile);
    if(leadMask != 0)
        return leadMask;

    if(trumpSuit)
    {
        const Mask trumpMask = hand & maskSuit(static_cast<unsigned int>(*trumpSuit), profile);
        if(trumpMask != 0)
            return trumpMask;
    }

    return hand;
}

bool BasePolicy::safeFromLaterPlayers(CardId c,
                                     Mask unseen,
                                     const TrickState& trick,
                                     const EvalContext& ctx,
                                     const DeckProfile& profile)
{
    const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
    const std::optional<unsigned int> trumpIdx =
        ctx.trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*ctx.trumpSuit)) : std::nullopt;

    const bool cIsTrump = trumpIdx && cSuit == *trumpIdx;

    for(unsigned int q : trick.playersYetToPlay)
    {
        if(!cIsTrump)
        {
            if(trumpIdx)
            {
                if((unseen & maskSuit(*trumpIdx, profile)) != 0 && !(ctx.voidMask[q] & (1 << *trumpIdx)))
                {
                    return false; // q might ruff
                }
            }
            if(trick.leadSuit)
            {
                const unsigned int lSuit = static_cast<unsigned int>(*trick.leadSuit);
                if((unseen & maskAbove(c, profile)) != 0 && !(ctx.voidMask[q] & (1 << lSuit)))
                {
                    return false; // q might overtake in lead suit
                }
            }
        }
        else
        {
            // c is trump (or no trump exists)
            if((unseen & maskAbove(c, profile)) != 0 && !(ctx.voidMask[q] & (1 << cSuit)))
            {
                return false;
            }
        }
    }

    return true;
}

float BasePolicy::pLed(CardId c,
                      Mask unseen,
                      const EvalContext& ctx,
                      const DeckProfile& profile,
                      bool onlyLaterPlayers,
                      const std::vector<unsigned int>& targetPlayers)
{
    const unsigned int n = popcount(unseen);
    if(n == 0)
        return 1.0f;

    unsigned int targetHandSum = 0;
    if(onlyLaterPlayers)
    {
        for(unsigned int q : targetPlayers)
            targetHandSum += ctx.handSize[q];
    }
    else
    {
        for(unsigned int q = 0; q < ctx.playerCount; ++q)
            targetHandSum += ctx.handSize[q];
    }

    const float qProb = std::min(1.0f, static_cast<float>(targetHandSum) / static_cast<float>(n));
    const unsigned int hi = popcount(unseen & maskAbove(c, profile));
    const float pTop = (hi == 0) ? 1.0f : std::pow(std::max(0.0f, 1.0f - qProb * CATCH), static_cast<float>(hi));

    const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
    const std::optional<unsigned int> trumpIdx =
        ctx.trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*ctx.trumpSuit)) : std::nullopt;

    if(!trumpIdx || cSuit == *trumpIdx)
        return pTop;

    const unsigned int nS = popcount(unseen & maskSuit(cSuit, profile));
    const unsigned int nT = popcount(unseen & maskSuit(*trumpIdx, profile));

    float pRuff = 0.0f;
    if(nT > 0)
    {
        auto checkPlayer = [&](unsigned int o) {
            if(ctx.handSize[o] > 0 && !(ctx.voidMask[o] & (1 << *trumpIdx)))
            {
                const float pVoid = (ctx.voidMask[o] & (1 << cSuit)) ? 1.0f : hyper0(nS, n, ctx.handSize[o]);
                const float pHasT = 1.0f - hyper0(nT, n, ctx.handSize[o]);
                pRuff = 1.0f - (1.0f - pRuff) * (1.0f - pVoid * pHasT);
            }
        };

        if(onlyLaterPlayers)
        {
            for(unsigned int q : targetPlayers)
                checkPlayer(q);
        }
        else
        {
            for(unsigned int o = 0; o < ctx.playerCount; ++o)
                checkPlayer(o);
        }
    }

    return pTop * (1.0f - pRuff);
}

float BasePolicy::danger(CardId c, Mask unseen, const DeckProfile& profile)
{
    const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
    const unsigned int hi = popcount(unseen & maskAbove(c, profile));
    const unsigned int unseenInSuit = popcount(unseen & maskSuit(cSuit, profile));
    return 1.0f - static_cast<float>(hi) / static_cast<float>(unseenInSuit + 1);
}

CardId BasePolicy::shed(Mask Lo, Mask hand, Mask unseen,
                       bool isFollowingSuitOrUnderTrumping,
                       std::optional<Suit> trumpSuit,
                       const DeckProfile& profile)
{
    if(isFollowingSuitOrUnderTrumping)
    {
        return highestCardInMask(Lo, profile);
    }

    const std::optional<unsigned int> trumpIdx =
        trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*trumpSuit)) : std::nullopt;

    const bool holdsTrumps = trumpIdx && (hand & maskSuit(*trumpIdx, profile)) != 0;

    if(holdsTrumps)
    {
        // Discard highest card of longest non-trump suit
        int maxLen = -1;
        unsigned int bestSuit = 0;
        for(unsigned int s = 0; s < 4; ++s)
        {
            if(trumpIdx && s == *trumpIdx)
                continue;
            const Mask loInSuit = Lo & maskSuit(s, profile);
            if(loInSuit == 0)
                continue;
            const int len = popcount(hand & maskSuit(s, profile));
            if(len > maxLen)
            {
                maxLen = len;
                bestSuit = s;
            }
        }
        if(maxLen != -1)
        {
            return highestCardInMask(Lo & maskSuit(bestSuit, profile), profile);
        }
    }

    // Discard argmax danger(c); tie -> shorter suit
    CardId bestC = INVALID_CARD_ID;
    float maxDanger = -1.0f;
    int shortestLen = 1000;

    Mask rem = Lo;
    while(rem != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(rem));
        rem &= rem - 1ULL;

        const float d = danger(c, unseen, profile);
        const unsigned int s = cardSuit(c, profile.ranksPerSuit);
        const int len = popcount(hand & maskSuit(s, profile));

        if(d > maxDanger || (std::abs(d - maxDanger) < 1e-4f && len < shortestLen))
        {
            maxDanger = d;
            shortestLen = len;
            bestC = c;
        }
    }

    return (bestC != INVALID_CARD_ID) ? bestC : lowestCardInMask(Lo, profile);
}

CardId BasePolicy::keep(Mask Lo, Mask hand, Mask unseen,
                       bool isFollowingSuit,
                       std::optional<Suit> trumpSuit,
                       const DeckProfile& profile)
{
    if(isFollowingSuit)
    {
        return lowestCardInMask(Lo, profile);
    }

    const std::optional<unsigned int> trumpIdx =
        trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*trumpSuit)) : std::nullopt;

    Mask nonTrumpsInLo = Lo;
    if(trumpIdx)
    {
        const Mask trumpLo = Lo & maskSuit(*trumpIdx, profile);
        if((Lo & ~trumpLo) != 0)
        {
            nonTrumpsInLo = Lo & ~trumpLo;
        }
    }

    // Lowest danger among non-trumps; prefer shortening a suit if holding trumps
    const bool holdsTrumps = trumpIdx && (hand & maskSuit(*trumpIdx, profile)) != 0;

    CardId bestC = INVALID_CARD_ID;
    float minDanger = 100.0f;
    int shortestLen = 1000;

    Mask rem = nonTrumpsInLo;
    while(rem != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(rem));
        rem &= rem - 1ULL;

        const float d = danger(c, unseen, profile);
        const unsigned int s = cardSuit(c, profile.ranksPerSuit);
        const int len = popcount(hand & maskSuit(s, profile));

        if(d < minDanger || (holdsTrumps && std::abs(d - minDanger) < 1e-4f && len < shortestLen))
        {
            minDanger = d;
            shortestLen = len;
            bestC = c;
        }
    }

    return (bestC != INVALID_CARD_ID) ? bestC : lowestCardInMask(Lo, profile);
}

PlayIntent BasePolicy::balanceDecide(CardId cW, CardId cL,
                                    Mask hand,
                                    unsigned int seat,
                                    int need,
                                    bool isFollowing,
                                    bool cWInSureW,
                                    const TrickState& trick,
                                    const EvalContext& ctx,
                                    const DeckProfile& profile)
{
    const Mask unseen = ctx.live & ~hand;

    float pW = 1.0f;
    if(isFollowing)
    {
        pW = cWInSureW ? 1.0f : pLed(cW, unseen, ctx, profile, /*onlyLaterPlayers=*/true, trick.playersYetToPlay);
    }
    else
    {
        pW = pLed(cW, unseen, ctx, profile);
    }

    float pL = 1.0f;
    if(!isFollowing)
    {
        pL = 1.0f - pLed(cL, unseen, ctx, profile);
    }

    const Mask handAfterW = hand & ~cardBit(cW);
    const Mask handAfterL = hand & ~cardBit(cL);

    const float eW = evalHand(handAfterW, seat, ctx);
    const float eL = evalHand(handAfterL, seat, ctx);

    float scoreW = pW * (-std::abs(static_cast<float>(need - 1) - eW)) +
                   (1.0f - pW) * (-std::abs(static_cast<float>(need) - eW));

    float scoreL = pL * (-std::abs(static_cast<float>(need) - eL)) +
                   (1.0f - pL) * (-std::abs(static_cast<float>(need - 1) - eL));

    const unsigned int cWSuit = cardSuit(cW, profile.ranksPerSuit);
    const std::optional<unsigned int> trumpIdx =
        ctx.trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*ctx.trumpSuit)) : std::nullopt;

    const bool cWIsTrump = trumpIdx && cWSuit == *trumpIdx;
    const unsigned int hi = popcount(unseen & maskAbove(cW, profile));
    const unsigned int unseenInSuit = popcount(unseen & maskSuit(cWSuit, profile));

    if(!cWIsTrump && hi == 0 && unseenInSuit <= (ctx.playerCount - 1))
    {
        scoreW += WASTE_BONUS;
    }

    return (scoreW >= scoreL) ? PlayIntent::WantWin : PlayIntent::WantLose;
}

CardId BasePolicy::chooseCard(Mask hand,
                             unsigned int seat,
                             int target,
                             unsigned int won,
                             unsigned int remTricks,
                             const TrickState& trick,
                             const EvalContext& ctx)
{
    const auto& profile = getDeckProfile(ctx.playerCount);
    const Mask legal = legalCards(hand, trick.leadSuit, ctx.trumpSuit, profile);

    if(legal == 0)
        return INVALID_CARD_ID;
    if(popcount(legal) == 1)
        return static_cast<CardId>(std::countr_zero(legal));

    const int need = target - static_cast<int>(won);
    const int rem = static_cast<int>(remTricks);

    enum class Mode { Duck, Take, Balance };
    Mode mode = Mode::Balance;
    if(need <= 0)
        mode = Mode::Duck;
    else if(need >= rem)
        mode = Mode::Take;

    const Mask unseen = ctx.live & ~hand;
    const bool isFollowing = trick.leadSuit.has_value();

    // A plain index and a flag rather than an optional, which is what the five
    // other functions in this file still use. Not an inconsistency to tidy: GCC
    // reports THIS one's payload as maybe-uninitialized in optimized builds even
    // though every read is under its own guard, and the five that do not warn
    // were left alone because rewriting them risks moving Reckoner's play for no
    // gain. `trumpIdx` is read only where `hasTrump` is true.
    // The no-trump value indexes nothing, deliberately. 0 would be hearts, so a
    // read that ever escaped its `hasTrump` guard would quietly treat a real
    // suit as trump; ~0u makes the same slip fail loudly, which is the one
    // property the optional was still buying us here.
    constexpr unsigned int NoTrumpSuit = ~0u;

    const bool hasTrump = ctx.trumpSuit.has_value();
    const unsigned int trumpIdx =
        hasTrump ? static_cast<unsigned int>(*ctx.trumpSuit) : NoTrumpSuit;

    if(!isFollowing)
    {
        // LEADING (§4.4)
        auto computeLeadingCard = [&](PlayIntent intent) -> CardId {
            if(intent == PlayIntent::WantLose)
            {
                CardId bestC = INVALID_CARD_ID;
                float minPLed = 100.0f;

                Mask remCards = legal;
                while(remCards != 0)
                {
                    const CardId c = static_cast<CardId>(std::countr_zero(remCards));
                    remCards &= remCards - 1ULL;

                    const float p = pLed(c, unseen, ctx, profile);
                    const unsigned int s = cardSuit(c, profile.ranksPerSuit);
                    const bool isTrump = hasTrump && s == trumpIdx;

                    if(bestC == INVALID_CARD_ID || p < minPLed)
                    {
                        minPLed = p;
                        bestC = c;
                    }
                    else if(std::abs(p - minPLed) < 1e-4f)
                    {
                        const unsigned int bestS = cardSuit(bestC, profile.ranksPerSuit);
                        const bool bestIsTrump = hasTrump && bestS == trumpIdx;

                        // Tie-breaks:
                        // 1. Non-trump over trump
                        if(!isTrump && bestIsTrump)
                        {
                            bestC = c;
                            continue;
                        }
                        if(isTrump && !bestIsTrump)
                            continue;

                        // 2. Suit with most live cards outside
                        const unsigned int cLiveOutside = popcount(unseen & maskSuit(s, profile));
                        const unsigned int bestLiveOutside = popcount(unseen & maskSuit(bestS, profile));
                        if(cLiveOutside > bestLiveOutside)
                        {
                            bestC = c;
                            continue;
                        }
                        if(cLiveOutside < bestLiveOutside)
                            continue;

                        // 3. Longest suit if holding trumps, else shortest
                        const bool holdsTrumps = hasTrump && (hand & maskSuit(trumpIdx, profile)) != 0;
                        const int cLen = popcount(hand & maskSuit(s, profile));
                        const int bestLen = popcount(hand & maskSuit(bestS, profile));
                        if(holdsTrumps)
                        {
                            if(cLen > bestLen)
                            {
                                bestC = c;
                                continue;
                            }
                            if(cLen < bestLen)
                                continue;
                        }
                        else
                        {
                            if(cLen < bestLen)
                            {
                                bestC = c;
                                continue;
                            }
                            if(cLen > bestLen)
                                continue;
                        }

                        // 4. Lowest rank
                        if(cardRank(c, profile.ranksPerSuit) < cardRank(bestC, profile.ranksPerSuit))
                        {
                            bestC = c;
                        }
                    }
                }
                return (bestC != INVALID_CARD_ID) ? bestC : lowestCardInMask(legal, profile);
            }
            else // WantWin
            {
                // Find certain winners C
                Mask C = 0;
                Mask remCards = legal;
                while(remCards != 0)
                {
                    const CardId c = static_cast<CardId>(std::countr_zero(remCards));
                    remCards &= remCards - 1ULL;

                    const unsigned int s = cardSuit(c, profile.ranksPerSuit);
                    const unsigned int hi = popcount(unseen & maskAbove(c, profile));
                    const bool isTrump = hasTrump && s == trumpIdx;

                    bool allVoidTrump = true;
                    if(hasTrump)
                    {
                        for(unsigned int o = 0; o < ctx.playerCount; ++o)
                        {
                            if(o != seat && ctx.handSize[o] > 0 && !(ctx.voidMask[o] & (1 << trumpIdx)))
                            {
                                allVoidTrump = false;
                                break;
                            }
                        }
                    }

                    const bool certain = (hi == 0) && (!hasTrump || isTrump ||
                                                       popcount(unseen & maskSuit(trumpIdx, profile)) == 0 ||
                                                       allVoidTrump);
                    if(certain)
                        C |= cardBit(c);
                }

                if(C != 0)
                {
                    if(hasTrump && (C & maskSuit(trumpIdx, profile)) != 0 &&
                       popcount(unseen & maskSuit(trumpIdx, profile)) > 0 && need >= 2)
                    {
                        return highestCardInMask(C & maskSuit(trumpIdx, profile), profile);
                    }

                    // Suit with most live cards outside, lowest rank there
                    unsigned int maxLiveOutside = 0;
                    unsigned int bestSuit = 0;
                    for(unsigned int s = 0; s < 4; ++s)
                    {
                        if((C & maskSuit(s, profile)) != 0)
                        {
                            const unsigned int liveOut = popcount(unseen & maskSuit(s, profile));
                            if(liveOut >= maxLiveOutside)
                            {
                                maxLiveOutside = liveOut;
                                bestSuit = s;
                            }
                        }
                    }
                    return lowestCardInMask(C & maskSuit(bestSuit, profile), profile);
                }

                // argmax pLed(c); tie -> higher rank
                CardId bestC = INVALID_CARD_ID;
                float maxPLed = -1.0f;
                remCards = legal;
                while(remCards != 0)
                {
                    const CardId c = static_cast<CardId>(std::countr_zero(remCards));
                    remCards &= remCards - 1ULL;

                    const float p = pLed(c, unseen, ctx, profile);
                    if(p > maxPLed || (std::abs(p - maxPLed) < 1e-4f &&
                                       bestC != INVALID_CARD_ID &&
                                       cardRank(c, profile.ranksPerSuit) > cardRank(bestC, profile.ranksPerSuit)))
                    {
                        maxPLed = p;
                        bestC = c;
                    }
                }
                return (bestC != INVALID_CARD_ID) ? bestC : highestCardInMask(legal, profile);
            }
        };

        if(mode == Mode::Duck)
            return computeLeadingCard(PlayIntent::WantLose);
        if(mode == Mode::Take)
            return computeLeadingCard(PlayIntent::WantWin);

        const CardId cW = computeLeadingCard(PlayIntent::WantWin);
        const CardId cL = computeLeadingCard(PlayIntent::WantLose);
        const PlayIntent intent = balanceDecide(cW, cL, hand, seat, need,
                                               /*isFollowing=*/false, /*cWInSureW=*/false,
                                               trick, ctx, profile);
        return (intent == PlayIntent::WantWin) ? cW : cL;
    }
    else
    {
        // FOLLOWING (§4.3)
        Mask W = 0;
        Mask Lo = 0;
        Mask sureW = 0;

        Mask remCards = legal;
        while(remCards != 0)
        {
            const CardId c = static_cast<CardId>(std::countr_zero(remCards));
            remCards &= remCards - 1ULL;

            if(cardBeatsHelper(c, trick.bestCard, *trick.leadSuit, ctx.trumpSuit, profile))
            {
                W |= cardBit(c);
                if(safeFromLaterPlayers(c, unseen, trick, ctx, profile))
                {
                    sureW |= cardBit(c);
                }
            }
            else
            {
                Lo |= cardBit(c);
            }
        }

        const bool winnersScarce = evalHand(hand, seat, ctx) <= (static_cast<float>(need) + 0.5f);
        const bool amLastToPlay = trick.playersYetToPlay.empty();

        const unsigned int leadSuitIdx = static_cast<unsigned int>(*trick.leadSuit);
        const bool isFollowingSuit = (hand & maskSuit(leadSuitIdx, profile)) != 0;
        const bool isUnderTrumping = hasTrump && (hand & maskSuit(trumpIdx, profile)) != 0 &&
                                     cardSuit(trick.bestCard, profile.ranksPerSuit) == trumpIdx;

        auto computeFollowingCard = [&](PlayIntent intent) -> CardId {
            if(intent == PlayIntent::WantLose)
            {
                if(Lo != 0)
                {
                    return shed(Lo, hand, unseen, isFollowingSuit || isUnderTrumping, ctx.trumpSuit, profile);
                }
                return highestCardInMask(W, profile);
            }
            else // WantWin
            {
                if(sureW != 0)
                {
                    return lowestCardInMask(sureW, profile);
                }
                if(W != 0)
                {
                    if(amLastToPlay || !winnersScarce)
                    {
                        return lowestCardInMask(W, profile);
                    }
                    return highestCardInMask(W, profile);
                }
                return keep(Lo, hand, unseen, isFollowingSuit, ctx.trumpSuit, profile);
            }
        };

        if(mode == Mode::Duck)
            return computeFollowingCard(PlayIntent::WantLose);
        if(mode == Mode::Take)
            return computeFollowingCard(PlayIntent::WantWin);

        // BALANCE (§4.6)
        if(W == 0)
            return computeFollowingCard(PlayIntent::WantLose);
        if(Lo == 0)
            return computeFollowingCard(PlayIntent::WantWin);

        const CardId cW = computeFollowingCard(PlayIntent::WantWin);
        const CardId cL = computeFollowingCard(PlayIntent::WantLose);
        const bool cWInSureW = (sureW & cardBit(cW)) != 0;

        const PlayIntent intent = balanceDecide(cW, cL, hand, seat, need,
                                               /*isFollowing=*/true, cWInSureW,
                                               trick, ctx, profile);
        return (intent == PlayIntent::WantWin) ? cW : cL;
    }
}

} // namespace romanian_whist::reckoner
