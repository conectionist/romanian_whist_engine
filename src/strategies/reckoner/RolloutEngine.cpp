#include <romanian_whist/strategies/reckoner/RolloutEngine.h>
#include <romanian_whist/strategies/reckoner/ReckonerKnobs.h>

#include <algorithm>
#include <cmath>

namespace romanian_whist::reckoner
{

namespace
{

struct RolloutState
{
    std::array<Mask, 6> hands{};
    std::array<unsigned int, 6> won{};
    Mask live = 0;
    std::array<std::uint8_t, 6> voidMask{};
    std::array<unsigned int, 6> handSize{};
    TrickState trick{};
    unsigned int remTricks = 0;
    unsigned int currentLeader = 0;
};

CardId findEquivalentRepresentative(CardId card, const std::vector<CardId>& cand,
                                    Mask unseen, const DeckProfile& profile)
{
    for(CardId c : cand)
    {
        if(card == c)
            return c;
        if(cardSuit(card, profile.ranksPerSuit) == cardSuit(c, profile.ranksPerSuit))
        {
            const CardId low = (cardRank(card, profile.ranksPerSuit) < cardRank(c, profile.ranksPerSuit)) ? card : c;
            const CardId high = (cardRank(card, profile.ranksPerSuit) < cardRank(c, profile.ranksPerSuit)) ? c : card;
            if((unseen & maskBetween(low, high, profile)) == 0)
            {
                return c;
            }
        }
    }
    return cand.empty() ? card : cand.front();
}

} // namespace

float RolloutEngine::scoreUtility(unsigned int myTricks,
                                  const std::array<unsigned int, 6>& won,
                                  int myBid,
                                  const std::array<int, 6>& bids,
                                  const ReckonerTracker& tracker,
                                  const ReckonerKnobs& knobs)
{
    float own = (myTricks == static_cast<unsigned int>(myBid))
                    ? static_cast<float>(5 + myBid)
                    : -static_cast<float>(std::abs(myBid - static_cast<int>(myTricks)));

    if(tracker.roundTrickCount > 1)
    {
        if(myTricks == static_cast<unsigned int>(myBid))
        {
            if(tracker.myConsecutiveWins + 1 == 5)
                own += 10.0f;
        }
        else
        {
            if(tracker.myConsecutiveLosses + 1 == 5)
                own -= 10.0f;
        }
    }

    float sab = 0.0f;
    for(unsigned int o = 0; o < tracker.playerCount; ++o)
    {
        if(o != tracker.mySeat && bids[o] != UNBID)
        {
            const int oppBid = bids[o];
            const unsigned int oppTricks = won[o];
            const float oppScore = (oppTricks == static_cast<unsigned int>(oppBid))
                                       ? static_cast<float>(5 + oppBid)
                                       : -static_cast<float>(std::abs(oppBid - static_cast<int>(oppTricks)));
            const float lam = (o == tracker.scoreLeaderSeat) ? knobs.lambdaLeader : knobs.lambdaDefault;
            sab += lam * oppScore;
        }
    }

    return own - sab;
}

float RolloutEngine::rollout(const DealSample& sample,
                            CardId myFirstCard,
                            Mask myHand,
                            const ReckonerTracker& tracker,
                            const ReckonerKnobs& knobs,
                            const std::array<int, 6>& targets)
{
    const auto& profile = getDeckProfile(tracker.playerCount);

    RolloutState state{};
    state.hands[tracker.mySeat] = myHand;
    for(unsigned int o = 0; o < tracker.playerCount; ++o)
    {
        if(o != tracker.mySeat)
            state.hands[o] = sample.hands[o];
    }
    state.won = tracker.won;
    state.live = tracker.live;
    state.voidMask = tracker.voidMask;
    state.handSize = tracker.handSize;
    state.remTricks = tracker.roundTrickCount - static_cast<unsigned int>(tracker.completedTricks.size());
    state.currentLeader = tracker.currentTrickLeader;

    // Set up current trick state
    if(!tracker.currentTrickCards.empty())
    {
        const CardId leadC = tracker.currentTrickCards.front().card;
        state.trick.leadSuit = static_cast<Suit>(cardSuit(leadC, profile.ranksPerSuit));
        state.trick.bestCard = leadC;
        state.trick.bestHolder = tracker.currentTrickCards.front().seat;

        for(std::size_t i = 1; i < tracker.currentTrickCards.size(); ++i)
        {
            if(tracker.cardBeats(tracker.currentTrickCards[i].card, state.trick.bestCard, *state.trick.leadSuit))
            {
                state.trick.bestCard = tracker.currentTrickCards[i].card;
                state.trick.bestHolder = tracker.currentTrickCards[i].seat;
            }
        }
    }
    else
    {
        state.trick.leadSuit = std::nullopt;
        state.trick.bestCard = INVALID_CARD_ID;
        state.trick.bestHolder = state.currentLeader;
    }

    // Determine players yet to play in current trick
    std::vector<bool> playedInTrick(tracker.playerCount, false);
    for(const auto& tc : tracker.currentTrickCards)
    {
        playedInTrick[tc.seat] = true;
    }

    state.trick.playersYetToPlay.clear();
    unsigned int cur = state.currentLeader;
    for(unsigned int i = 0; i < tracker.playerCount; ++i)
    {
        if(!playedInTrick[cur])
        {
            state.trick.playersYetToPlay.push_back(cur);
        }
        cur = (cur + 1) % tracker.playerCount;
    }

    // Play my card
    state.hands[tracker.mySeat] &= ~cardBit(myFirstCard);
    state.live &= ~cardBit(myFirstCard);
    if(state.handSize[tracker.mySeat] > 0)
        state.handSize[tracker.mySeat]--;

    if(!state.trick.leadSuit)
    {
        state.trick.leadSuit = static_cast<Suit>(cardSuit(myFirstCard, profile.ranksPerSuit));
        state.trick.bestCard = myFirstCard;
        state.trick.bestHolder = tracker.mySeat;
    }
    else
    {
        if(tracker.cardBeats(myFirstCard, state.trick.bestCard, *state.trick.leadSuit))
        {
            state.trick.bestCard = myFirstCard;
            state.trick.bestHolder = tracker.mySeat;
        }
    }

    // Remove mySeat from playersYetToPlay
    auto it = std::find(state.trick.playersYetToPlay.begin(), state.trick.playersYetToPlay.end(), tracker.mySeat);
    if(it != state.trick.playersYetToPlay.end())
    {
        state.trick.playersYetToPlay.erase(it);
    }

    // Finish current trick with remaining players
    while(!state.trick.playersYetToPlay.empty())
    {
        const unsigned int q = state.trick.playersYetToPlay.front();
        state.trick.playersYetToPlay.erase(state.trick.playersYetToPlay.begin());

        EvalContext ctx{};
        ctx.playerCount = tracker.playerCount;
        ctx.live = state.live;
        ctx.handSize = state.handSize;
        ctx.voidMask = state.voidMask;
        ctx.trumpSuit = tracker.trumpSuit;

        const CardId c = BasePolicy::chooseCard(state.hands[q], q, targets[q], state.won[q],
                                               state.remTricks, state.trick, ctx);

        state.hands[q] &= ~cardBit(c);
        state.live &= ~cardBit(c);
        if(state.handSize[q] > 0)
            state.handSize[q]--;

        // Update void inferences
        if(state.trick.leadSuit)
        {
            const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
            const unsigned int lSuit = static_cast<unsigned int>(*state.trick.leadSuit);
            if(cSuit != lSuit)
            {
                state.voidMask[q] |= static_cast<std::uint8_t>(1 << lSuit);
                if(tracker.trumpSuit && cSuit != static_cast<unsigned int>(*tracker.trumpSuit))
                {
                    state.voidMask[q] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(*tracker.trumpSuit));
                }
            }
        }

        if(tracker.cardBeats(c, state.trick.bestCard, *state.trick.leadSuit))
        {
            state.trick.bestCard = c;
            state.trick.bestHolder = q;
        }
    }

