#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"
#include "ScriptedMoveProvider.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/Deck.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>

#include <memory>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

namespace
{
std::unique_ptr<IMoveProvider> dummyProvider()
{
    return std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>());
}

// A playable setup of `count` seats, named P0..Pn. Seat count is the only thing
// most of these cases vary, so everything else stays at the harness defaults.
GameSetup setupFor(unsigned int count, GameStructure structure = GameStructure::S_181)
{
    std::vector<std::unique_ptr<IMoveProvider>> providers;

    for(unsigned int i = 0 ; i < count ; i++)
        providers.push_back(dummyProvider());

    return test::buildSetup(structure, std::move(providers), 1u);
}

// A setup with these exact names, for the cases that are about name validation.
// Seat count comes from the list, so it can also express an unplayable table.
GameSetup setupWithNames(std::vector<std::string> names)
{
    GameSetup setup;
    setup.shuffleSeed = 1u;

    for(std::string& name : names)
        setup.seats.push_back(SeatSetup{ std::move(name), dummyProvider() });

    return setup;
}

// The bidding rules used to be testable by hand: place a bet, ask what is
// forbidden, place another. placeBet() is the engine's own business now, so the
// only way in is to watch a real game being played - which is the better test
// anyway, since it checks the rule in the situation it is actually used in.
//
// Every seat is scripted so the game is deterministic and its bids are known.
std::unique_ptr<GameEngine> buildScriptedGame(std::vector<ScriptedMoveProvider*>& seats,
                                              unsigned int playerCount,
                                              GameStructure structure = GameStructure::S_181,
                                              bool endWithForeheadAndHidden = false,
                                              bool all1GamesAreForehead = false)
{
    std::vector<std::unique_ptr<IMoveProvider>> providers;

    for(unsigned int i = 0 ; i < playerCount ; i++)
    {
        auto provider = std::make_unique<ScriptedMoveProvider>();
        seats.push_back(provider.get());
        providers.push_back(std::move(provider));
    }

    auto engine = std::make_unique<GameEngine>();
    engine->start(test::buildSetup(structure, std::move(providers), 1u,
                                   endWithForeheadAndHidden, all1GamesAreForehead));

    return engine;
}
}

TEST_CASE("GameEngine::getForbiddenBet names the bid that would complete the round", "[game-engine]")
{
    std::vector<ScriptedMoveProvider*> seats;
    const auto engine = buildScriptedGame(seats, 3);

    // Recomputed independently, and deliberately not the way getForbiddenBet()
    // does it: that counts how many bets are already engaged, this asks where
    // the seat sits in the bidding order. Two routes to the same answer, so a
    // mistake in either shows up as a disagreement.
    struct Checker : IGameObserver
    {
        unsigned int total = 0;
        unsigned int seen = 0;

        void onRoundStarted(const GameEngine&) override
        {
            total = 0;
            seen = 0;
        }

        void onBetRequested(const GameEngine& engine, Seat seat) override
        {
            const unsigned int trickCount = engine.getCurrentRoundTrickCount();
            const bool isFinalBidder = engine.getBiddingOrder(seat) == engine.getPlayerCount();

            std::optional<unsigned int> expected;

            // The restriction only binds the last bidder, and only while a bid
            // could still bring the total to exactly the trick count.
            if(isFinalBidder && total <= trickCount)
                expected = trickCount - total;

            REQUIRE(engine.getForbiddenBet() == expected);

            // isBetLegal() folds the same rule together with the range check.
            if(expected)
                REQUIRE_FALSE(engine.isBetLegal(*expected));

            REQUIRE_FALSE(engine.isBetLegal(trickCount + 1));

            // Every bidder but the last has a completely free hand.
            if(!isFinalBidder)
            {
                for(unsigned int bet = 0 ; bet <= trickCount ; bet++)
                    REQUIRE(engine.isBetLegal(bet));
            }

            seen++;
        }

        void onBetPlaced(const GameEngine&, Seat, unsigned int bet) override
        {
            total += bet;
        }

        void onBettingComplete(const GameEngine& engine) override
        {
            REQUIRE(seen == engine.getPlayerCount());

            // Everyone has bid, so there is no final bidder left to restrict.
            REQUIRE_FALSE(engine.getForbiddenBet().has_value());
        }
    };

    Checker checker;
    engine->addObserver(&checker);
    engine->run();

    REQUIRE(engine->getStatus() == GameStatus::Finished);
}

