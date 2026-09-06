#ifndef ROMANIAN_WHIST_BASE_POLICY_H
#define ROMANIAN_WHIST_BASE_POLICY_H

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/Evaluator.h>

#include <array>
#include <optional>
#include <vector>

namespace romanian_whist::reckoner
{

inline constexpr float WASTE_BONUS = 0.4f;

enum class PlayIntent
{
    WantWin,
    WantLose
};

struct TrickState
{
    std::optional<Suit> leadSuit = std::nullopt;
    CardId bestCard = INVALID_CARD_ID;
    unsigned int bestHolder = 0;
    std::vector<unsigned int> playersYetToPlay;
};

class BasePolicy
{
public:
    static Mask legalCards(Mask hand, std::optional<Suit> leadSuit,
                           std::optional<Suit> trumpSuit, const DeckProfile& profile);

    static CardId chooseCard(Mask hand,
                             unsigned int seat,
                             int target,
                             unsigned int won,
                             unsigned int remTricks,
                             const TrickState& trick,
                             const EvalContext& ctx);

    static bool safeFromLaterPlayers(CardId c,
                                     Mask unseen,
                                     const TrickState& trick,
                                     const EvalContext& ctx,
                                     const DeckProfile& profile);

    static float pLed(CardId c,
                      Mask unseen,
                      const EvalContext& ctx,
                      const DeckProfile& profile,
                      bool onlyLaterPlayers = false,
                      const std::vector<unsigned int>& targetPlayers = {});

    static float danger(CardId c, Mask unseen, const DeckProfile& profile);

    static CardId shed(Mask Lo, Mask hand, Mask unseen,
                       bool isFollowingSuitOrUnderTrumping,
                       std::optional<Suit> trumpSuit,
                       const DeckProfile& profile);

    static CardId keep(Mask Lo, Mask hand, Mask unseen,
                       bool isFollowingSuit,
                       std::optional<Suit> trumpSuit,
                       const DeckProfile& profile);

    static PlayIntent balanceDecide(CardId cW, CardId cL,
                                    Mask hand,
                                    unsigned int seat,
                                    int need,
                                    bool isFollowing,
                                    bool cWInSureW,
                                    const TrickState& trick,
                                    const EvalContext& ctx,
                                    const DeckProfile& profile);
};

} // namespace romanian_whist::reckoner

#endif