    // Resolve current trick
    state.won[state.trick.bestHolder]++;
    state.remTricks--;
    state.currentLeader = state.trick.bestHolder;

    // Play remaining tricks to end of round
    while(state.remTricks > 0)
    {
        state.trick.leadSuit = std::nullopt;
        state.trick.bestCard = INVALID_CARD_ID;
        state.trick.bestHolder = state.currentLeader;
        state.trick.playersYetToPlay.clear();

        for(unsigned int i = 1; i < tracker.playerCount; ++i)
        {
            state.trick.playersYetToPlay.push_back((state.currentLeader + i) % tracker.playerCount);
        }

        // Leader plays
        const unsigned int leader = state.currentLeader;
        EvalContext leadCtx{};
        leadCtx.playerCount = tracker.playerCount;
        leadCtx.live = state.live;
        leadCtx.handSize = state.handSize;
        leadCtx.voidMask = state.voidMask;
        leadCtx.trumpSuit = tracker.trumpSuit;

        const CardId leadC = BasePolicy::chooseCard(state.hands[leader], leader, targets[leader],
                                                   state.won[leader], state.remTricks, state.trick, leadCtx);

        state.hands[leader] &= ~cardBit(leadC);
        state.live &= ~cardBit(leadC);
        if(state.handSize[leader] > 0)
            state.handSize[leader]--;

        state.trick.leadSuit = static_cast<Suit>(cardSuit(leadC, profile.ranksPerSuit));
        state.trick.bestCard = leadC;
        state.trick.bestHolder = leader;

        // Followers play
        while(!state.trick.playersYetToPlay.empty())
        {
            const unsigned int q = state.trick.playersYetToPlay.front();
            state.trick.playersYetToPlay.erase(state.trick.playersYetToPlay.begin());

            EvalContext ctx{};
            ctx.playerCount = tracker.playerCount;
            ctx.live = state.live;
            ctx.handSize = state.handSize;
            ctx.voidMask = state.voidMask;
            ctx.trumpSuit = tracker.trumpSuit;

            const CardId c = BasePolicy::chooseCard(state.hands[q], q, targets[q], state.won[q],
                                                   state.remTricks, state.trick, ctx);

            state.hands[q] &= ~cardBit(c);
            state.live &= ~cardBit(c);
            if(state.handSize[q] > 0)
                state.handSize[q]--;

            const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
            const unsigned int lSuit = static_cast<unsigned int>(*state.trick.leadSuit);
            if(cSuit != lSuit)
            {
                state.voidMask[q] |= static_cast<std::uint8_t>(1 << lSuit);
                if(tracker.trumpSuit && cSuit != static_cast<unsigned int>(*tracker.trumpSuit))
                {
                    state.voidMask[q] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(*tracker.trumpSuit));
                }
            }

            if(tracker.cardBeats(c, state.trick.bestCard, *state.trick.leadSuit))
            {
                state.trick.bestCard = c;
                state.trick.bestHolder = q;
            }
        }

