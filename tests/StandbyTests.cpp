#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/CardValidator.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>
#include <romanian_whist/strategies/common/CardMask.h>
#include <romanian_whist/strategies/common/RoundMemory.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

namespace
{
// When the stand-in takes the seat over.
enum class Moment
{
    Bid,            // a bid in a normal round
    RoundLead,      // the lead of a round's first trick
    MidRoundLead,   // the lead of a later trick, after this seat has played in the round
    Follow,         // a card with others already on the table
    ForeheadBid,
    HiddenBid,
    Any             // any decision of the game
};

const char* nameOf(Moment moment)
{
    switch(moment)
    {
        case Moment::Bid:          return "a bid";
        case Moment::RoundLead:    return "a round's first lead";
        case Moment::MidRoundLead: return "a lead mid-round";
        case Moment::Follow:       return "a follow";
        case Moment::ForeheadBid:  return "a Forehead bid";
        case Moment::HiddenBid:    return "a Hidden bid";
        case Moment::Any:          return "any decision";
    }

    return "?";
}

constexpr Moment kEveryMoment[] = { Moment::Bid, Moment::RoundLead, Moment::MidRoundLead, Moment::Follow,
                                    Moment::ForeheadBid, Moment::HiddenBid, Moment::Any };

// What is wrong with a strategy's memory of the round, measured against the engine at the
// moment `seat` is asked to decide, or an empty string. The test's own version of the check
// the Cyborg makes before it trusts its memory, so it covers the Reckoner too, which has no
// counter to read.
std::string memoryProblem(const common::RoundMemory& memory, const GameEngine& engine, Seat seat,
                          const std::vector<Card>& hand)
{
    const unsigned int n = engine.getPlayerCount();

    if(!memory.roundInitialised)
        return "it never saw this round start";

    if(memory.mySeat != seat.index)
        return "it is watching the wrong seat";

    if(memory.playerCount != n || memory.roundTrickCount != engine.getCurrentRoundTrickCount())
        return "it has the wrong table or round size";

    if(memory.opener != engine.getRoundLeaderSeat().index)
        return "it has the wrong round leader";

    const std::optional<Card> trump = engine.getCurrentTrumpCard();

    if(memory.trumpSuit != (trump ? std::optional<Suit>(trump->suit) : std::nullopt))
        return "it has the wrong trump";

    if(memory.handSize[seat.index] != hand.size())
        return "it has the wrong hand size";

    if((common::cardsToMask(hand, n) & ~memory.live) != 0)
        return "it thinks a card in the hand has been played";

    const std::vector<PlayedCard>& played = engine.getCurrentTrick().getPlayedCards();

    if(memory.currentTrickCards.size() != played.size())
        return "it has the wrong number of cards on the table";

    for(std::size_t i = 0 ; i < played.size() ; i++)
    {
        if(memory.currentTrickCards[i].seat != played[i].seat.index
           || memory.currentTrickCards[i].card != common::cardToId(played[i].card, n))
            return "it has the wrong cards on the table";
    }

    for(unsigned int i = 0 ; i < n ; i++)
    {
        const std::optional<unsigned int> bet = engine.getBet(Seat{i});

        if(memory.bids[i] != (bet ? static_cast<int>(*bet) : common::UNBID))
            return "it has a wrong bid for seat " + std::to_string(i);

        if(memory.won[i] != engine.getTricksWon(Seat{i}))
            return "it has a wrong trick count for seat " + std::to_string(i);
    }

    return {};
}

// Plays one seat with its own provider until the moment comes, then asks the stand-in for every
// later decision. The stand-in has watched the whole game, and its memory is checked at every
// one of the seat's decisions, before the takeover as well as after.
class StandbyProvider : public IMoveProvider
{
public:
    StandbyProvider(const GameEngine& _engine, Seat _seat, std::unique_ptr<IMoveProvider> _own,
                    IStrategy& _standIn, const common::RoundMemory& _memory, Moment _moment,
                    unsigned int _skip)
        : engine(_engine), seat(_seat), own(std::move(_own)), standIn(_standIn), memory(_memory),
          moment(_moment), skip(_skip)
    {}

    unsigned int makeBet(const BetContext& context) override
    {
        requireMemoryAgrees(context.hand);
        takeOverIfDue(true, context.roundType, 0);

        if(!tookOver)
            return own->makeBet(context);

        const unsigned int bet = standIn.getBestBet(context);

        INFO("stand-in bid " << bet);
        REQUIRE(engine.isBetLegal(bet));

        standInDecisions++;
        return bet;
    }

