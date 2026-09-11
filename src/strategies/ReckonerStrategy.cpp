#include <romanian_whist/strategies/ReckonerStrategy.h>
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/reckoner/RolloutEngine.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace romanian_whist
{

ReckonerStrategy::ReckonerStrategy(reckoner::ReckonerKnobs _knobs, std::optional<std::uint32_t> seed)
    : knobs(_knobs)
{
    if(seed)
        rng.seed(*seed);
    else
        rng.seed(std::random_device{}());

    tracker.rankGapConfScale = knobs.rankGapConfScale;
}

ReckonerStrategy::ReckonerStrategy(reckoner::ReckonerPreset preset, std::optional<std::uint32_t> seed)
    : ReckonerStrategy([preset]() {
          switch(preset)
          {
              case reckoner::ReckonerPreset::Easy:
                  return reckoner::ReckonerKnobs::easy();
              case reckoner::ReckonerPreset::Medium:
                  return reckoner::ReckonerKnobs::medium();
              case reckoner::ReckonerPreset::Brutal:
                  return reckoner::ReckonerKnobs::brutal();
              case reckoner::ReckonerPreset::Hard:
              default:
                  return reckoner::ReckonerKnobs::hard();
          }
      }(), seed)
{
}

void ReckonerStrategy::setSeat(Seat seat)
{
    mySeat = seat;
    tracker.mySeat = seat.index;
}

void ReckonerStrategy::setPlayerName(const std::string& name)
{
    playerName = name;
}

const reckoner::ReckonerTracker& ReckonerStrategy::getTracker() const
{
    return tracker;
}

reckoner::ReckonerTracker& ReckonerStrategy::getTracker()
{
    return tracker;
}

const reckoner::ReckonerKnobs& ReckonerStrategy::getKnobs() const
{
    return knobs;
}

void ReckonerStrategy::setKnobs(const reckoner::ReckonerKnobs& _knobs)
{
    knobs = _knobs;
    tracker.rankGapConfScale = knobs.rankGapConfScale;
}

void ReckonerStrategy::requireTracker(const char* decision) const
{
    if(tracker.roundInitialised)
        return;

    // NOT AN ASSERT AND NOT A SHRUG. This strategy is an IStrategy *and* an
    // IGameObserver, and unlike the other four it is useless as the first without
    // having been registered as the second: every fact it plays from arrives
    // through the observer callbacks. Registered late, or not at all, it keeps the
    // tracker's initialisers - a four-handed table, no trump, an empty deck - and
    // then plays from them.
    //
    // WHAT THAT LOOKED LIKE BEFORE THIS THREW: a bot that chose a card outside the
    // deck profile of the table it was actually sitting at, which the engine
    // rejected with "played a card that is not legal in this trick" - a failure
    // several layers from its cause, blamed on the engine's validation. And once
    // getBestChoice() started filtering its answer through the card validator
    // (which it now does), that same misconfiguration would have stopped throwing
    // altogether and simply played weakly forever, with nothing to read.
    //
    // So this is the diagnostic that has to exist BECAUSE the guard exists.
    throw std::logic_error(
        std::string("ReckonerStrategy::") + decision
        + " was called before the strategy saw a round start, which means it was never"
          " registered as an observer. Build the seat with makeReckonerSeat(), or call"
          " GameEngine::addObserver() with it before GameEngine::start().");
}

unsigned int ReckonerStrategy::getBestBet(const BetContext& context)
{
    requireTracker("getBestBet");

    // The lowest bid that is not the forbidden one. Used for the round types that
    // forbid reading the hand at all, and as the answer when the hand cannot be
    // encoded - see the catch below.
    const auto safestBid = [&context]
    {
        return (context.forbiddenBet && *context.forbiddenBet == 0) ? 1u : 0u;
    };

    if(context.roundType == RoundType::Forehead || context.roundType == RoundType::Hidden)
    {
        return safestBid();
    }

    // CONTAINED THE SAME WAY getBestChoice() IS, and for the same reason: this
    // encodes the hand through the tracker's deck profile too, so a replica that
    // disagrees with the real table throws out of cardsToMask() rather than
    // returning a poor bid. A bid is always legal at 0 (or 1 where 0 is barred),
    // so there is a safe answer to fall back to.
    try
    {
        const reckoner::Mask myHand = reckoner::cardsToMask(context.hand, tracker.playerCount);
        return reckoner::RolloutEngine::chooseBid(static_cast<unsigned int>(context.hand.size()),
                                                 myHand,
                                                 context.forbiddenBet,
                                                 tracker,
                                                 knobs,
                                                 rng);
    }
    catch(const std::logic_error&)
    {
        return safestBid();
    }
}

std::optional<Card> ReckonerStrategy::getBestChoice(const PlayContext& context)
{
    requireTracker("getBestChoice");

    if(context.hand.empty())
        return std::nullopt;

    // THE AUTHORITY ON WHAT MAY BE PLAYED IS THE CONTEXT, NOT THE TRACKER, and
    // that is the whole point of computing this first rather than only in the
    // fallback below. PlayContext.h says it outright - "run it through
    // CardValidator::getLegalCards() before choosing from it" - and until now this
    // function was the one IStrategy in the engine that did not, on its main path.
    //
    // WHY IT MATTERS HERE MORE THAN ANYWHERE ELSE. RolloutEngine::choosePlay() is
    // handed no PlayContext at all: it reconstructs the lead suit from
    // tracker.currentTrickCards, the trump from tracker.trumpSuit and the deck
    // profile from tracker.playerCount, and decides legality from those. So the
    // card it returns is legal with respect to a REPLICA of the position. Any way
    // that replica can drift from the real one - a callback that never arrived, a
    // seat resolved wrongly, a future bug in the tracker - becomes an illegal move
    // that no layer between here and GameEngine would catch.
    //
    // This does not fix that; it contains it. The rollout still decides, and its
    // answer is still taken whenever it is playable. What changes is that a
    // divergence now costs one weak card instead of a std::logic_error out of the
    // engine on the game thread.
    const std::vector<Card> legal =
        cardValidator.getLegalCards(context.hand, context.trump, context.leadSuit);

    if(legal.empty())
        return std::nullopt;

    // THE ENCODING CAN FAIL OUTRIGHT, not merely disagree, which is the half of
    // this that a comparison of the answer cannot catch. cardToId() maps a card
    // through the deck profile of tracker.playerCount, and the profiles are not
    // nested the same way in both directions: a 2-player deck is Jack..Ace and a
    // 6-player deck is Three..Ace, so a tracker that believes the table is SMALLER
    // than it is meets a card its deck has no id for and throws
    // std::invalid_argument from inside cardsToMask() - before the answer exists
    // to be checked.
    //
    // Caught here rather than prevented, because preventing it needs the real seat
    // count and PlayContext does not carry one. So the rule this function follows
    // is the whole rule: whatever the replica says, a legal card comes back.
    try
    {
        const reckoner::Mask myHand = reckoner::cardsToMask(context.hand, tracker.playerCount);
        const reckoner::CardId chosenId =
            reckoner::RolloutEngine::choosePlay(myHand, tracker, knobs, rng);

        if(chosenId != reckoner::INVALID_CARD_ID)
        {
            const Card chosen = reckoner::idToCard(chosenId, tracker.playerCount);

            if(std::find(legal.begin(), legal.end(), chosen) != legal.end())
                return chosen;
        }
    }
    catch(const std::logic_error&)
    {
        // std::invalid_argument is one of these. Deliberately NOT a catch-all: a
        // bad_alloc or a stop request out of the rollout is not this function's to
        // swallow.
    }

    // Either the rollout had nothing to say, or what it said is not playable at
    // this table. The first legal card is what this function already fell back to
    // in the first case, and is no worse an answer in the second.
    return legal.front();
}

void ReckonerStrategy::onGameStarted(const GameEngine& engine)
{
    const unsigned int n = engine.getPlayerCount();
    if(!mySeat && !playerName.empty())
    {
        for(unsigned int i = 0; i < n; ++i)
        {
            if(engine.getPlayers().at(i).getName() == playerName)
            {
                setSeat(Seat{i});
                break;
            }
        }
    }

    // Detect known deterministic opponents (§1.4, §3)
    for(unsigned int i = 0; i < n; ++i)
    {
        const std::string& name = engine.getPlayers().at(i).getName();
        if(name.find("LowRisk") != std::string::npos || name.find("Ducking") != std::string::npos)
        {
            tracker.opponentModels[i].isDeterministic = true;
        }
    }
}

void ReckonerStrategy::onRoundStarted(const GameEngine& engine)
{
    const unsigned int n = engine.getPlayerCount();
    const unsigned int r = engine.getCurrentRoundTrickCount();
    const unsigned int myIdx = mySeat ? mySeat->index : 0;
    const unsigned int opener = engine.getRoundLeaderSeat().index;
    const std::optional<Card> trump = engine.getCurrentTrumpCard();

    tracker.initRound(n, r, myIdx, opener, trump);
}

void ReckonerStrategy::onBetPlaced(const GameEngine& /*engine*/, Seat seat, unsigned int bet)
{
    tracker.recordBid(seat.index, bet);
}

void ReckonerStrategy::onCardPlayed(const GameEngine& /*engine*/, Seat seat, const Card& card)
{
    tracker.recordCardPlayed(seat.index, card);
}

void ReckonerStrategy::onTrickWon(const GameEngine& /*engine*/, Seat winner, unsigned int /*trickNumber*/)
{
    tracker.recordTrickWon(winner.index);
}

void ReckonerStrategy::onRoundScored(const GameEngine& engine)
{
    const unsigned int n = engine.getPlayerCount();
    std::vector<int> roundScores(n, 0);
    std::vector<int> runningTotals(n, 0);

    for(unsigned int i = 0; i < n; ++i)
    {
        const Seat s{i};
        roundScores[i] = engine.getRoundScore(s);
        runningTotals[i] = engine.getTotalScore(s) + roundScores[i];
    }

    tracker.recordRoundEnd(roundScores, runningTotals);
}

void ReckonerStrategy::onRoundStart(unsigned int n, unsigned int r, std::optional<Card> trump,
                                   unsigned int openerSeat, unsigned int mySeatIdx)
{
    tracker.initRound(n, r, mySeatIdx, openerSeat, trump);
}

void ReckonerStrategy::onBid(unsigned int seat, unsigned int value)
{
    tracker.recordBid(seat, value);
}

void ReckonerStrategy::onCardPlayed(unsigned int seat, const Card& card)
{
    tracker.recordCardPlayed(seat, card);
}

void ReckonerStrategy::onTrickWon(unsigned int winner)
{
    tracker.recordTrickWon(winner);
}

void ReckonerStrategy::onRoundEnd(const std::vector<int>& roundScores, const std::vector<int>& runningTotals)
{
    tracker.recordRoundEnd(roundScores, runningTotals);
}

SeatSetup makeReckonerSeat(const std::string& name, GameEngine& engine,
                           const reckoner::ReckonerKnobs& knobs,
                           std::optional<std::uint32_t> seed)
{
    auto strategy = std::make_unique<ReckonerStrategy>(knobs, seed);
    strategy->setPlayerName(name);
    engine.addObserver(strategy.get());
    return SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))};
}

} // namespace romanian_whist