        state.won[state.trick.bestHolder]++;
        state.remTricks--;
        state.currentLeader = state.trick.bestHolder;
    }

    return scoreUtility(state.won[tracker.mySeat], state.won, targets[tracker.mySeat], targets, tracker, knobs);
}

CardId RolloutEngine::choosePlay(Mask myHand,
                                const ReckonerTracker& tracker,
                                const ReckonerKnobs& knobs,
                                std::mt19937& rng)
{
    const auto& profile = getDeckProfile(tracker.playerCount);

    std::optional<Suit> leadSuit = std::nullopt;
    CardId bestCard = INVALID_CARD_ID;
    unsigned int bestHolder = tracker.currentTrickLeader;

    if(!tracker.currentTrickCards.empty())
    {
        const CardId leadC = tracker.currentTrickCards.front().card;
        leadSuit = static_cast<Suit>(cardSuit(leadC, profile.ranksPerSuit));
        bestCard = leadC;
        bestHolder = tracker.currentTrickCards.front().seat;

        for(std::size_t i = 1; i < tracker.currentTrickCards.size(); ++i)
        {
            if(tracker.cardBeats(tracker.currentTrickCards[i].card, bestCard, *leadSuit))
            {
                bestCard = tracker.currentTrickCards[i].card;
                bestHolder = tracker.currentTrickCards[i].seat;
            }
        }
    }

    const Mask legal = BasePolicy::legalCards(myHand, leadSuit, tracker.trumpSuit, profile);
    if(legal == 0)
        return INVALID_CARD_ID;
    if(popcount(legal) == 1)
        return static_cast<CardId>(std::countr_zero(legal));

    // Candidate reduction by equivalence classes (§5.1)
    const Mask unseen = tracker.getUnseenCards(myHand);
    std::vector<CardId> candidates;

    for(unsigned int s = 0; s < 4; ++s)
    {
        Mask inSuit = legal & maskSuit(s, profile);
        std::vector<CardId> suitCards;
        while(inSuit != 0)
        {
            const CardId c = static_cast<CardId>(std::countr_zero(inSuit));
            suitCards.push_back(c);
            inSuit &= inSuit - 1ULL;
        }

        std::sort(suitCards.begin(), suitCards.end(), [&](CardId a, CardId b) {
            return cardRank(a, profile.ranksPerSuit) < cardRank(b, profile.ranksPerSuit);
        });

        for(std::size_t i = 0; i < suitCards.size(); ++i)
        {
            if(i == 0)
            {
                candidates.push_back(suitCards[i]);
            }
            else
            {
                const CardId prev = candidates.back();
                const CardId curr = suitCards[i];
                if((unseen & maskBetween(prev, curr, profile)) != 0)
                {
                    candidates.push_back(curr);
                }
            }
        }
    }

    // Mode pruning if following (§5.1)
    const int bid = tracker.bids[tracker.mySeat];
    const int need = (bid != UNBID) ? (bid - static_cast<int>(tracker.won[tracker.mySeat])) : 0;
    const int rem = static_cast<int>(tracker.roundTrickCount - tracker.completedTricks.size());

    enum class Mode { Duck, Take, Balance };
    Mode mode = Mode::Balance;
    if(need <= 0)
        mode = Mode::Duck;
    else if(need >= rem)
        mode = Mode::Take;

    if(leadSuit && bestCard != INVALID_CARD_ID)
    {
        std::vector<CardId> W_cand;
        std::vector<CardId> Lo_cand;
        for(CardId c : candidates)
        {
            if(tracker.cardBeats(c, bestCard, *leadSuit))
                W_cand.push_back(c);
            else
                Lo_cand.push_back(c);
        }

        if(mode == Mode::Duck && !Lo_cand.empty())
            candidates = Lo_cand;
        else if(mode == Mode::Take && !W_cand.empty())
            candidates = W_cand;
    }

    if(candidates.size() == 1)
        return candidates.front();

    // Prepare trick state for BasePolicy
    TrickState trickState{};
    trickState.leadSuit = leadSuit;
    trickState.bestCard = bestCard;
    trickState.bestHolder = bestHolder;

    std::vector<bool> playedInTrick(tracker.playerCount, false);
    for(const auto& tc : tracker.currentTrickCards)
        playedInTrick[tc.seat] = true;

    unsigned int cur = tracker.currentTrickLeader;
    for(unsigned int i = 0; i < tracker.playerCount; ++i)
    {
        if(!playedInTrick[cur] && cur != tracker.mySeat)
            trickState.playersYetToPlay.push_back(cur);
        cur = (cur + 1) % tracker.playerCount;
    }

    const EvalContext ctx = tracker.makeCurrentEvalContext();
    const CardId baseMove = BasePolicy::chooseCard(myHand, tracker.mySeat, bid,
                                                  tracker.won[tracker.mySeat], rem, trickState, ctx);

    const CardId baseRep = findEquivalentRepresentative(baseMove, candidates, unseen, profile);

    // If K_play == 0 (Easy level), use base policy move
    if(knobs.K_play == 0)
    {
        if(knobs.epsilon > 0.0f)
        {
            std::uniform_real_distribution<float> uDist(0.0f, 1.0f);
            if(uDist(rng) < knobs.epsilon)
            {
                std::vector<CardId> allLegal;
                Mask remM = legal;
                while(remM != 0)
                {
                    allLegal.push_back(static_cast<CardId>(std::countr_zero(remM)));
                    remM &= remM - 1ULL;
                }
                std::uniform_int_distribution<std::size_t> iDist(0, allLegal.size() - 1);
                return allLegal[iDist(rng)];
            }
        }
        return baseRep;
    }

    // Draw samples
    const auto samples = Sampler::drawSamples(knobs.K_play, tracker, myHand, rng);
    if(samples.empty())
        return baseRep;

    // Targets for rollouts: use bids if known, else guess
    std::array<int, 6> targets{};
    for(unsigned int p = 0; p < tracker.playerCount; ++p)
    {
        targets[p] = (tracker.bids[p] != UNBID) ? tracker.bids[p] : 0;
    }

    CardId bestCandidate = baseRep;
    float maxVal = -100000.0f;
    float baseVal = -100000.0f;

    for(CardId c : candidates)
    {
        float val = 0.0f;
        float totalW = 0.0f;
        for(const auto& s : samples)
        {
            const float score = rollout(s, c, myHand, tracker, knobs, targets);
            val += s.weight * score;
            totalW += s.weight;
        }
        if(totalW > 0.0f)
            val /= totalW;

        if(c == baseRep)
            baseVal = val;

        if(val > maxVal)
        {
            maxVal = val;
            bestCandidate = c;
        }
    }

    // Regularization (§5.4): if best candidate doesn't beat base policy by MARGIN, stick to base
    CardId chosen = bestCandidate;
    if(maxVal - baseVal < knobs.margin)
    {
        chosen = baseRep;
    }

    if(knobs.epsilon > 0.0f)
    {
        std::uniform_real_distribution<float> uDist(0.0f, 1.0f);
        if(uDist(rng) < knobs.epsilon)
        {
            std::vector<CardId> allLegal;
            Mask remM = legal;
            while(remM != 0)
            {
                allLegal.push_back(static_cast<CardId>(std::countr_zero(remM)));
                remM &= remM - 1ULL;
            }
            std::uniform_int_distribution<std::size_t> iDist(0, allLegal.size() - 1);
            return allLegal[iDist(rng)];
        }
    }

    return chosen;
}