TEST_CASE("GameEngine::getForbiddenBet is empty once the bids already exceed the trick count", "[game-engine]")
{
    std::vector<ScriptedMoveProvider*> seats;
    const auto engine = buildScriptedGame(seats, 3);

    // Round 3 of a 3-player S_181 schedule is the first 2-trick round: the
    // three 1-trick rounds come first. Overbid it with the first two seats so
    // no bid the third could make can bring the total back down to 2.
    seats[0]->bids = { 0, 0, 0, 2 };
    seats[1]->bids = { 0, 0, 0, 2 };

    struct Checker : IGameObserver
    {
        bool checkedOverbidRound = false;

        void onBetRequested(const GameEngine& engine, Seat seat) override
        {
            if(engine.getCurrentRoundIndex() != 3)
                return;

            if(engine.getBiddingOrder(seat) != engine.getPlayerCount())
                return;

            REQUIRE(engine.getCurrentRoundTrickCount() == 2);

            // No value at all is barred: the round can no longer add up
            // exactly, so there is nothing left to prevent.
            REQUIRE_FALSE(engine.getForbiddenBet().has_value());

            // Which means every bid in range is legal, including the one that
            // would have been barred had the total still been reachable.
            REQUIRE(engine.isBetLegal(0));
            REQUIRE(engine.isBetLegal(2));

            checkedOverbidRound = true;
        }
    };

    Checker checker;
    engine->addObserver(&checker);
    engine->run();

    REQUIRE(checker.checkedOverbidRound);
}

