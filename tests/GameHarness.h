#ifndef GAME_HARNESS_H
#define GAME_HARNESS_H

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/Scoreboard.h>
#include <romanian_whist/Seat.h>

#include "CardStringMaker.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace romanian_whist::test
{
// Sets a game up and hands it to the engine's own loop. `providers.size()` is
// the player count, one provider per seat in seat order. `observers` are
// registered before the game starts, so they see onGameStarted().
// `endWithForeheadAndHidden` and `all1GamesAreForehead` default to off,
// matching every existing caller.
//
// Returned by pointer, not by value: GameEngine holds an atomic stop flag and
// is therefore neither copyable nor movable. That is the shape every holder of
// a live engine has to take - a backend hosting several games needs
// std::map<GameId, std::unique_ptr<GameEngine>>, never a vector of engines.
std::unique_ptr<GameEngine> playFullGame(GameStructure structure,
                                         std::vector<std::unique_ptr<IMoveProvider>> providers,
                                         std::uint32_t seed,
                                         const std::vector<IGameObserver*>& observers = {},
                                         bool endWithForeheadAndHidden = false,
                                         bool all1GamesAreForehead = false);

// The same setup playFullGame() builds, for a test that wants to start the game
// itself - to register something between construction and start(), to drive
// playRound() by hand, or to assert on start() rather than on a played game.
// Seats are named "P0".."Pn" in provider order.
//
// GameSetup owns the providers and is move-only, so this returns by value and
// every call site reads game.start(buildSetup(...)).
GameSetup buildSetup(GameStructure structure,
                     std::vector<std::unique_ptr<IMoveProvider>> providers,
                     std::uint32_t seed,
                     bool endWithForeheadAndHidden = false,
                     bool all1GamesAreForehead = false);

std::vector<int> finalScores(const GameEngine& engine);   // seat-ordered totals

// Round-robins the three deterministic strategies across seats, so every
// scenario exercises all three regardless of player count.
// RandomCardStrategy is deliberately excluded: its draw count depends on
// what every other seat bids, so any behavioural change desyncs its stream
// and turns a located failure into "something changed somewhere" instead.
//
// Shared by every golden suite - GoldenGameTests pins the scores these produce,
// CyborgMemoryTests pins the RoundMemory state they produce - so the two are
// looking at the same games and a divergence can only be in what is measured.
std::vector<std::unique_ptr<IMoveProvider>> buildRoundRobinProviders(unsigned int playerCount);

// (bid, tricksWon) per seat, per round.
using RoundRecord = std::vector<std::vector<std::pair<unsigned int, unsigned int>>>;

// Records each round as it is scored. The one place a test reads a round's bids
// and results, so a later API change touches this class alone rather than every
// test that wants a round-by-round history.
struct RoundRecorder : IGameObserver
{
    RoundRecord record;

    void onRoundScored(const GameEngine& engine) override;
};

} // namespace romanian_whist::test

#endif
