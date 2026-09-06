#include <romanian_whist/strategies/reckoner/Sampler.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace romanian_whist::reckoner
{

namespace
{

unsigned int simulateLowRiskBid(Mask startHand, std::optional<Suit> trumpSuit,
                                unsigned int playerCount, const DeckProfile& profile)
{
    std::vector<Card> cards = maskToCards(startHand, playerCount);
    std::optional<Card> trumpPlaceholder = std::nullopt;
    if(trumpSuit)
    {
        // LowRisk only inspects trump.suit, so any rank in trumpSuit works
        trumpPlaceholder = Card{Rank::Ace, *trumpSuit};
    }

    unsigned int winners = 0;
    unsigned int trumpLength = 0;

    for(const Card& c : cards)
    {
        if(trumpPlaceholder && c.suit == trumpPlaceholder->suit)
        {
            trumpLength++;
            if(c.rank == Rank::King || c.rank == Rank::Ace)
                winners++;
        }
        else if(c.rank == Rank::Ace)
        {
            winners++;
        }
    }

    if(trumpPlaceholder && trumpLength > 2)
    {
        winners += (trumpLength - 2);
    }

    return std::min<unsigned int>(winners, static_cast<unsigned int>(cards.size()));
}

} // namespace

float Sampler::computeBidLikelihood(const ReckonerTracker& tracker,
                                    unsigned int opponent,
                                    int bid,
                                    Mask startHand,
                                    float sigmaMultiplier)
{
    const auto& model = tracker.opponentModels[opponent];
    if(model.isDeterministic)
    {
        const auto& profile = getDeckProfile(tracker.playerCount);
        const unsigned int simulated =
            simulateLowRiskBid(startHand, tracker.trumpSuit, tracker.playerCount, profile);
        return (static_cast<int>(simulated) == bid) ? 1.0f : 0.02f;
    }

    const float eO = evalHand(startHand, opponent, tracker.initialContext);
    const float diff = static_cast<float>(bid) - eO - model.bias;
    const float sig = std::max(0.2f, model.sigma * sigmaMultiplier);
    const float lik = std::exp(-(diff * diff) / (2.0f * sig * sig));
    return std::max(1e-6f, lik);
}

std::vector<DealSample> Sampler::drawSamples(unsigned int K,
                                             const ReckonerTracker& tracker,
                                             Mask myHand,
                                             std::mt19937& rng,
                                             float sigmaMultiplier,
                                             float confMultiplier)
{
    if(K == 0)
        return {};

    const auto& profile = getDeckProfile(tracker.playerCount);
    const Mask U_mask = tracker.getUnseenCards(myHand);

    // Collect list of cards in U
    std::vector<CardId> U;
    Mask m = U_mask;
    while(m != 0)
    {
        const CardId c = static_cast<CardId>(std::countr_zero(m));
        U.push_back(c);
        m &= m - 1ULL;
    }

    // Hand capacities and dead count
    std::array<unsigned int, 6> initialCap{};
    unsigned int totalCap = 0;
    for(unsigned int o = 0; o < tracker.playerCount; ++o)
    {
        if(o != tracker.mySeat)
        {
            initialCap[o] = tracker.handSize[o];
            totalCap += initialCap[o];
        }
    }
    const unsigned int initialDead = (U.size() >= totalCap) ? static_cast<unsigned int>(U.size() - totalCap) : 0;

    // Order U: most-constrained suits first (suits that more opponents are void in)
    std::array<unsigned int, 4> voidCount{};
    for(unsigned int o = 0; o < tracker.playerCount; ++o)
    {
        if(o != tracker.mySeat)
        {
            for(unsigned int s = 0; s < 4; ++s)
            {
                if(tracker.voidMask[o] & (1 << s))
                    voidCount[s]++;
            }
        }
    }

    std::array<std::vector<CardId>, 4> suitCards;
    for(CardId c : U)
    {
        const unsigned int s = cardSuit(c, profile.ranksPerSuit);
        suitCards[s].push_back(c);
    }

    std::array<unsigned int, 4> suitOrder = {0, 1, 2, 3};
    std::sort(suitOrder.begin(), suitOrder.end(), [&](unsigned int a, unsigned int b) {
        return voidCount[a] > voidCount[b];
    });

    std::vector<CardId> orderedU;
    orderedU.reserve(U.size());
    for(unsigned int s : suitOrder)
    {
        std::shuffle(suitCards[s].begin(), suitCards[s].end(), rng);
        orderedU.insert(orderedU.end(), suitCards[s].begin(), suitCards[s].end());
    }

    std::vector<DealSample> samples;
    samples.reserve(K);

    for(unsigned int k = 0; k < K; ++k)
    {
        DealSample sample{};
        sample.weight = 1.0f;

        bool success = false;
        unsigned int restarts = 0;

        while(!success && restarts < 50)
        {
            for(unsigned int o = 0; o < tracker.playerCount; ++o)
                sample.hands[o] = 0ULL;

            auto cap = initialCap;
            unsigned int deadLeft = initialDead;
            bool failed = false;

            for(CardId c : orderedU)
            {
                const unsigned int s = cardSuit(c, profile.ranksPerSuit);

                // Build eligible receivers
                std::vector<unsigned int> elig;
                std::vector<unsigned int> weights;
                unsigned int totalWeight = 0;

                for(unsigned int o = 0; o < tracker.playerCount; ++o)
                {
                    if(o != tracker.mySeat && cap[o] > 0 && !(tracker.voidMask[o] & (1 << s)))
                    {
                        elig.push_back(o);
                        weights.push_back(cap[o]);
                        totalWeight += cap[o];
                    }
                }
                if(deadLeft > 0)
                {
                    elig.push_back(tracker.playerCount); // dead pile sentinel
                    weights.push_back(deadLeft);
                    totalWeight += deadLeft;
                }

                if(totalWeight == 0)
                {
                    failed = true;
                    break;
                }

                // Pick receiver proportional to remaining capacity
                std::uniform_int_distribution<unsigned int> dist(0, totalWeight - 1);
                const unsigned int pick = dist(rng);
                unsigned int cum = 0;
                unsigned int chosen = elig.back();
                for(std::size_t i = 0; i < elig.size(); ++i)
                {
                    cum += weights[i];
                    if(pick < cum)
                    {
                        chosen = elig[i];
                        break;
                    }
                }

                if(chosen == tracker.playerCount)
                {
                    deadLeft--;
                }
                else
                {
                    sample.hands[chosen] |= cardBit(c);
                    cap[chosen]--;
                }
            }

            if(!failed)
            {
                success = true;
            }
            else
            {
                restarts++;
            }
        }

        if(!success)
        {
            // Fallback: ignore void constraints for this sample
            for(unsigned int o = 0; o < tracker.playerCount; ++o)
                sample.hands[o] = 0ULL;

            auto cap = initialCap;
            unsigned int deadLeft = initialDead;

            for(CardId c : orderedU)
            {
                std::vector<unsigned int> elig;
                std::vector<unsigned int> weights;
                unsigned int totalWeight = 0;

                for(unsigned int o = 0; o < tracker.playerCount; ++o)
                {
                    if(o != tracker.mySeat && cap[o] > 0)
                    {
                        elig.push_back(o);
                        weights.push_back(cap[o]);
                        totalWeight += cap[o];
                    }
                }
                if(deadLeft > 0)
                {
                    elig.push_back(tracker.playerCount);
                    weights.push_back(deadLeft);
                    totalWeight += deadLeft;
                }

                if(totalWeight == 0)
                    break;

                std::uniform_int_distribution<unsigned int> dist(0, totalWeight - 1);
                const unsigned int pick = dist(rng);
                unsigned int cum = 0;
                unsigned int chosen = elig.back();
                for(std::size_t i = 0; i < elig.size(); ++i)
                {
                    cum += weights[i];
                    if(pick < cum)
                    {
                        chosen = elig[i];
                        break;
                    }
                }

                if(chosen == tracker.playerCount)
                {
                    deadLeft--;
                }
                else
                {
                    sample.hands[chosen] |= cardBit(c);
                    cap[chosen]--;
                }
            }
        }

        // Compute sample weight
        float w = 1.0f;
        for(unsigned int o = 0; o < tracker.playerCount; ++o)
        {
            if(o != tracker.mySeat)
            {
                const Mask startHand = sample.hands[o] | tracker.playedBy[o];
                if(tracker.bids[o] != UNBID)
                {
                    w *= computeBidLikelihood(tracker, o, tracker.bids[o], startHand, sigmaMultiplier);
                }

                for(const auto& constraint : tracker.constraints)
                {
                    if(constraint.player == o)
                    {
                        if((sample.hands[o] & constraint.excluded) != 0)
                        {
                            const float scaledConf = std::min(0.99f, constraint.conf * confMultiplier);
                            w *= (1.0f - scaledConf);
                        }
                    }
                }
            }
        }

        sample.weight = std::max(1e-9f, w);
        samples.push_back(sample);
    }

    // Normalize weights and compute ESS
    float sumW = 0.0f;
    for(const auto& s : samples)
        sumW += s.weight;

    float sumW2 = 0.0f;
    if(sumW > 0.0f)
    {
        for(auto& s : samples)
        {
            s.weight /= sumW;
            sumW2 += s.weight * s.weight;
        }
    }
    else
    {
        const float uniform = 1.0f / static_cast<float>(K);
        for(auto& s : samples)
        {
            s.weight = uniform;
            sumW2 += uniform * uniform;
        }
    }

    const float ess = (sumW2 > 0.0f) ? (1.0f / sumW2) : 0.0f;

    if(ess < ESS_MIN * static_cast<float>(K) && sigmaMultiplier == 1.0f)
    {
        // Relax (sigma x2, conf x0.5) and redraw once
        return drawSamples(K, tracker, myHand, rng, /*sigmaMultiplier=*/2.0f, /*confMultiplier=*/0.5f);
    }
    else if(ess < ESS_MIN * static_cast<float>(K))
    {
        // Still low after relaxation -> use uniform weights
        const float uniform = 1.0f / static_cast<float>(K);
        for(auto& s : samples)
        {
            s.weight = uniform;
        }
    }

    return samples;
}

} // namespace romanian_whist::reckoner
