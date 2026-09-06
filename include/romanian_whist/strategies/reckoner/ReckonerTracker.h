#ifndef ROMANIAN_WHIST_RECKONER_TRACKER_H
#define ROMANIAN_WHIST_RECKONER_TRACKER_H

#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/Evaluator.h>

#include <array>
#include <optional>
#include <vector>

namespace romanian_whist::reckoner
{

inline constexpr int UNBID = -1;

struct SoftConstraint
{
    unsigned int player = 0;
    Mask excluded = 0;
    float conf = 0.8f;
};

struct TrickCard
{
    unsigned int seat = 0;
    CardId card = INVALID_CARD_ID;
};

struct CompletedTrick
{
    unsigned int leader = 0;
    unsigned int winner = 0;
    std::vector<TrickCard> cards;
};

struct OpponentModel
{
    float bias = 0.0f;
    float sigma = 1.2f;
    unsigned int sampleCount = 0;
    float sumResiduals = 0.0f;
    float sumSqResiduals = 0.0f;
    bool isDeterministic = false; // true if LowRisk or Ducking
};

class ReckonerTracker
{
public:
    unsigned int playerCount = 4;
    unsigned int roundTrickCount = 1; // R
    unsigned int mySeat = 0;
    unsigned int opener = 0; // round leader

    std::optional<CardId> trumpCard = std::nullopt;
    std::optional<Suit> trumpSuit = std::nullopt;

    Mask live = 0;
    std::array<Mask, 6> playedBy{};
    std::array<unsigned int, 6> handSize{};
    std::array<std::uint8_t, 6> voidMask{}; // bit s is (1 << s)
    std::array<int, 6> bids{};              // UNBID or 0..R
    std::array<unsigned int, 6> won{};

    std::vector<SoftConstraint> constraints;
    std::vector<CompletedTrick> completedTricks;
    std::vector<TrickCard> currentTrickCards;
    unsigned int currentTrickLeader = 0;

    // Cross-round opponent models (kept across the game)
    std::array<OpponentModel, 6> opponentModels{};

    // Standings & streak tracking
    std::array<int, 6> totalScores{};
    unsigned int scoreLeaderSeat = 0;
    unsigned int myConsecutiveWins = 0;
    unsigned int myConsecutiveLosses = 0;

    // Configuration
    float rankGapConfScale = 1.0f;

    EvalContext initialContext{};

    ReckonerTracker() = default;

    void initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                   unsigned int openerSeat, std::optional<Card> trump);

    void recordBid(unsigned int seat, unsigned int value);

    void recordCardPlayed(unsigned int seat, const Card& card);

    void recordTrickWon(unsigned int winner);

    void recordRoundEnd(const std::vector<int>& roundScores, const std::vector<int>& runningTotals);

    EvalContext makeCurrentEvalContext() const;

    Mask getUnseenCards(Mask myHand) const;

    unsigned int getDeadCount() const;

    bool cardBeats(CardId candidate, CardId currentBest, Suit leadSuit) const;

private:
    void applyRankGapInferences(unsigned int p, CardId c, CardId bestBefore,
                                unsigned int bestHolderBefore, std::optional<Suit> leadSuit);
};

} // namespace romanian_whist::reckoner

#endif
