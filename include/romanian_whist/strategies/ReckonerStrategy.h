#ifndef ROMANIAN_WHIST_RECKONER_STRATEGY_H
#define ROMANIAN_WHIST_RECKONER_STRATEGY_H

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/strategies/IStrategy.h>
#include <romanian_whist/strategies/reckoner/CardMask.h>
#include <romanian_whist/strategies/reckoner/ReckonerKnobs.h>
#include <romanian_whist/strategies/reckoner/ReckonerTracker.h>

#include <memory>
#include <optional>
#include <random>
#include <string>

namespace romanian_whist
{

class ReckonerStrategy : public IStrategy, public IGameObserver
{
private:
    reckoner::ReckonerTracker tracker;
    reckoner::ReckonerKnobs knobs;
    std::mt19937 rng;
    std::optional<Seat> mySeat;
    std::string playerName;

public:
    explicit ReckonerStrategy(reckoner::ReckonerKnobs _knobs = reckoner::ReckonerKnobs::hard(),
                             std::optional<std::uint32_t> seed = std::nullopt);

    explicit ReckonerStrategy(reckoner::ReckonerPreset preset,
                             std::optional<std::uint32_t> seed = std::nullopt);

    void setSeat(Seat seat);
    void setPlayerName(const std::string& name);
    const reckoner::ReckonerTracker& getTracker() const;
    reckoner::ReckonerTracker& getTracker();
    const reckoner::ReckonerKnobs& getKnobs() const;
    void setKnobs(const reckoner::ReckonerKnobs& _knobs);

    // IStrategy
    unsigned int getBestBet(const BetContext& context) override;
    std::optional<Card> getBestChoice(const PlayContext& context) override;

    // IGameObserver
    void onGameStarted(const GameEngine& engine) override;
    void onRoundStarted(const GameEngine& engine) override;
    void onBetPlaced(const GameEngine& engine, Seat seat, unsigned int bet) override;
    void onCardPlayed(const GameEngine& engine, Seat seat, const Card& card) override;
    void onTrickWon(const GameEngine& engine, Seat winner, unsigned int trickNumber) override;
    void onRoundScored(const GameEngine& engine) override;

    // Direct standalone hooks (§1.6)
    void onRoundStart(unsigned int n, unsigned int r, std::optional<Card> trump,
                      unsigned int openerSeat, unsigned int mySeatIdx);
    void onBid(unsigned int seat, unsigned int value);
    void onCardPlayed(unsigned int seat, const Card& card);
    void onTrickWon(unsigned int winner);
    void onRoundEnd(const std::vector<int>& roundScores, const std::vector<int>& runningTotals);

private:
    // Throws std::logic_error unless the tracker has seen a round start - i.e.
    // unless this strategy was actually registered as an observer. Called at the
    // top of both decisions; see the definition for why it is loud rather than
    // defensive. `decision` names the caller, so the message says which.
    void requireTracker(const char* decision) const;
};

SeatSetup makeReckonerSeat(const std::string& name, GameEngine& engine,
                           const reckoner::ReckonerKnobs& knobs = reckoner::ReckonerKnobs::hard(),
                           std::optional<std::uint32_t> seed = std::nullopt);

} // namespace romanian_whist

#endif
