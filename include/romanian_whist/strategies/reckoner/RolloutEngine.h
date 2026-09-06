#ifndef ROMANIAN_WHIST_ROLLOUT_ENGINE_H
#define ROMANIAN_WHIST_ROLLOUT_ENGINE_H

#include <romanian_whist/strategies/reckoner/BasePolicy.h>
#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>
#include <romanian_whist/strategies/reckoner/Sampler.h>

#include <array>
#include <optional>
#include <random>
#include <vector>

namespace romanian_whist::reckoner
{

struct ReckonerKnobs;

class RolloutEngine
{
public:
    static CardId choosePlay(Mask myHand,
                             const ReckonerTracker& tracker,
                             const ReckonerKnobs& knobs,
                             std::mt19937& rng);

    static unsigned int chooseBid(unsigned int R,
                                  Mask myHand,
                                  std::optional<unsigned int> forbiddenBet,
                                  const ReckonerTracker& tracker,
                                  const ReckonerKnobs& knobs,
                                  std::mt19937& rng);

private:
    static float rollout(const DealSample& sample,
                         CardId myFirstCard,
                         Mask myHand,
                         const ReckonerTracker& tracker,
                         const ReckonerKnobs& knobs,
                         const std::array<int, 6>& targets);

    static float scoreUtility(unsigned int myTricks,
                              const std::array<unsigned int, 6>& won,
                              int myBid,
                              const std::array<int, 6>& bids,
                              const ReckonerTracker& tracker,
                              const ReckonerKnobs& knobs);

    static float endgameSearch(const DealSample& sample,
                               Mask myHand,
                               const ReckonerTracker& tracker,
                               const ReckonerKnobs& knobs,
                               CardId candidate);
};

} // namespace romanian_whist::reckoner

#endif