    std::optional<std::size_t> playCard(const PlayContext& context) override
    {
        requireMemoryAgrees(context.hand);
        takeOverIfDue(false, engine.getCurrentRoundType(), context.playedCards.size());

        if(!tookOver)
            return own->playCard(context);

        const std::optional<Card> chosen = standIn.getBestChoice(context);

        REQUIRE(chosen.has_value());

        const std::vector<Card> legal =
            CardValidator().getLegalCards(context.hand, context.trump, context.leadSuit);

        INFO("stand-in played " << chosen->toString());
        REQUIRE(std::find(legal.begin(), legal.end(), *chosen) != legal.end());

        standInDecisions++;

        const auto it = std::find(context.hand.begin(), context.hand.end(), *chosen);
        return static_cast<std::size_t>(std::distance(context.hand.begin(), it));
    }

    bool tookOver = false;
    unsigned int standInDecisions = 0;

private:
    const GameEngine& engine;
    Seat seat;
    std::unique_ptr<IMoveProvider> own;
    IStrategy& standIn;
    const common::RoundMemory& memory;
    Moment moment;
    unsigned int skip;

    void requireMemoryAgrees(const std::vector<Card>& hand) const
    {
        const std::string problem = memoryProblem(memory, engine, seat, hand);

        if(!problem.empty())
            FAIL("the stand-in's memory disagrees with the engine: " << problem);
    }

    bool isDue(bool bidding, RoundType roundType, std::size_t cardsOnTable) const
    {
        switch(moment)
        {
            case Moment::Bid:          return bidding && roundType == RoundType::Normal;
            case Moment::RoundLead:    return !bidding && cardsOnTable == 0 && engine.getCurrentTrickNumber() == 1;
            case Moment::MidRoundLead: return !bidding && cardsOnTable == 0 && engine.getCurrentTrickNumber() > 1;
            case Moment::Follow:       return !bidding && cardsOnTable > 0;
            case Moment::ForeheadBid:  return bidding && roundType == RoundType::Forehead;
            case Moment::HiddenBid:    return bidding && roundType == RoundType::Hidden;
            case Moment::Any:          return true;
        }

        return false;
    }

    void takeOverIfDue(bool bidding, RoundType roundType, std::size_t cardsOnTable)
    {
        if(tookOver || !isDue(bidding, roundType, cardsOnTable))
            return;

        if(skip > 0)
        {
            skip--;
            return;
        }

        tookOver = true;
    }
};

// How many of the moment's chances to let pass before taking over. Each range stays below what
// every table is sure to offer: the opener moves one seat a round, so even at six seats a seat
// opens five rounds. A mid-round lead needs the seat to have won a trick, and the Forehead and
// Hidden rounds come once each, so those take the first chance.
unsigned int skipFor(Moment moment, std::uint32_t seed)
{
    std::mt19937 generator(seed);

    switch(moment)
    {
        case Moment::Bid:       return generator() % 10;
        case Moment::RoundLead: return generator() % 4;
        case Moment::Follow:    return generator() % 10;
        case Moment::Any:       return generator() % 60;
        case Moment::MidRoundLead:
        case Moment::ForeheadBid:
        case Moment::HiddenBid:
            return 0;
    }

    return 0;
}

struct StandbyResult
{
    GameStatus status = GameStatus::NotStarted;
    bool tookOver = false;
    unsigned int standInDecisions = 0;
};

// `seat` is played by a seeded random player while `standIn` watches it, and the stand-in takes
// it over at the chosen moment. The other seats are the round-robin strategies, whose P0..Pn
// names keep clear of the Reckoner's "LowRisk" and "Ducking" name check.
template<typename Strategy>
StandbyResult playWithStandby(Strategy& standIn, const common::RoundMemory& memory,
                              unsigned int playerCount, unsigned int seat, Moment moment,
                              std::uint32_t seed)
{
    GameEngine engine;

    standIn.setSeat(Seat{seat});
    engine.addObserver(&standIn);

    GameSetup setup = buildSetup(GameStructure::S_181, buildRoundRobinProviders(playerCount), seed, true, false);

    auto own = std::make_unique<AiMoveProvider>(std::make_unique<RandomCardStrategy>(seed));
    auto provider = std::make_unique<StandbyProvider>(engine, Seat{seat}, std::move(own), standIn, memory,
                                                      moment, skipFor(moment, seed));
    const StandbyProvider* watch = provider.get();

    setup.seats[seat].moveProvider = std::move(provider);

    engine.start(std::move(setup));
    engine.run();

    return { engine.getStatus(), watch->tookOver, watch->standInDecisions };
}

void requireTakeoverPlayedOut(const StandbyResult& result)
{
    REQUIRE(result.tookOver);
    REQUIRE(result.standInDecisions > 0);
    REQUIRE(result.status == GameStatus::Finished);
}

void runCyborgStandby(unsigned int playerCount, unsigned int seat, Moment moment, std::uint32_t seed)
{
    INFO("Cyborg, players = " << playerCount << ", seat " << seat << ", taking over at " << nameOf(moment)
         << ", seed = " << seed);

    CyborgStrategy standIn;

    requireTakeoverPlayedOut(playWithStandby(standIn, standIn.getMemory(), playerCount, seat, moment, seed));

    // Zero says its memory agreed with every position it was shown after the takeover.
    REQUIRE(standIn.getFallbacksTaken() == 0);
}

void runReckonerStandby(reckoner::ReckonerPreset preset, const char* presetName, unsigned int playerCount,
                        unsigned int seat, Moment moment, std::uint32_t seed)
{
    INFO("Reckoner " << presetName << ", players = " << playerCount << ", seat " << seat << ", taking over at "
         << nameOf(moment) << ", seed = " << seed);

    ReckonerStrategy standIn(preset, seed);

    requireTakeoverPlayedOut(playWithStandby(standIn, standIn.getTracker(), playerCount, seat, moment, seed));
}

// Every bid and card of a game, in order, to compare two games move for move.
struct MoveRecorder : IGameObserver
{
    std::vector<std::string> moves;