unsigned int RolloutEngine::chooseBid(unsigned int R,
                                     Mask myHand,
                                     std::optional<unsigned int> forbiddenBet,
                                     const ReckonerTracker& tracker,
                                     const ReckonerKnobs& knobs,
                                     std::mt19937& rng)
{
    // 1-trick rounds (§6.2)
    if(R == 1)
    {
        if(forbiddenBet && *forbiddenBet == 1)
            return 0;
        if(forbiddenBet && *forbiddenBet == 0)
            return 1;

        // Estimate P(win) using samples
        const unsigned int sampleCount = (knobs.K_bid > 0) ? 200 : 0;
        if(sampleCount > 0)
        {
            const auto samples = Sampler::drawSamples(sampleCount, tracker, myHand, rng);

            // P(win), by playing the single trick out in each sampled deal. A
            // one-card round needs no rollout: there is one card to play and one
            // trick to win, so the trick itself IS the whole simulation.
            //
            // A loop that called rollout() on every sample stood here and threw
            // each answer away - 200 discarded rollouts per bid, at any preset
            // with K_bid > 0. Removing it changed no bid and consumed no RNG,
            // since rollout() draws nothing. It is not a speed-up worth claiming
            // either: a rollout of a one-card hand is a single trick, and the
            // [reckoner] tests time the same before and after. Dead, not slow.
            //
            // The samples are IMPORTANCE-weighted - drawSamples() scores each
            // deal by how well it explains the bids already made, then
            // normalises - so they have to be averaged by weight, exactly as the
            // R >= 2 path below does. Counting them one apiece would estimate
            // the win rate of the PROPOSAL rather than of the posterior, which
            // near the 6/13 threshold is the difference between bidding 1 and 0.
            float winWeight = 0.0f;
            float totalWeight = 0.0f;
            const CardId myCard = static_cast<CardId>(std::countr_zero(myHand));
            const auto& profile = getDeckProfile(tracker.playerCount);

            for(const auto& s : samples)
            {
                TrickState trick{};

                // Play in seat order from opener
                unsigned int cur = tracker.opener;
                for(unsigned int i = 0; i < tracker.playerCount; ++i)
                {
                    CardId card = (cur == tracker.mySeat) ? myCard : static_cast<CardId>(std::countr_zero(s.hands[cur]));
                    if(i == 0)
                    {
                        trick.leadSuit = static_cast<Suit>(cardSuit(card, profile.ranksPerSuit));
                        trick.bestCard = card;
                        trick.bestHolder = cur;
                    }
                    else
                    {
                        if(tracker.cardBeats(card, trick.bestCard, *trick.leadSuit))
                        {
                            trick.bestCard = card;
                            trick.bestHolder = cur;
                        }
                    }
                    cur = (cur + 1) % tracker.playerCount;
                }
                totalWeight += s.weight;
                if(trick.bestHolder == tracker.mySeat)
                    winWeight += s.weight;
            }

            // Dividing by the weight actually accumulated also covers the
            // degenerate draw: no samples means no weight, and the R >= 2 path
            // guards the same expression the same way rather than testing
            // samples.empty() separately.
            const float p = (totalWeight > 0.0f) ? (winWeight / totalWeight) : 0.0f;

            // Bid 1 iff p > 6/13 ≈ 0.4615
            const unsigned int bid = (p > (6.0f / 13.0f)) ? 1 : 0;
            return (forbiddenBet && *forbiddenBet == bid) ? (1 - bid) : bid;
        }
        else
        {
            // Base evaluation
            const float e = evalHand(myHand, tracker.mySeat, tracker.initialContext);
            const unsigned int bid = (e > 0.46f) ? 1 : 0;
            return (forbiddenBet && *forbiddenBet == bid) ? (1 - bid) : bid;
        }
    }

    // General rounds (R >= 2, §6.1)
    if(knobs.K_bid == 0)
    {
        const float e = evalHand(myHand, tracker.mySeat, tracker.initialContext);
        int b = std::clamp(static_cast<int>(std::round(e)), 0, static_cast<int>(R));
        if(forbiddenBet && static_cast<unsigned int>(b) == *forbiddenBet)
        {
            b = (b == 0) ? 1 : b - 1;
        }
        return static_cast<unsigned int>(b);
    }

    const auto samples = Sampler::drawSamples(knobs.K_bid, tracker, myHand, rng);
    if(samples.empty())
    {
        const float e = evalHand(myHand, tracker.mySeat, tracker.initialContext);
        int b = std::clamp(static_cast<int>(std::round(e)), 0, static_cast<int>(R));
        if(forbiddenBet && static_cast<unsigned int>(b) == *forbiddenBet)
            b = (b == 0) ? 1 : b - 1;
        return static_cast<unsigned int>(b);
    }

    // Determine who is the last bidder in the round
    const unsigned int lastBidder = (tracker.opener + tracker.playerCount - 1) % tracker.playerCount;

    // Evaluate expected utility for each candidate b in 0..R
    int bestBid = 0;
    float maxU = -100000.0f;

    for(unsigned int b = 0; b <= R; ++b)
    {
        if(forbiddenBet && b == *forbiddenBet)
            continue;

        float sumUtility = 0.0f;
        float totalW = 0.0f;
        float hitWeight = 0.0f;

        for(const auto& s : samples)
        {
            // Determine targets for simulated opponents (§6.1)
            std::array<int, 6> targets{};
            targets[tracker.mySeat] = static_cast<int>(b);

            unsigned int bidsSum = b;
            unsigned int cur = tracker.opener;

            for(unsigned int i = 0; i < tracker.playerCount; ++i)
            {
                if(cur != tracker.mySeat)
                {
                    if(tracker.bids[cur] != UNBID)
                    {
                        targets[cur] = tracker.bids[cur];
                        bidsSum += static_cast<unsigned int>(targets[cur]);
                    }
                    else
                    {
                        const Mask startHand = s.hands[cur];
                        const float eO = evalHand(startHand, cur, tracker.initialContext);
                        const float est = eO + tracker.opponentModels[cur].bias;
                        int tgt = std::clamp(static_cast<int>(std::round(est)), 0, static_cast<int>(R));

                        if(cur == lastBidder)
                        {
                            if(bidsSum + static_cast<unsigned int>(tgt) == R)
                            {
                                const float frac = est - std::floor(est);
                                if(frac >= 0.5f)
                                    tgt = (tgt < static_cast<int>(R)) ? (tgt + 1) : (tgt - 1);
                                else
                                    tgt = (tgt > 0) ? (tgt - 1) : (tgt + 1);
                                tgt = std::clamp(tgt, 0, static_cast<int>(R));
                            }
                        }

                        targets[cur] = tgt;
                        bidsSum += static_cast<unsigned int>(tgt);
                    }
                }
                cur = (cur + 1) % tracker.playerCount;
            }

            // Rollout trick 1 to R with target b
            TrickState emptyTrick{};
            emptyTrick.leadSuit = std::nullopt;
            emptyTrick.bestCard = INVALID_CARD_ID;
            emptyTrick.bestHolder = tracker.opener;

            // Pick lead card for opener
            EvalContext leadCtx{};
            leadCtx.playerCount = tracker.playerCount;
            leadCtx.live = tracker.live;
            for(unsigned int p = 0; p < tracker.playerCount; ++p)
                leadCtx.handSize[p] = R;
            leadCtx.trumpSuit = tracker.trumpSuit;

            CardId firstCard = INVALID_CARD_ID;
            if(tracker.opener == tracker.mySeat)
            {
                firstCard = BasePolicy::chooseCard(myHand, tracker.mySeat, static_cast<int>(b), 0, R, emptyTrick, leadCtx);
            }
            else
            {
                firstCard = BasePolicy::chooseCard(s.hands[tracker.opener], tracker.opener, targets[tracker.opener],
                                                  0, R, emptyTrick, leadCtx);
            }

            // Execute rollout
            const float score = rollout(s, firstCard, myHand, tracker, knobs, targets);
            sumUtility += s.weight * score;
            totalW += s.weight;

            // Check if own score was a hit
            if(score > 0.0f)
            {
                hitWeight += s.weight;
            }
        }

        float u = (totalW > 0.0f) ? (sumUtility / totalW) : -100.0f;

        // Apply GAMMA sharpening if specified
        if(knobs.gamma != 1.0f && totalW > 0.0f)
        {
            const float pHit = hitWeight / totalW;
            const float sharpened = std::pow(std::max(0.0f, pHit), knobs.gamma);
            u = u * sharpened;
        }

        if(u > maxU)
        {
            maxU = u;
            bestBid = static_cast<int>(b);
        }
    }

    // Bid noise (dumbing down for Easy/Medium)
    if(knobs.bidNoise > 0.0f)
    {
        std::uniform_real_distribution<float> uDist(0.0f, 1.0f);
        if(uDist(rng) < knobs.bidNoise)
        {
            std::uniform_int_distribution<int> deltaDist(-1, 1);
            int noisy = bestBid + deltaDist(rng);
            noisy = std::clamp(noisy, 0, static_cast<int>(R));
            if(!forbiddenBet || static_cast<unsigned int>(noisy) != *forbiddenBet)
            {
                bestBid = noisy;
            }
        }
    }

    return static_cast<unsigned int>(bestBid);
}

} // namespace romanian_whist::reckoner
