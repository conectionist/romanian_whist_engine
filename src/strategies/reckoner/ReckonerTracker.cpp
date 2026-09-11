#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::reckoner
{

void ReckonerTracker::initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                                unsigned int openerSeat, std::optional<Card> trump)
{
    // CLAMPED TO THE ARRAY WIDTH, not to the rules. GameEngine::start() already
    // rejects any table outside 2..6 by name, so this cannot bite through the
    // engine today - but every per-seat array below is written by a loop bounded
    // by playerCount, so the cost of it ever biting is a buffer overflow rather
    // than a wrong answer. GCC sees the same thing at -O2 and says so
    // (-Wstringop-overflow on the voidMask write further down).
    //
    // Seats are clamped the same way for the same reason: mySeat and opener index
    // those arrays directly.
    playerCount = std::min(n, MaxPlayers);
    roundTrickCount = r;
    mySeat = std::min(mySeatIdx, MaxPlayers - 1);
    opener = std::min(openerSeat, MaxPlayers - 1);
    currentTrickLeader = opener;

    // THE TRACKER IS NOW WORTH READING, which nothing before this point was. See
    // ReckonerTracker::roundInitialised for what an unset one costs.
    roundInitialised = true;

    const auto& profile = getDeckProfile(playerCount);

    if(trump)
    {
        trumpCard = cardToId(*trump, playerCount);
        trumpSuit = trump->suit;
    }
    else
    {
        trumpCard = std::nullopt;
        trumpSuit = std::nullopt;
    }

    live = profile.fullDeckMask;
    if(trumpCard)
    {
        // The turned-up card is dead
        live &= ~cardBit(*trumpCard);
    }

    // EVERY SLOT, NOT ONLY THE SEATS IN PLAY. Two reasons, and the second is the
    // one that made this a change rather than a tidy-up:
    //
    //   * a tail left behind is a real hazard the moment a tracker outlives one
    //     table size - reset only up to playerCount, seats 4 and 5 keep the
    //     previous round's bids and hand sizes, and anything that ever reads them
    //     (a sampler bug, a wrong seat) sees a plausible stale number rather than
    //     an empty one; and
    //   * bounded by the ARRAY rather than by a count, this is provably in range,
    //     which the loop it replaced was not. GCC said so at -O2
    //     (-Wstringop-overflow, "writing 1 byte into a region of size 0"), and a
    //     warning in a per-seat write loop is not one to carry.
    for(unsigned int p = 0; p < MaxPlayers; ++p)
    {
        const bool seated = p < playerCount;

        playedBy[p] = 0ULL;
        handSize[p] = seated ? roundTrickCount : 0;
        voidMask[p] = 0;
        bids[p] = UNBID;
        won[p] = 0;
    }

    constraints.clear();
    completedTricks.clear();
    currentTrickCards.clear();

    // Compute initial context for o's starting hand evaluation
    initialContext.playerCount = playerCount;
    initialContext.live = live;
    for(unsigned int p = 0; p < MaxPlayers; ++p)
    {
        initialContext.handSize[p] = (p < playerCount) ? roundTrickCount : 0;
        initialContext.voidMask[p] = 0;
    }
    initialContext.trumpSuit = trumpSuit;
}

void ReckonerTracker::recordBid(unsigned int seat, unsigned int value)
{
    if(seat < playerCount)
    {
        bids[seat] = static_cast<int>(value);
    }
}

bool ReckonerTracker::cardBeats(CardId candidate, CardId currentBest, Suit leadSuit) const
{
    const auto& profile = getDeckProfile(playerCount);
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

    // Two off-suit discards: earlier stays ahead
    return false;
}

void ReckonerTracker::recordCardPlayed(unsigned int seat, const Card& card)
{
    if(seat >= playerCount)
        return;

    const CardId c = cardToId(card, playerCount);
    const auto& profile = getDeckProfile(playerCount);

    // Determine current best card and lead suit before this card is played
    CardId bestBefore = INVALID_CARD_ID;
    unsigned int bestHolderBefore = seat;
    std::optional<Suit> leadSuit = std::nullopt;

    if(!currentTrickCards.empty())
    {
        const CardId leadCard = currentTrickCards.front().card;
        leadSuit = static_cast<Suit>(cardSuit(leadCard, profile.ranksPerSuit));
        bestBefore = leadCard;
        bestHolderBefore = currentTrickCards.front().seat;

        for(std::size_t i = 1; i < currentTrickCards.size(); ++i)
        {
            if(cardBeats(currentTrickCards[i].card, bestBefore, *leadSuit))
            {
                bestBefore = currentTrickCards[i].card;
                bestHolderBefore = currentTrickCards[i].seat;
            }
        }
    }

    // Update state per §1.3
    live &= ~cardBit(c);
    playedBy[seat] |= cardBit(c);
    if(handSize[seat] > 0)
    {
        handSize[seat]--;
    }

    // Hard void inferences (§1.3)
    if(leadSuit)
    {
        const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
        const unsigned int lSuit = static_cast<unsigned int>(*leadSuit);

        if(cSuit != lSuit)
        {
            voidMask[seat] |= static_cast<std::uint8_t>(1 << lSuit);

            if(trumpSuit && cSuit != static_cast<unsigned int>(*trumpSuit))
            {
                voidMask[seat] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(*trumpSuit));
            }
        }
    }

    // Soft rank-gap inferences (§1.4)
    if(seat != mySeat && leadSuit && bestBefore != INVALID_CARD_ID)
    {
        applyRankGapInferences(seat, c, bestBefore, bestHolderBefore, leadSuit);
    }

    currentTrickCards.push_back(TrickCard{seat, c});
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

void ReckonerTracker::recordTrickWon(unsigned int winner)
{
    if(winner < playerCount)
    {
        won[winner]++;
        completedTricks.push_back(CompletedTrick{currentTrickLeader, winner, std::move(currentTrickCards)});
        currentTrickCards.clear();
        currentTrickLeader = winner;
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

Mask ReckonerTracker::getUnseenCards(Mask myHand) const
{
    return live & ~myHand;
}

unsigned int ReckonerTracker::getDeadCount() const
{
    unsigned int inHands = 0;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        inHands += handSize[p];
    }
    const unsigned int liveCount = static_cast<unsigned int>(popcount(live));
    return (liveCount >= inHands) ? (liveCount - inHands) : 0;
}

} // namespace romanian_whist::reckoner