TEST_CASE("Round-scoped accessors throw before the game is set up", "[game-engine]")
{
    GameEngine engine;

    // Scoreboard::getCurrentRound() is rounds[currentRound] on a vector that is
    // empty until start() lays the schedule out, so every one of these would
    // otherwise read arbitrary memory rather than complain. A client with a
    // setup screen - a Qt window drawing a scoreboard widget before the wizard
    // finishes, a backend serving a game that was created but never started -
    // walks straight into it.
    REQUIRE_THROWS_AS(engine.getCurrentRound(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentRoundIndex(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentRoundTrickCount(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentRoundType(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentTrumpCard(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getRoundLeaderSeat(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getTrickLeaderSeat(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getBiddingOrder(Seat{0}), std::logic_error);
    REQUIRE_THROWS_AS(engine.getForbiddenBet(), std::logic_error);
    REQUIRE_THROWS_AS(engine.isBetLegal(0), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentTrick(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentTrickLeader(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getCurrentTrickNumber(), std::logic_error);
    REQUIRE_THROWS_AS(engine.getBet(Seat{0}), std::logic_error);
    REQUIRE_THROWS_AS(engine.getTricksWon(Seat{0}), std::logic_error);
    REQUIRE_THROWS_AS(engine.canSeeHand(Seat{0}, Seat{0}), std::logic_error);

    // These answer at any time, and isSetUp() is the one that says whether the
    // rest are safe to call yet.
    REQUIRE_FALSE(engine.isSetUp());
    REQUIRE(engine.getPhase() == GamePhase::NotStarted);
    REQUIRE(engine.getStatus() == GameStatus::NotStarted);
    REQUIRE_FALSE(engine.isInProgress());
    REQUIRE(engine.getPlayerCount() == 0);
    REQUIRE_FALSE(engine.getActiveSeat().has_value());

    engine.start(setupFor(3));

    REQUIRE(engine.isSetUp());
    REQUIRE(engine.getPlayerCount() == 3);
    REQUIRE_NOTHROW(engine.getCurrentRound());
    REQUIRE_NOTHROW(engine.getCurrentTrick());
    REQUIRE(engine.getCurrentTrickNumber() == 0u);
}

TEST_CASE("isSetUp(), not the phase, is what says the accessors are safe", "[game-engine]")
{
    // getPhase() used to advertise itself for this, and is wrong in both
    // directions: it is still NotStarted throughout onGameStarted(), where every
    // round-scoped accessor already answers.
    struct Checker : IGameObserver
    {
        bool checked = false;

        void onGameStarted(const GameEngine& engine) override
        {
            checked = true;

            REQUIRE(engine.getPhase() == GamePhase::NotStarted);
            REQUIRE(engine.isSetUp());
            REQUIRE_NOTHROW(engine.getCurrentRoundTrickCount());
            REQUIRE_NOTHROW(engine.getRoundLeaderSeat());
        }
    };

    GameEngine engine;

    Checker checker;
    engine.addObserver(&checker);
    engine.start(setupFor(3));

    REQUIRE(checker.checked);
}

TEST_CASE("GameEngine::getBiddingOrder counts round from the round leader", "[game-engine]")
{
    std::vector<ScriptedMoveProvider*> seats;
    const auto engine = buildScriptedGame(seats, 4);

    struct Checker : IGameObserver
    {
        unsigned int roundsChecked = 0;

        void onRoundStarted(const GameEngine& engine) override
        {
            const unsigned int playerCount = engine.getPlayerCount();

            Seat seat = engine.getRoundLeaderSeat();

            for(unsigned int position = 1 ; position <= playerCount ; position++)
            {
                REQUIRE(engine.getBiddingOrder(seat) == position);
                seat = engine.getNextSeat(seat);
            }

            // Back where it started, having named every seat exactly once.
            REQUIRE(seat == engine.getRoundLeaderSeat());

            roundsChecked++;
        }
    };

    Checker checker;
    engine->addObserver(&checker);
    engine->run();

    // Checked in every round, because the round leader advances one seat per
    // round: a getBiddingOrder() measured from a fixed seat, or from the trick
    // leader, would still pass on round 0 alone.
    REQUIRE(checker.roundsChecked == engine->getRoundCount());
    REQUIRE_THROWS_AS(engine->getBiddingOrder(Seat{4}), std::out_of_range);
}

TEST_CASE("GameEngine::canSeeHand follows the round type", "[game-engine]")
{
    // all1GamesAreForehead and endWithForeheadAndHidden together put every
    // RoundType this game knows about into one schedule: Normal (the 2..7/8/7..2
    // block), Forehead (every 1-trick round) and Hidden (the very last round).
    std::vector<ScriptedMoveProvider*> seats;
    const auto engine = buildScriptedGame(seats, 4, GameStructure::S_181,
                                          /*endWithForeheadAndHidden=*/true,
                                          /*all1GamesAreForehead=*/true);

    struct Checker : IGameObserver
    {
        unsigned int roundsChecked = 0;
        unsigned int normalRounds = 0;
        unsigned int foreheadRounds = 0;
        unsigned int hiddenRounds = 0;

        void onRoundStarted(const GameEngine& engine) override
        {
            const unsigned int playerCount = engine.getPlayerCount();
            const RoundType type = engine.getCurrentRoundType();

            switch(type)
            {
                case RoundType::Forehead: foreheadRounds++; break;
                case RoundType::Hidden:   hiddenRounds++;   break;
                case RoundType::Normal:   normalRounds++;   break;
            }

            for(unsigned int v = 0 ; v < playerCount ; v++)
            {
                for(unsigned int h = 0 ; h < playerCount ; h++)
                {
                    const Seat viewer{v};
                    const Seat holder{h};
                    const bool sameSeat = viewer == holder;

                    switch(type)
                    {
                        case RoundType::Forehead:
                            REQUIRE(engine.canSeeHand(viewer, holder) == !sameSeat);
                            break;
                        case RoundType::Hidden:
                            REQUIRE_FALSE(engine.canSeeHand(viewer, holder));
                            break;
                        case RoundType::Normal:
                            REQUIRE(engine.canSeeHand(viewer, holder) == sameSeat);
                            break;
                    }
                }
            }

            roundsChecked++;
        }
    };

    Checker checker;
    engine->addObserver(&checker);
    engine->run();

    REQUIRE(checker.roundsChecked == engine->getRoundCount());

    // Every assertion above sits inside a switch on the round type, so a
    // schedule that stopped producing one of the three would silently drop that
    // branch and leave the test green on the other two - forcing every round to
    // Normal passes it. These three pin the coverage the comment claims.
    REQUIRE(checker.normalRounds > 0);
    REQUIRE(checker.foreheadRounds > 0);
    REQUIRE(checker.hiddenRounds > 0);

    REQUIRE_THROWS_AS(engine->canSeeHand(Seat{4}, Seat{0}), std::out_of_range);
    REQUIRE_THROWS_AS(engine->canSeeHand(Seat{0}, Seat{4}), std::out_of_range);
}

TEST_CASE("GameEngine::start rejects impossible seat counts", "[game-engine]")
{
    // The 2..6 check used to live in initializeDeck() and key off an argument.
    // It is a property of the setup, so it belongs with the rest of the setup
    // validation - and now reads the seat count it is actually about.
    SECTION("1 seat throws")
    {
        GameEngine engine;
        REQUIRE_THROWS_AS(engine.start(setupFor(1)), std::invalid_argument);
    }

    SECTION("7 seats throws")
    {
        GameEngine engine;
        REQUIRE_THROWS_AS(engine.start(setupFor(7)), std::invalid_argument);
    }

    SECTION("0 seats throws")
    {
        GameEngine engine;
        REQUIRE_THROWS_AS(engine.start(GameSetup{}), std::invalid_argument);
    }

    SECTION("2 through 6 seats do not throw")
    {
        for(unsigned int n = 2 ; n <= 6 ; n++)
        {
            GameEngine engine;
            REQUIRE_NOTHROW(engine.start(setupFor(n)));
            REQUIRE(engine.getPlayerCount() == n);
        }
    }
}

TEST_CASE("GameEngine::start rejects unusable seat names", "[game-engine]")
{
    SECTION("a duplicate name throws")
    {
        // Bets and tricks are keyed by Seat, so a shared name no longer
        // corrupts scoring - it is rejected because two rows a player cannot
        // tell apart is a bad game, not because it is still a wrong one.
        GameEngine engine;
        REQUIRE_THROWS_AS(engine.start(setupWithNames({ "A", "B", "A" })),
                          std::invalid_argument);
    }

    SECTION("an empty name throws")
    {
        GameEngine engine;
        REQUIRE_THROWS_AS(engine.start(setupWithNames({ "A", "", "C" })),
                          std::invalid_argument);
    }

    SECTION("names are compared byte for byte, so case tells them apart")
    {
        // Deliberately no case folding: these are Romanian names and will carry
        // S-comma, T-comma, A-breve. A byte-wise tolower over UTF-8 either does
        // nothing to a multibyte sequence or corrupts it, and correct Unicode
        // folding is not a dependency the engine should grow. A client that
        // wants to be stricter is free to be - SetupWizard already is.
        GameEngine engine;
        REQUIRE_NOTHROW(engine.start(setupWithNames({ "John", "john" })));
    }

    SECTION("nothing is trimmed - validate, do not mutate")
    {
        GameEngine engine;
        REQUIRE_NOTHROW(engine.start(setupWithNames({ "John ", "John" })));
        REQUIRE(engine.getPlayers().at(0).getName() == "John ");
    }
}

TEST_CASE("GameEngine::start rejects a seat with no move provider", "[game-engine]")
{
    GameSetup setup;
    setup.seats.push_back(SeatSetup{ "A", dummyProvider() });
    setup.seats.push_back(SeatSetup{ "B", nullptr });

    GameEngine engine;
    REQUIRE_THROWS_AS(engine.start(std::move(setup)), std::invalid_argument);
}

TEST_CASE("GameEngine::start rejects a second start", "[game-engine]")
{
    // One error naming start(), where there used to be four - addPlayer() after
    // either initializer, initializeDeck() twice and initializeScoreboard()
    // twice - each complaining about whichever ran first rather than about the
    // thing the client actually did.
    GameEngine engine;
    engine.start(setupFor(3));

    const unsigned int roundCount = engine.getRoundCount();
    REQUIRE(roundCount == 21);

    REQUIRE_THROWS_AS(engine.start(setupFor(4)), std::logic_error);

    // And left nothing behind: the schedule is not doubled, the deck is not
    // rebuilt, and the table is still the one onGameStarted() announced.
    REQUIRE(engine.getRoundCount() == roundCount);
    REQUIRE(engine.getPlayerCount() == 3);
    REQUIRE(engine.getDeck().size() == 24);
}

TEST_CASE("A rejected start leaves the engine startable", "[game-engine]")
{
    // start() validates the whole setup before applying any of it, so a client
    // that catches the error can fix the setup and try again. Nothing is half
    // applied in between: no seats, no schedule, no deck, and no callback.
    struct StartWatcher : IGameObserver
    {
        unsigned int gameStarted = 0;

        void onGameStarted(const GameEngine&) override { gameStarted++; }
    };

    GameEngine engine;
    StartWatcher watcher;
    engine.addObserver(&watcher);

    REQUIRE_THROWS_AS(engine.start(setupWithNames({ "A", "A" })), std::invalid_argument);

    REQUIRE(watcher.gameStarted == 0);
    REQUIRE(engine.getStatus() == GameStatus::NotStarted);
    REQUIRE_FALSE(engine.isSetUp());
    REQUIRE(engine.getPlayerCount() == 0);
    REQUIRE(engine.getDeck().size() == 0);

    REQUIRE_NOTHROW(engine.start(setupWithNames({ "A", "B" })));
    REQUIRE(watcher.gameStarted == 1);
    REQUIRE(engine.getPlayerCount() == 2);
}

TEST_CASE("GameEngine::getStandings carries the seat and sorts stably", "[game-engine]")
{
    GameEngine engine;
    engine.start(setupWithNames({ "Ana", "Bogdan", "Cristi", "Dan" }));

    SECTION("before a point is scored every seat is level, in seat order")
    {
        // The tie case is the whole reason this is a stable_sort. With
        // std::sort four equal scores could come back in any order, and the
        // same game could render its scoreboard differently between two runs -
        // a difference no test could reproduce and no reader could explain.
        const std::vector<Standing> standings = engine.getStandings();

        REQUIRE(standings.size() == 4);

        for(unsigned int i = 0 ; i < 4 ; i++)
        {
            REQUIRE(standings[i].seat == Seat{i});
            REQUIRE(standings[i].score == 0);
        }

        REQUIRE(standings[0].name == "Ana");
        REQUIRE(standings[3].name == "Dan");
    }

    SECTION("after a game it is sorted best first, and the seat still identifies the row")
    {
        engine.run();

        const std::vector<Standing> standings = engine.getStandings();
        REQUIRE(standings.size() == 4);

        for(std::size_t i = 1 ; i < standings.size() ; i++)
            REQUIRE(standings[i - 1].score >= standings[i].score);

        // Every seat appears exactly once, and each row's score is that seat's
        // - so a client can key a highlight off the seat rather than the name.
        std::vector<unsigned int> seatsSeen;
        for(const Standing& standing : standings)
        {
            REQUIRE(standing.score == engine.getTotalScore(standing.seat));
            REQUIRE(standing.name == engine.getPlayers().at(standing.seat.index).getName());
            seatsSeen.push_back(standing.seat.index);
        }

        std::sort(seatsSeen.begin(), seatsSeen.end());
        REQUIRE(seatsSeen == std::vector<unsigned int>{ 0, 1, 2, 3 });
    }
}

TEST_CASE("getStandings places ties by competition ranking", "[game-engine]")
{
    // A ScriptedMoveProvider with an empty script bids the lowest legal bid and
    // plays the first legal card, which is what makes the tie here designed
    // rather than found: every seat bids 0 in the opening one-trick rounds, so
    // exactly one seat takes the trick and scores -1 while the other three bid
    // exactly and score 5. The deal decides WHICH seat is the odd one out - the
    // seed is arbitrary - but never how many there are, so the shape of the
    // standings below holds for every seed.
    std::vector<std::unique_ptr<IMoveProvider>> providers;

    for(unsigned int i = 0 ; i < 4 ; i++)
        providers.push_back(std::make_unique<ScriptedMoveProvider>());

    GameEngine engine;
    engine.start(buildSetup(GameStructure::S_181, std::move(providers), 7u));

    SECTION("a level table is one four-way tie for first")
    {
        for(const Standing& standing : engine.getStandings())
            REQUIRE(standing.place == 1);
    }

    SECTION("a three-way tie for first leaves nobody second or third")
    {
        engine.playRound();

        const std::vector<Standing> standings = engine.getStandings();
        REQUIRE(standings.size() == 4);

        // 5, 5, 5, -1 ranks 1, 1, 1, 4. The dense ranking that a running
        // place++ per row produces would say 1, 2, 3, 4 and put two seats on
        // the same score in different places; the off-by-one version of this
        // loop, numbering from the tie rather than from the index, would say
        // 1, 1, 1, 2 and hand the last seat a second place nobody came second
        // in.
        REQUIRE(standings[0].place == 1);
        REQUIRE(standings[1].place == 1);
        REQUIRE(standings[2].place == 1);
        REQUIRE(standings[3].place == 4);

        REQUIRE(standings[0].score == 5);
        REQUIRE(standings[1].score == 5);
        REQUIRE(standings[2].score == 5);
        REQUIRE(standings[3].score == -1);

        // The three that tied stay in seat order, which is what the stable sort
        // is for - a client rendering them has to get the same order twice.
        REQUIRE(standings[0].seat.index < standings[1].seat.index);
        REQUIRE(standings[1].seat.index < standings[2].seat.index);
    }
}

TEST_CASE("getWinners returns every seat on the top score", "[game-engine]")
{
    std::vector<std::unique_ptr<IMoveProvider>> providers;

    for(unsigned int i = 0 ; i < 4 ; i++)
        providers.push_back(std::make_unique<ScriptedMoveProvider>());

    GameEngine engine;
    engine.start(buildSetup(GameStructure::S_181, std::move(providers), 7u));

    SECTION("a drawn game names every winner, not the first of them")
    {
        // Same three-way tie as above. Reading getStandings().front() here -
        // the way this gets written by hand - would name one winner of a game
        // three seats drew, which is the whole reason getWinners() exists.
        engine.playRound();

        const std::vector<Standing> winners = engine.getWinners();

        REQUIRE(winners.size() == 3);

        for(const Standing& winner : winners)
        {
            REQUIRE(winner.place == 1);
            REQUIRE(winner.score == 5);
        }

        REQUIRE(winners[0].seat.index < winners[1].seat.index);
        REQUIRE(winners[1].seat.index < winners[2].seat.index);
    }

    SECTION("it agrees with the standings it is drawn from, all game long")
    {
        // Checked every round rather than at the end alone: getWinners() reads
        // during a game too, where it means "currently leading", and the two
        // views drifting apart mid-game is exactly what a live scoreboard would
        // show.
        while(engine.getStatus() == GameStatus::InProgress)
        {
            engine.playRound();

            const std::vector<Standing> standings = engine.getStandings();
            const std::vector<Standing> winners = engine.getWinners();

            REQUIRE_FALSE(winners.empty());
            REQUIRE(winners.size() <= standings.size());

            const int topScore = standings.front().score;

            for(const Standing& winner : winners)
                REQUIRE(winner.score == topScore);

            // Nobody outside the returned set shares the top score, which is
            // the half a "every winner has the top score" check alone misses.
            for(std::size_t i = winners.size() ; i < standings.size() ; i++)
                REQUIRE(standings[i].score < topScore);
        }

        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }
}

TEST_CASE("places stay consistent with the scores they rank", "[game-engine]")
{
    // The properties the ranking has to satisfy whatever the scores turn out to
    // be, over games nobody designed: equal scores share a place, a lower score
    // takes a strictly later one, and a place is either its row's index or the
    // place of the tie it joined. The designed cases above pin the two shapes
    // that matter; this is what would catch a ranking that happens to be right
    // for those and wrong elsewhere.
    //
    // Ties are common in the early one-trick rounds but not guaranteed at the
    // end of any particular game, so the loop counts them and asserts it
    // actually saw some - a property test that only ever ranked distinct scores
    // would pass without exercising a single line of the tie handling.
    unsigned int tiesSeen = 0;

    for(std::uint32_t seed = 1 ; seed <= 25 ; seed++)
    {
        INFO("seed " << seed);

        std::vector<std::unique_ptr<IMoveProvider>> providers;

        for(unsigned int i = 0 ; i < 4 ; i++)
            providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>()));

        GameEngine engine;
        engine.start(buildSetup(GameStructure::S_181, std::move(providers), seed));

        while(engine.getStatus() == GameStatus::InProgress)
        {
            engine.playRound();

            const std::vector<Standing> standings = engine.getStandings();

            REQUIRE(standings.front().place == 1);

            for(std::size_t i = 1 ; i < standings.size() ; i++)
            {
                if(standings[i].score == standings[i - 1].score)
                {
                    REQUIRE(standings[i].place == standings[i - 1].place);
                    tiesSeen++;
                }
                else
                {
                    REQUIRE(standings[i].score < standings[i - 1].score);
                    REQUIRE(standings[i].place == i + 1);
                }
            }
        }
    }

    REQUIRE(tiesSeen > 0);
}

TEST_CASE("getRoundScore is what the round is worth, getTotalScore what is committed", "[game-engine]")
{
    // The split Player::addToScore()/resetCurrentRoundScore() makes: scoring a
    // round writes the round score, and committing folds it into the total and
    // clears it. A client rendering only one of the two shows a different number
    // either side of the commit for the same round.
    struct Checker : IGameObserver
    {
        unsigned int roundsChecked = 0;
        std::vector<int> totalsAtScored;

        void onRoundScored(const GameEngine& engine) override
        {
            totalsAtScored.clear();

            for(unsigned int i = 0 ; i < engine.getPlayerCount() ; i++)
                totalsAtScored.push_back(engine.getTotalScore(Seat{i}) +
                                         engine.getRoundScore(Seat{i}));
        }

        void onRoundComplete(const GameEngine& engine) override
        {
            // Committed: the round score has been folded in and cleared, so the
            // total alone now equals the projection made a moment ago.
            for(unsigned int i = 0 ; i < engine.getPlayerCount() ; i++)
            {
                REQUIRE(engine.getRoundScore(Seat{i}) == 0);
                REQUIRE(engine.getTotalScore(Seat{i}) == totalsAtScored[i]);
            }

            roundsChecked++;
        }
    };

    GameEngine engine;
    Checker checker;
    engine.addObserver(&checker);
    engine.start(setupFor(3));
    engine.run();

    REQUIRE(checker.roundsChecked == engine.getRoundCount());
}

TEST_CASE("GameEngine deck composition", "[game-engine]")
{
    struct Expectation { unsigned int playerCount; std::size_t size; Rank lowest; };

    const std::vector<Expectation> expectations{
        { 2, 16, Rank::Jack },
        { 3, 24, Rank::Nine },
        { 4, 32, Rank::Seven },
        { 5, 40, Rank::Five },
        { 6, 48, Rank::Three },
    };

    for(const auto& expectation : expectations)
    {
        GameEngine engine;
        engine.start(setupFor(expectation.playerCount));

        const Deck& deck = engine.getDeck();
        REQUIRE(deck.size() == expectation.size);

        Rank lowest = Rank::Ace;
        bool sawTwo = false;
        for(const Card& card : deck)
        {
            if(static_cast<int>(card.rank) < static_cast<int>(lowest))
                lowest = card.rank;
            if(card.rank == Rank::Two)
                sawTwo = true;
        }

        REQUIRE(lowest == expectation.lowest);
        REQUIRE_FALSE(sawTwo);
    }
}
