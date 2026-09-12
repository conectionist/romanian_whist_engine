#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::reckoner
{

void ReckonerTracker::initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                                unsigned int openerSeat, std::optional<Card> trump)
{
    common::RoundMemory::initRound(n, r, mySeatIdx, openerSeat, trump);

    constraints.clear();

    // Compute initial context for o's starting hand evaluation
    initialContext.playerCount = playerCount;
    initialContext.live = live;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        initialContext.handSize[p] = roundTrickCount;
        initialContext.voidMask[p] = 0;
    }
    initialContext.trumpSuit = trumpSuit;
}

PrePlayState ReckonerTracker::recordCardPlayed(unsigned int seat, const Card& card)
{
    if(seat >= playerCount)
        return PrePlayState{};

    const CardId c = cardToId(card, playerCount);

    // The base records the card and draws the certain inferences, and hands
    // back the position the player was choosing from.
    const PrePlayState before = common::RoundMemory::recordCardPlayed(seat, card);

    // Soft rank-gap inferences (§1.4). Nothing below reads currentTrickCards,
    // so it does not matter that the base has already appended to it.
    if(seat != mySeat && before.leadSuit && before.bestBefore != INVALID_CARD_ID)
    {
        applyRankGapInferences(seat, c, before.bestBefore, before.bestHolderBefore, before.leadSuit);
    }

    return before;
}

void ReckonerTracker::applyRankGapInferences(unsigned int p, CardId c, CardId bestBefore,
                                            unsigned int /*bestHolderBefore*/, std::optional<Suit> leadSuit)
{
    if(!leadSuit || rankGapConfScale <= 0.0f)
        return;

    const auto& profile = getDeckProfile(playerCount);
    const int bid = bids[p];
    if(bid == UNBID)
        return;

    const int need = bid - static_cast<int>(won[p]);
    const int rem = static_cast<int>(roundTrickCount - completedTricks.size());

    enum class Mode { Duck, Take, Balance };
    Mode mode = Mode::Balance;
    if(need <= 0)
        mode = Mode::Duck;
    else if(need >= rem)
        mode = Mode::Take;

    const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
    const unsigned int lSuit = static_cast<unsigned int>(*leadSuit);
    const unsigned int bSuit = cardSuit(bestBefore, profile.ranksPerSuit);
    const bool beatsBest = cardBeats(c, bestBefore, *leadSuit);

    const std::optional<unsigned int> trumpSuitIdx =
        trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*trumpSuit)) : std::nullopt;

    const bool isDeterministic = opponentModels[p].isDeterministic;
    const float baseConfScale = isDeterministic ? 1.25f : rankGapConfScale;

    auto addConstraint = [&](Mask excluded, float conf) {
        if(excluded != 0)
        {
            float finalConf = std::min(0.98f, conf * baseConfScale);
            constraints.push_back(SoftConstraint{p, excluded, finalConf});
        }
    };

    if(cSuit == lSuit)
    {
        // Followed suit
        if(mode == Mode::Duck)
        {
            if(!beatsBest)
            {
                if(bSuit == lSuit)
                {
                    // No L-card in (rank(c), rank(B))
                    addConstraint(maskBetween(c, bestBefore, profile), 0.80f);
                }
                else if(trumpSuitIdx && bSuit == *trumpSuitIdx)
                {
                    // B is trump: no L-card above rank(c)
                    addConstraint(maskAbove(c, profile), 0.80f);
                }
            }
            else
            {
                // Forced win: no L-card below rank(B) and none above rank(c)
                Mask excl = maskBelow(bestBefore, profile) | maskAbove(c, profile);
                addConstraint(excl, 0.85f);
            }
        }
        else if(mode == Mode::Take)
        {
            if(!beatsBest)
            {
                if(bSuit == lSuit)
                {
                    // No L-card above rank(B)
                    addConstraint(maskAbove(bestBefore, profile), 0.90f);
                }
            }
            else
            {
                // Won: no L-card in (rank(B), rank(c))
                if(bSuit == lSuit)
                {
                    addConstraint(maskBetween(bestBefore, c, profile), 0.70f);
                }
            }
        }
        else // Balance
        {
            if(!beatsBest)
            {
                if(bSuit == lSuit)
                {
                    addConstraint(maskBetween(c, bestBefore, profile), 0.35f);
                }
                else if(trumpSuitIdx && bSuit == *trumpSuitIdx)
                {
                    addConstraint(maskAbove(c, profile), 0.35f);
                }
            }
            else
            {
                if(bSuit == lSuit)
                {
                    addConstraint(maskBetween(bestBefore, c, profile), 0.35f);
                }
            }
        }
    }
    else if(trumpSuitIdx && cSuit == *trumpSuitIdx && bSuit == *trumpSuitIdx)
    {
        // Ruffed when another trump T was already best card
        if(mode == Mode::Duck && !beatsBest)
        {
            // Under-trumped: no trump in (rank(c), rank(T))
            addConstraint(maskBetween(c, bestBefore, profile), 0.80f);
        }
        else if(mode == Mode::Take && beatsBest)
        {
            // Over-trumped: no trump in (rank(T), rank(c))
            addConstraint(maskBetween(bestBefore, c, profile), 0.70f);
        }
        else if(mode == Mode::Balance)
        {
            if(!beatsBest)
                addConstraint(maskBetween(c, bestBefore, profile), 0.35f);
            else
                addConstraint(maskBetween(bestBefore, c, profile), 0.35f);
        }
    }
}

