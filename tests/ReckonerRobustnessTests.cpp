#include <catch2/catch_test_macros.hpp>

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/CardValidator.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace romanian_whist;

// ReckonerStrategy is the only strategy in the engine that is also an
// IGameObserver, and the only one whose answers depend on state that arrives from
// somewhere other than the argument it was called with. These are the cases about
// what happens when that state is absent or disagrees with the position - not
// about how well it plays, which ReckonerStrategyTests and the tournament cover.

TEST_CASE("ReckonerStrategy: an unregistered strategy refuses to decide", "[reckoner][robustness]")
{
    // THE MISCONFIGURATION THIS CLASS INVITES. Every other IStrategy is complete
    // once constructed; this one is inert until an engine has been told to feed
    // it. Constructing one and handing it to AiMoveProvider is the natural thing
    // to do, compiles, and used to produce a bot that played a four-handed game at
    // whatever table it was actually sitting at.
    ReckonerStrategy strategy(reckoner::ReckonerKnobs::alpha(), 42);

    // JACK..ACE, because the deck a Reckoner encodes through depends on the seat
    // count: ranksPerSuit is 2n and the low rank is 1 + (6-n)*2, so a two-handed
    // deck is Jack..Ace and a six-handed one is Three..Ace. The hand below has to
    // exist at the table this strategy is about to be told it is sitting at.
    const std::vector<Card> hand = { Card{ Rank::Ace, Suit::Hearts },
                                     Card{ Rank::King, Suit::Spades } };
    const std::vector<Card> played;

    const PlayContext play{ hand, played, std::nullopt, std::nullopt, 0, 0 };

    REQUIRE_THROWS_AS(strategy.getBestChoice(play), std::logic_error);

    const BetContext bet{ hand, std::nullopt, true, std::nullopt, RoundType::Normal };

    REQUIRE_THROWS_AS(strategy.getBestBet(bet), std::logic_error);

    // AND IT STOPS REFUSING AS SOON AS IT IS FED. The standalone hook is the other
    // half of the contract - a caller driving the tracker without an engine is
    // supported - so the check has to key on the tracker being initialised rather
    // than on an engine having been seen.
    strategy.onRoundStart(2, 1, std::nullopt, 0, 0);

    REQUIRE_NOTHROW(strategy.getBestChoice(play));
    REQUIRE_NOTHROW(strategy.getBestBet(bet));
}

TEST_CASE("ReckonerStrategy: a stale tracker cannot produce an illegal card",
          "[reckoner][robustness]")
{
    // THE GUARD, exercised by making the replica disagree with the position on the
    // one rule that matters. The tracker is told a trick is being led - it has no
    // cards in it and no trump - while the PlayContext says hearts were led and
    // this hand holds one. Anything that decides from the tracker alone is free to
    // discard the spade; the rules are not.
    //
    // This is a stand-in for every way the two can drift: a dropped onCardPlayed,
    // a seat resolved wrongly, a future bug in the tracker. The strategy cannot
    // detect drift, so what is asserted is that it cannot ACT on it illegally.
    //
    // ALPHA, AND THE PRESET IS NOT INCIDENTAL. Alpha has K_play = 0, so choosePlay()
    // answers from the base policy and never enters the Monte-Carlo rollout - and
    // the rollout is not safe to drive from a tracker this inconsistent. Written
    // first with gamma(), this case made UBSan report two errors inside the
    // SIMULATION: BasePolicy::chooseCard() returns INVALID_CARD_ID (255) for an
    // imagined seat holding no legal card, and RolloutEngine::rollout() passes it
    // to cardBit() and maskSuit() unchecked - a 255-bit shift and a read of
    // suitMasks[31]. See RolloutEngine.cpp:256 and :278.
    //
    // That is a real defect and it is NOT this case's to fix: it lives in the
    // rollout's handling of its own samples, it is unreachable from ordinary play
    // (the engine's own [reckoner] games report zero UBSan errors), and a guess at
    // the right recovery there would bias the estimator in a way no assertion here
    // would catch. What is tested at Alpha is the same guard on the same drift; the
    // rollout half is written up separately.
    ReckonerStrategy strategy(reckoner::ReckonerKnobs::alpha(), 7);

    strategy.onRoundStart(4, 4, std::nullopt, 0, 0);

    // Seven..Ace, the four-handed deck - see the case above on why this matters.
    const std::vector<Card> hand = { Card{ Rank::Seven, Suit::Hearts },
                                     Card{ Rank::Ace, Suit::Spades },
                                     Card{ Rank::King, Suit::Clubs } };

    // Hearts led by somebody the tracker never heard about.
    const std::vector<Card> played = { Card{ Rank::Ten, Suit::Hearts } };

    const PlayContext context{ hand, played, std::nullopt, Suit::Hearts, 1, 0 };

    const std::optional<Card> chosen = strategy.getBestChoice(context);

    REQUIRE(chosen.has_value());

    // Holding a heart, it must follow suit - whatever the rollout preferred.
    REQUIRE(chosen->suit == Suit::Hearts);

    // And the same answer stated the way the engine states it, so this case cannot
    // pass by agreeing with a rule nobody else applies.
    const CardValidator validator;
    const std::vector<Card> legal = validator.getLegalCards(hand, std::nullopt, Suit::Hearts);

    REQUIRE(std::find(legal.begin(), legal.end(), *chosen) != legal.end());
}

