#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"
#include "GameHarness.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/CardValidator.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

// CyborgStrategy decides from state that arrives somewhere other than the
// argument it was called with. These are the cases about what happens when that
// state is absent, stale, or describes a different table - not about how well it
// plays, which CyborgStrategyTests covers.
//
// Ported from ReckonerRobustnessTests, which faces the same hazards for the same
// reason. Cyborg diverges in one place, and case 4 is it.

TEST_CASE("CyborgStrategy: an unregistered strategy refuses to decide", "[cyborg][robustness]")
{
    // THE MISCONFIGURATION THIS CLASS INVITES. Every other IStrategy is complete
    // once constructed; this one is inert until an engine has been told to feed
    // it. Constructing one and handing it to AiMoveProvider compiles and looks
    // right.
    CyborgStrategy strategy;

    // Jack..Ace, because the deck a Cyborg encodes through depends on the seat
    // count, and the hand has to exist at the table it is about to be told it is
    // sitting at.
    const std::vector<Card> hand = { Card{Rank::Ace, Suit::Hearts},
                                     Card{Rank::King, Suit::Spades} };
    const std::vector<Card> played;

    const PlayContext play{ hand, played, std::nullopt, std::nullopt, 0, 0 };
    const BetContext bet{ hand, std::nullopt, true, std::nullopt, RoundType::Normal };

    REQUIRE_THROWS_AS(strategy.getBestChoice(play), std::logic_error);
    REQUIRE_THROWS_AS(strategy.getBestBet(bet), std::logic_error);

    // AND IT STOPS REFUSING AS SOON AS IT IS FED. Driving the memory directly is
    // a supported use, so the guard has to key on the memory being initialised
    // rather than on an engine having been seen.
    strategy.onRoundStart(2, 1, std::nullopt, 0, 0);

    REQUIRE_NOTHROW(strategy.getBestChoice(play));
    REQUIRE_NOTHROW(strategy.getBestBet(bet));
}

TEST_CASE("CyborgStrategy: a stale memory cannot produce an illegal card", "[cyborg][robustness]")
{
    CyborgStrategy strategy;

    // A fresh round and nothing else: the memory believes it is on lead.
    strategy.onRoundStart(4, 4, std::nullopt, 0, 0);

    // Seven..Ace, the four-handed deck.
    const std::vector<Card> hand = { Card{Rank::Seven, Suit::Hearts},
                                     Card{Rank::Ace, Suit::Spades},
                                     Card{Rank::King, Suit::Clubs} };

    // Hearts led by somebody the memory never heard about.
    const std::vector<Card> played = { Card{Rank::Ten, Suit::Hearts} };

    const PlayContext context{ hand, played, std::nullopt, Suit::Hearts, 1, 0 };

    const std::optional<Card> chosen = strategy.getBestChoice(context);

    REQUIRE(chosen.has_value());

    // Holding a heart, it must follow suit - whatever the memory believed.
    REQUIRE(chosen->suit == Suit::Hearts);

    // And the same answer stated the way the engine states it, so this case
    // cannot pass by agreeing with a rule nobody else applies.
    const CardValidator validator;
    const std::vector<Card> legal = validator.getLegalCards(hand, std::nullopt, Suit::Hearts);

    REQUIRE(std::find(legal.begin(), legal.end(), *chosen) != legal.end());

    // The disagreement was noticed rather than papered over.
    REQUIRE(strategy.getFallbacksTaken() == 1);
}

