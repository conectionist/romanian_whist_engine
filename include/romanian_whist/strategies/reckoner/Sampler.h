#ifndef ROMANIAN_WHIST_SAMPLER_H
#define ROMANIAN_WHIST_SAMPLER_H

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>

#include <array>
#include <random>
#include <vector>

namespace romanian_whist::reckoner
{

inline constexpr float ESS_MIN = 0.25f;

struct DealSample
{
    std::array<Mask, 6> hands{}; // Opponents' hands (my hand is unchanged)
    float weight = 1.0f;
};

class Sampler
{
public:
    static std::vector<DealSample> drawSamples(unsigned int K,
                                              const ReckonerTracker& tracker,
                                              Mask myHand,
                                              std::mt19937& rng,
                                              float sigmaMultiplier = 1.0f,
                                              float confMultiplier = 1.0f);

private:
    static float computeBidLikelihood(const ReckonerTracker& tracker,
                                      unsigned int opponent,
                                      int bid,
                                      Mask startHand,
                                      float sigmaMultiplier);
};

} // namespace romanian_whist::reckoner

#endif