TEST_CASE("ReckonerStrategy: a hand its deck cannot encode still gets a legal answer",
          "[reckoner][robustness]")
{
    // THE OTHER DIRECTION OF DRIFT, and the one a comparison of the answer cannot
    // catch. The deck profiles are not nested both ways: a two-handed deck is
    // Jack..Ace, a six-handed one is Three..Ace. So a tracker that believes the
    // table is SMALLER than it is meets a card it has no id for, and cardToId()
    // throws std::invalid_argument from inside cardsToMask() - before there is any
    // answer to validate.
    //
    // Found by writing the two cases above: they failed with "card rank 5 not in
    // deck for 2 players", which is this path, reached by accident.
    ReckonerStrategy strategy(reckoner::ReckonerKnobs::alpha(), 3);

    // Told it is at a two-handed table...
    strategy.onRoundStart(2, 3, std::nullopt, 0, 0);

    // ...and then handed a six-handed hand, whose low cards its deck cannot name.
    const std::vector<Card> hand = { Card{ Rank::Three, Suit::Hearts },
                                     Card{ Rank::Four, Suit::Hearts },
                                     Card{ Rank::Five, Suit::Clubs } };
    const std::vector<Card> played = { Card{ Rank::Six, Suit::Hearts } };

    const PlayContext context{ hand, played, std::nullopt, Suit::Hearts, 1, 0 };

    std::optional<Card> chosen;
    REQUIRE_NOTHROW(chosen = strategy.getBestChoice(context));

    REQUIRE(chosen.has_value());

    // Still following suit, because the fallback is the validator's own list.
    REQUIRE(chosen->suit == Suit::Hearts);

    // AND THE BID SEAM TOO, which encodes the same hand the same way.
    const BetContext bet{ hand, std::nullopt, true, std::nullopt, RoundType::Normal };

    unsigned int bid = 99;
    REQUIRE_NOTHROW(bid = strategy.getBestBet(bet));
    REQUIRE(bid <= hand.size());
}

TEST_CASE("ReckonerTracker: a seat count past the array width is clamped",
          "[reckoner][robustness]")
{
    // UNREACHABLE THROUGH THE ENGINE, which rejects any table outside 2..6 by name
    // - so this is about the cost if it ever became reachable. Every per-seat array
    // in the tracker is written by a loop bounded by playerCount, so an unclamped
    // count is a buffer overflow rather than a bad estimate, and GCC says so at -O2.
    //
    // Run this one under a sanitizer for it to mean anything: without the clamp it
    // is a heap write past the end of a std::array, which passes uninstrumented as
    // often as not.
    reckoner::ReckonerTracker tracker;

    REQUIRE_NOTHROW(tracker.initRound(99, 3, 42, 77, std::nullopt));

    REQUIRE(tracker.playerCount <= reckoner::ReckonerTracker::MaxPlayers);
    REQUIRE(tracker.mySeat < reckoner::ReckonerTracker::MaxPlayers);
    REQUIRE(tracker.opener < reckoner::ReckonerTracker::MaxPlayers);
}

TEST_CASE("ReckonerStrategy: a registered strategy still plays a whole legal game",
          "[reckoner][robustness]")
{
    // THE COUNTERWEIGHT. Three cases above are about refusing and clamping, and a
    // guard that rejected everything would satisfy all of them. This is the one
    // that says the ordinary path still works, through the factory, end to end.
    GameEngine engine;
    GameSetup setup;

    setup.seats.push_back(makeReckonerSeat("Reckoner", engine, reckoner::ReckonerKnobs::alpha(), 42));
    setup.seats.push_back({ "LowRisk", std::make_unique<AiMoveProvider>(
                                           std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back({ "LowRisk2", std::make_unique<AiMoveProvider>(
                                            std::make_unique<LowRiskStrategy>()) });
    setup.shuffleSeed = 4242;

    engine.start(std::move(setup));

    REQUIRE_NOTHROW(engine.run());
    REQUIRE(engine.getStatus() == GameStatus::Finished);
}
