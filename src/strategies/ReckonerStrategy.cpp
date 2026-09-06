#include <romanian_whist/strategies/ReckonerStrategy.h>
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/reckoner/RolloutEngine.h>

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

unsigned int ReckonerStrategy::getBestBet(const BetContext& context)
{
    if(context.roundType == RoundType::Forehead || context.roundType == RoundType::Hidden)
    {
        return (context.forbiddenBet && *context.forbiddenBet == 0) ? 1 : 0;
    }

    const reckoner::Mask myHand = reckoner::cardsToMask(context.hand, tracker.playerCount);
    return reckoner::RolloutEngine::chooseBid(static_cast<unsigned int>(context.hand.size()),
                                             myHand,
                                             context.forbiddenBet,
                                             tracker,
                                             knobs,
                                             rng);
}

std::optional<Card> ReckonerStrategy::getBestChoice(const PlayContext& context)
{
    if(context.hand.empty())
        return std::nullopt;

    const reckoner::Mask myHand = reckoner::cardsToMask(context.hand, tracker.playerCount);
    const reckoner::CardId chosenId =
        reckoner::RolloutEngine::choosePlay(myHand, tracker, knobs, rng);

    if(chosenId == reckoner::INVALID_CARD_ID)
    {
        // Fallback to card validator legal cards
        const auto legal = cardValidator.getLegalCards(context.hand, context.trump, context.leadSuit);
        return legal.empty() ? std::nullopt : std::optional<Card>(legal.front());
    }

    return reckoner::idToCard(chosenId, tracker.playerCount);
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
