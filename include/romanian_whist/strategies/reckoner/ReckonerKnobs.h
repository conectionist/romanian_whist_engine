#ifndef ROMANIAN_WHIST_RECKONER_KNOBS_H
#define ROMANIAN_WHIST_RECKONER_KNOBS_H

namespace romanian_whist::reckoner
{

enum class ReckonerPreset
{
    Alpha,
    Beta,
    Gamma,
    Delta
};

struct ReckonerKnobs
{
    unsigned int K_play = 200;
    unsigned int K_bid = 250;
    bool fullMemory = true;
    float bidWeightingSigma = 1.2f;
    float rankGapConfScale = 1.0f;
    float lambdaDefault = 0.25f;
    float lambdaLeader = 0.5f;
    float margin = 0.15f;
    unsigned int endgameN = 0;
    float epsilon = 0.0f;
    float bidNoise = 0.0f;
    float hitSharpening = 0.9f;

    static constexpr ReckonerKnobs alpha()
    {
        ReckonerKnobs k{};
        k.K_play = 0;
        k.K_bid = 0;
        k.fullMemory = false;
        k.bidWeightingSigma = 0.0f; // off
        k.rankGapConfScale = 0.0f;
        k.lambdaDefault = 0.0f;
        k.lambdaLeader = 0.0f;
        k.margin = 0.0f;
        k.endgameN = 0;
        k.epsilon = 0.15f;
        k.bidNoise = 0.20f;
        k.hitSharpening = 1.0f;
        return k;
    }

    static constexpr ReckonerKnobs beta()
    {
        ReckonerKnobs k{};
        k.K_play = 60;
        k.K_bid = 100;
        k.fullMemory = true;
        k.bidWeightingSigma = 1.5f;
        k.rankGapConfScale = 0.5f;
        k.lambdaDefault = 0.15f;
        k.lambdaLeader = 0.15f;
        k.margin = 0.3f;
        k.endgameN = 0;
        k.epsilon = 0.05f;
        k.bidNoise = 0.05f;
        k.hitSharpening = 1.0f;
        return k;
    }

    static constexpr ReckonerKnobs gamma()
    {
        ReckonerKnobs k{};
        k.K_play = 200;
        k.K_bid = 250;
        k.fullMemory = true;
        k.bidWeightingSigma = 1.2f;
        k.rankGapConfScale = 1.0f;
        k.lambdaDefault = 0.25f;
        k.lambdaLeader = 0.50f;
        k.margin = 0.15f;
        k.endgameN = 0;
        k.epsilon = 0.0f;
        k.bidNoise = 0.0f;
        k.hitSharpening = 0.9f;
        return k;
    }

    static constexpr ReckonerKnobs delta()
    {
        ReckonerKnobs k{};
        k.K_play = 400;
        k.K_bid = 500;
        k.fullMemory = true;
        k.bidWeightingSigma = 1.0f;
        k.rankGapConfScale = 1.0f;
        k.lambdaDefault = 0.40f;
        k.lambdaLeader = 0.60f;
        k.margin = 0.10f;
        k.endgameN = 2;
        k.epsilon = 0.0f;
        k.bidNoise = 0.0f;
        k.hitSharpening = 0.85f;
        return k;
    }
};

} // namespace romanian_whist::reckoner

#endif
