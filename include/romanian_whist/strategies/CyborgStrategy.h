#ifndef ROMANIAN_WHIST_CYBORG_STRATEGY_H
#define ROMANIAN_WHIST_CYBORG_STRATEGY_H

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/strategies/IStrategy.h>
#include <romanian_whist/strategies/common/CardMask.h>
#include <romanian_whist/strategies/common/RoundMemory.h>
#include <romanian_whist/strategies/cyborg/CyborgKnobs.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace romanian_whist
{

// A rule-based AI that remembers the whole round and plays to its bid.
//
// See docs/romanian-whist-cyborg-ai.md for the design. Today it keeps the memory
// and delegates both decisions to the shared TrickHeuristics, so it plays at
// LowRiskStrategy's strength; the Cyborg rules for bidding and card play arrive
// on top of this.
//
// LIKE ReckonerStrategy, THIS IS AN IStrategy *AND* AN IGameObserver, and it is
// useless as the first until registered as the second - everything it decides
// from arrives through the observer callbacks, not through the context it is
// handed. Build it with makeCyborgSeat(), which registers it for you. Both
// decisions throw std::logic_error rather than play from an uninitialised
// memory, because a bot quietly reading a table that does not exist is a worse
// failure than a loud one.
//
// Deterministic: no RNG, no presets, one strength. Two Cyborgs given the same
// game play it identically.
class CyborgStrategy : public IStrategy, public IGameObserver
{
private:
    common::RoundMemory memory;
    cyborg::CyborgKnobs knobs;
    std::optional<Seat> mySeat;
    std::string playerName;

    // How many decisions fell through to the heuristic because the memory did
    // not agree with the position it was handed. Zero for a whole game is the
    // healthy answer, and asserting that is what turns "the guard never fires
    // when things are fine" from an assumption into a test.
    std::size_t fallbacksTaken = 0;

public:
    explicit CyborgStrategy(cyborg::CyborgKnobs knobs = {});

    // Normally resolved from the player name inside onGameStarted(). Call this
    // before GameEngine::start() to set it explicitly instead - which is also
    // the way to register a Cyborg that is watching a seat it does not occupy.
    void setSeat(Seat seat);
    void setPlayerName(const std::string& name);

    bool isSeated() const;
    std::size_t getFallbacksTaken() const;

    const common::RoundMemory& getMemory() const;
    common::RoundMemory& getMemory();
    const cyborg::CyborgKnobs& getKnobs() const;
    void setKnobs(const cyborg::CyborgKnobs& newKnobs);

    // IStrategy
    unsigned int getBestBet(const BetContext& context) override;
    std::optional<Card> getBestChoice(const PlayContext& context) override;

    // IGameObserver
    void onGameStarted(const GameEngine& engine) override;
    void onRoundStarted(const GameEngine& engine) override;
    void onBetPlaced(const GameEngine& engine, Seat seat, unsigned int bet) override;
    void onCardPlayed(const GameEngine& engine, Seat seat, const Card& card) override;
    void onTrickWon(const GameEngine& engine, Seat winner, unsigned int trickNumber) override;

    // The same four events, without an engine to hear them from. A caller
    // driving the memory itself is a supported use - it is how the robustness
    // tests prove the guard below keys on the memory rather than on having seen
    // an engine.
    void onRoundStart(unsigned int n, unsigned int r, std::optional<Card> trump,
                      unsigned int openerSeat, unsigned int mySeatIdx);
    void onBid(unsigned int seat, unsigned int value);
    void onCardPlayed(unsigned int seat, const Card& card);
    void onTrickWon(unsigned int winner);

private:
    // Throws std::logic_error unless the memory has seen a round start - i.e.
    // unless this strategy was actually registered as an observer. Called at the
    // top of both decisions. `decision` names the caller so the message says
    // which one refused.
    void requireMemory(const char* decision) const;

    // The single place a Card becomes a bit. Every conversion goes through here
    // so there is one site to point at when a stale player count makes the
    // deck profile disagree with the cards being handed over.
    common::Mask encodeHand(const std::vector<Card>& hand) const;

    // Does the memory still describe the game the engine is showing me? Never
    // throws; a false answer means fall back rather than decide.
    bool agreesWith(common::Mask myHand, std::size_t handSize, std::size_t trickSize) const;

    unsigned int heuristicBid(const BetContext& context) const;
    std::optional<Card> heuristicPlay(const PlayContext& context,
                                      const std::vector<Card>& legal) const;
};

// Builds a Cyborg seat and registers it as an observer of `engine`, which must
// happen before GameEngine::start(). The name is the seat name, and is how the
// strategy finds out which seat it is.
//
// No seed argument, unlike makeReckonerSeat(): there is nothing random here.
SeatSetup makeCyborgSeat(const std::string& name, GameEngine& engine,
                         const cyborg::CyborgKnobs& knobs = {});

} // namespace romanian_whist

#endif