void ReckonerTracker::recordRoundEnd(const std::vector<int>& /*roundScores*/, const std::vector<int>& runningTotals)
{
    // Standings & score leader
    if(runningTotals.size() >= playerCount)
    {
        int maxScore = -100000;
        unsigned int bestSeat = 0;
        for(unsigned int p = 0; p < playerCount; ++p)
        {
            totalScores[p] = runningTotals[p];
            if(totalScores[p] > maxScore)
            {
                maxScore = totalScores[p];
                bestSeat = p;
            }
        }
        scoreLeaderSeat = bestSeat;
    }

    // Streak update for my seat (1-trick rounds don't count for streaks)
    if(roundTrickCount > 1)
    {
        if(bids[mySeat] != UNBID && static_cast<unsigned int>(bids[mySeat]) == won[mySeat])
        {
            myConsecutiveWins++;
            myConsecutiveLosses = 0;
        }
        else
        {
            myConsecutiveLosses++;
            myConsecutiveWins = 0;
        }
    }

    // Cross-round calibration (§1.5)
    for(unsigned int o = 0; o < playerCount; ++o)
    {
        if(o != mySeat && bids[o] != UNBID && !opponentModels[o].isDeterministic)
        {
            const Mask startHand = playedBy[o];
            const float eO = evalHand(startHand, o, initialContext);
            const float rO = static_cast<float>(bids[o]) - eO;

            auto& model = opponentModels[o];
            model.sampleCount++;
            model.sumResiduals += rO;
            model.sumSqResiduals += rO * rO;

            // Bayesian prior: prior_mean = 0, prior_sigma = 1.2, prior_var = 1.44, pseudo-count M = 3
            constexpr float M = 3.0f;
            constexpr float priorMean = 0.0f;
            constexpr float priorVar = 1.44f;

            const float totalCount = M + static_cast<float>(model.sampleCount);
            model.bias = (M * priorMean + model.sumResiduals) / totalCount;

            const float sumSq = M * (priorVar + priorMean * priorMean) + model.sumSqResiduals;
            const float var = (sumSq / totalCount) - (model.bias * model.bias);
            model.sigma = std::sqrt(std::max(0.1f, var));
        }
    }
}

EvalContext ReckonerTracker::makeCurrentEvalContext() const
{
    EvalContext ctx{};
    ctx.playerCount = playerCount;
    ctx.live = live;
    ctx.handSize = handSize;
    ctx.voidMask = voidMask;
    ctx.trumpSuit = trumpSuit;
    return ctx;
}

} // namespace romanian_whist::reckoner