    void onBetPlaced(const GameEngine&, Seat seat, unsigned int bet) override
    {
        moves.push_back(std::to_string(seat.index) + " bids " + std::to_string(bet));
    }

    void onCardPlayed(const GameEngine&, Seat seat, const Card& card) override
    {
        moves.push_back(std::to_string(seat.index) + " plays " + card.toString());
    }
};

struct Trace
{
    std::vector<std::string> moves;
    std::vector<int> scores;
};

// The strategy seated by its own factory, which finds its seat from its name.
template<typename MakeSeat>
Trace playSeated(unsigned int playerCount, unsigned int seat, std::uint32_t seed, MakeSeat makeSeat)
{
    MoveRecorder recorder;
    GameEngine engine;
    engine.addObserver(&recorder);

    GameSetup setup = buildSetup(GameStructure::S_181, buildRoundRobinProviders(playerCount), seed, true, false);
    setup.seats[seat] = makeSeat("P" + std::to_string(seat), engine);

    engine.start(std::move(setup));
    engine.run();

    return { recorder.moves, finalScores(engine) };
}

// The same strategy as a stand-in given the seat by setSeat(), taking it over at its first
// decision.
template<typename Strategy>
Trace playStandingIn(Strategy& standIn, const common::RoundMemory& memory, unsigned int playerCount,
                     unsigned int seat, std::uint32_t seed)
{
    MoveRecorder recorder;
    GameEngine engine;
    engine.addObserver(&recorder);

    standIn.setSeat(Seat{seat});
    engine.addObserver(&standIn);

    GameSetup setup = buildSetup(GameStructure::S_181, buildRoundRobinProviders(playerCount), seed, true, false);

    auto own = std::make_unique<AiMoveProvider>(std::make_unique<RandomCardStrategy>(seed));
    setup.seats[seat].moveProvider = std::make_unique<StandbyProvider>(engine, Seat{seat}, std::move(own), standIn,
                                                                       memory, Moment::Any, 0);

    engine.start(std::move(setup));
    engine.run();

    return { recorder.moves, finalScores(engine) };
}
}

// The seat moves from game to game: a Reckoner given no seat plays seat 0, so tests that only
// ever watched seat 0 could not tell whether setSeat() took effect.