TEST_CASE("CyborgStrategy: a hand its deck cannot encode still gets a legal answer",
          "[cyborg][robustness]")
{
    CyborgStrategy strategy;

    // Told it is at a two-handed table, whose deck is Jack..Ace...
    strategy.onRoundStart(2, 3, std::nullopt, 0, 0);

    // ...and then handed a six-handed hand, whose low cards that deck cannot
    // even name. cardToId() throws rather than returning a wrong id, which is
    // the half of this that comparing the ANSWER could never catch.
    const std::vector<Card> hand = { Card{Rank::Three, Suit::Hearts},
                                     Card{Rank::Four, Suit::Hearts},
                                     Card{Rank::Five, Suit::Clubs} };
    const std::vector<Card> played = { Card{Rank::Six, Suit::Hearts} };

    const PlayContext context{ hand, played, std::nullopt, Suit::Hearts, 1, 0 };

    std::optional<Card> chosen;
    REQUIRE_NOTHROW(chosen = strategy.getBestChoice(context));

    REQUIRE(chosen.has_value());
    REQUIRE(chosen->suit == Suit::Hearts);

    // And the bid seam too, which encodes the same hand the same way.
    const BetContext bet{ hand, std::nullopt, true, std::nullopt, RoundType::Normal };

    unsigned int value = 99;
    REQUIRE_NOTHROW(value = strategy.getBestBet(bet));
    REQUIRE(value <= hand.size());
}

TEST_CASE("CyborgStrategy: a name that matches no seat is fatal at start", "[cyborg][robustness]")
{
    // THE ONE PLACE CYBORG DIVERGES FROM RECKONER. Reckoner leaves its seat
    // unset here and later plays as though it were seat 0, reading another
    // player's bids and hand sizes as its own. Cyborg refuses.
    //
    // onGameStarted() is the last thing start() does, so this surfaces before a
    // card is dealt - the earliest anything could tell that the name and the
    // table disagree.

    SECTION("a name nobody at the table has")
    {
        CyborgStrategy watcher;
        watcher.setPlayerName("Nobody");

        GameEngine engine;
        engine.addObserver(&watcher);

        std::vector<std::unique_ptr<IMoveProvider>> providers;
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));

        // buildSetup names the seats P0 and P1.
        REQUIRE_THROWS_AS(engine.start(buildSetup(GameStructure::S_181, std::move(providers), 9u)),
                          std::logic_error);

        // Deliberately not calling run(): an observer that throws out of start()
        // leaves the engine part-way through starting and unusable.
    }

    SECTION("an empty name is the same misconfiguration, not a licence")
    {
        CyborgStrategy watcher; // never given a name at all

        GameEngine engine;
        engine.addObserver(&watcher);

        std::vector<std::unique_ptr<IMoveProvider>> providers;
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));

        REQUIRE_THROWS_AS(engine.start(buildSetup(GameStructure::S_181, std::move(providers), 9u)),
                          std::logic_error);
    }

    SECTION("setSeat beforehand is the escape hatch, and suppresses it")
    {
        // This is also how a Cyborg watching a seat it does not occupy would be
        // registered.
        CyborgStrategy watcher;
        REQUIRE_FALSE(watcher.isSeated());

        watcher.setSeat(Seat{1});
        REQUIRE(watcher.isSeated());

        GameEngine engine;
        engine.addObserver(&watcher);

        std::vector<std::unique_ptr<IMoveProvider>> providers;
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));
        providers.push_back(std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()));

        REQUIRE_NOTHROW(engine.start(buildSetup(GameStructure::S_181, std::move(providers), 9u)));
        REQUIRE_NOTHROW(engine.run());
        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }
}

TEST_CASE("CyborgStrategy: a registered strategy still plays a whole legal game",
          "[cyborg][robustness]")
{
    // THE COUNTERWEIGHT. Every case above is satisfied by a strategy that
    // refuses to do anything at all, so one of them has to prove it still plays.
    GameEngine engine;
    GameSetup setup;

    setup.seats.push_back(makeCyborgSeat("Cyborg", engine));
    setup.seats.push_back({"LowRisk1", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
    setup.seats.push_back({"LowRisk2", std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>())});
    setup.shuffleSeed = 4242;

    engine.start(std::move(setup));

    REQUIRE_NOTHROW(engine.run());
    REQUIRE(engine.getStatus() == GameStatus::Finished);
}