TEST_CASE("A Cyborg stand-in takes a seat over at any moment and plays the game out", "[standby][cyborg]")
{
    for(unsigned int playerCount = 2 ; playerCount <= 6 ; playerCount++)
    {
        for(unsigned int i = 0 ; i < std::size(kEveryMoment) ; i++)
        {
            for(std::uint32_t seed = 1 ; seed <= 2 ; seed++)
                runCyborgStandby(playerCount, (i + seed) % playerCount, kEveryMoment[i], seed * 100 + playerCount);
        }
    }
}

TEST_CASE("A Reckoner Alpha stand-in takes a seat over at any moment and plays the game out", "[standby][reckoner]")
{
    for(unsigned int playerCount = 2 ; playerCount <= 6 ; playerCount++)
    {
        for(unsigned int i = 0 ; i < std::size(kEveryMoment) ; i++)
        {
            for(std::uint32_t seed = 1 ; seed <= 2 ; seed++)
                runReckonerStandby(reckoner::ReckonerPreset::Alpha, "Alpha", playerCount, (i + seed) % playerCount,
                                   kEveryMoment[i], seed * 100 + playerCount);
        }
    }
}

TEST_CASE("A Reckoner Beta stand-in takes a seat over at any moment and plays the game out", "[standby][reckoner]")
{
    for(unsigned int i = 0 ; i < std::size(kEveryMoment) ; i++)
    {
        const unsigned int playerCount = 3 + i % 4;
        runReckonerStandby(reckoner::ReckonerPreset::Beta, "Beta", playerCount, (i + 1) % playerCount,
                           kEveryMoment[i], 300 + i);
    }
}

// Two seats: these presets search hardest, and every test also runs in the Debug build.
TEST_CASE("Reckoner Gamma and Delta stand-ins take a seat over at any moment and play the game out",
          "[standby][reckoner]")
{
    for(unsigned int i = 0 ; i < std::size(kEveryMoment) ; i++)
    {
        runReckonerStandby(reckoner::ReckonerPreset::Gamma, "Gamma", 2, i % 2, kEveryMoment[i], 400 + i);
        runReckonerStandby(reckoner::ReckonerPreset::Delta, "Delta", 2, (i + 1) % 2, kEveryMoment[i], 500 + i);
    }
}

TEST_CASE("A Cyborg standing in from the first decision plays the game a seated Cyborg plays", "[standby][cyborg]")
{
    for(unsigned int playerCount = 2 ; playerCount <= 6 ; playerCount++)
    {
        INFO("players = " << playerCount);

        const unsigned int seat = playerCount - 1;

        const Trace seated = playSeated(playerCount, seat, 42, [](const std::string& name, GameEngine& engine)
        {
            return makeCyborgSeat(name, engine);
        });

        CyborgStrategy standIn;
        const Trace standing = playStandingIn(standIn, standIn.getMemory(), playerCount, seat, 42);

        REQUIRE(standing.moves == seated.moves);
        REQUIRE(standing.scores == seated.scores);
    }
}

// Alpha at every table size, and Beta, whose search reads the whole of the memory, at one.
TEST_CASE("A Reckoner standing in from the first decision plays the game a seated Reckoner plays",
          "[standby][reckoner]")
{
    struct Case
    {
        const char* name;
        reckoner::ReckonerKnobs knobs;
        unsigned int playerCount;
    };

    const Case cases[] = { { "Alpha", reckoner::ReckonerKnobs::alpha(), 2 },
                           { "Alpha", reckoner::ReckonerKnobs::alpha(), 3 },
                           { "Alpha", reckoner::ReckonerKnobs::alpha(), 4 },
                           { "Alpha", reckoner::ReckonerKnobs::alpha(), 5 },
                           { "Alpha", reckoner::ReckonerKnobs::alpha(), 6 },
                           { "Beta", reckoner::ReckonerKnobs::beta(), 3 } };

    for(const Case& game : cases)
    {
        INFO(game.name << ", players = " << game.playerCount);

        const unsigned int seat = game.playerCount - 1;
        const std::uint32_t seed = 600 + game.playerCount;

        const Trace seated = playSeated(game.playerCount, seat, 42, [&game, seed](const std::string& name, GameEngine& engine)
        {
            return makeReckonerSeat(name, engine, game.knobs, seed);
        });

        ReckonerStrategy standIn(game.knobs, seed);
        const Trace standing = playStandingIn(standIn, standIn.getTracker(), game.playerCount, seat, 42);

        REQUIRE(standing.moves == seated.moves);
        REQUIRE(standing.scores == seated.scores);
    }
}
