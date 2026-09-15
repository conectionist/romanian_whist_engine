#include <romanian_whist/strategies/CyborgStrategy.h>

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/TrickHeuristics.h>
#include <romanian_whist/strategies/cyborg/Bidding.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace romanian_whist
{

CyborgStrategy::CyborgStrategy(cyborg::CyborgKnobs _knobs) : knobs(_knobs)
{
}

void CyborgStrategy::setSeat(Seat seat)
{
    mySeat = seat;
    memory.mySeat = seat.index;
}

void CyborgStrategy::setPlayerName(const std::string& name)
{
    playerName = name;
}

bool CyborgStrategy::isSeated() const
{
    return mySeat.has_value();
}

std::size_t CyborgStrategy::getFallbacksTaken() const
{
    return fallbacksTaken;
}

const std::optional<Card>& CyborgStrategy::getPlannedLead() const
{
    return plannedLead;
}

const common::RoundMemory& CyborgStrategy::getMemory() const
{
    return memory;
}

common::RoundMemory& CyborgStrategy::getMemory()
{
    return memory;
}

const cyborg::CyborgKnobs& CyborgStrategy::getKnobs() const
{
    return knobs;
}

void CyborgStrategy::setKnobs(const cyborg::CyborgKnobs& newKnobs)
{
    knobs = newKnobs;
}

void CyborgStrategy::requireMemory(const char* decision) const
{
    if(memory.roundInitialised)
        return;

    // NOT AN ASSERT AND NOT A SHRUG. Everything this strategy decides from
    // arrives through the observer callbacks, so one that was never registered
    // keeps the memory's initialisers - a four-handed table, no trump, an empty
    // deck - and then plays from them. At a four-handed table that is a bot
    // confidently reading a deal that never happened; at five or six it is an
    // exception out of cardToId() several layers from the cause.
    //
    // And because both decisions below fall back to the heuristics rather than
    // failing, that misconfiguration would otherwise never announce itself at
    // all: it would simply play weakly forever. This is the diagnostic that has
    // to exist BECAUSE the fallback exists.
    throw std::logic_error(
        std::string("CyborgStrategy::") + decision +
        " was called before the strategy saw a round start, which means it was never"
        " registered as an observer. Build the seat with makeCyborgSeat(), or call"
        " GameEngine::addObserver() with it before GameEngine::start().");
}

common::Mask CyborgStrategy::encodeHand(const std::vector<Card>& hand) const
{
    return common::cardsToMask(hand, memory.playerCount);
}

bool CyborgStrategy::agreesWith(common::Mask myHand, std::size_t handSize,
                                std::size_t trickSize) const
{
    // Every card I am holding must still be live. A card the memory has already
    // seen played cannot also be in my hand.
    if((myHand & ~memory.live) != 0)
        return false;

    if(handSize != memory.handSize[memory.mySeat])
        return false;

    // The engine asks the provider BEFORE it announces the card, so at this
    // point the memory should have seen exactly the cards already on the table.
    return trickSize == memory.currentTrickCards.size();
}

unsigned int CyborgStrategy::heuristicBid(const BetContext& context) const
{
    // LowRiskStrategy::getBestBet, reproduced rather than delegated to, so that
    // the parity test pins a decision this class actually makes.
    const bool blind =
        context.roundType == RoundType::Forehead || context.roundType == RoundType::Hidden;

    unsigned int bet =
        blind ? 0u : heuristics::countLikelyWinners(context.hand, context.trump);

    if(context.forbiddenBet && bet == *context.forbiddenBet)
    {
        // Steps DOWN rather than up: shedding a trick it expected to win is
        // something a hand full of ducking chances can usually manage, whereas
        // forcing an extra one out of a hand that has none is a coin flip.
        //
        // Note this is not the same as the safestBid() below. Barred at 3, this
        // bids 2; safestBid() would bid 0.
        bet = (bet == 0u) ? 1u : bet - 1u;
    }

    return bet;
}

std::optional<Card> CyborgStrategy::heuristicPlay(const PlayContext& context,
                                                  const std::vector<Card>& legal) const
{
    const bool owesTricks = context.tricksWon < context.bet;

    return owesTricks ? heuristics::chooseWinningCard(context, legal)
                      : heuristics::chooseDuckingCard(context, legal);
}

unsigned int CyborgStrategy::getBestBet(const BetContext& context)
{
    requireMemory("getBestBet");

    // The lowest bid that is not the barred one. Used where the hand may not be
    // read at all, and as the answer when it cannot be encoded.
    const auto safestBid = [&context] {
        return (context.forbiddenBet && *context.forbiddenBet == 0) ? 1u : 0u;
    };

    // Forehead and Hidden both mean this hand may not be read to bid on.
    if(context.roundType == RoundType::Forehead || context.roundType == RoundType::Hidden)
        return safestBid();

    try
    {
        const common::Mask myHand = encodeHand(context.hand);

        if(knobs.useHeuristicBid)
            return heuristicBid(context);

        // The hand's size IS the round's trick count, and the context is the
        // authority. A memory that believes otherwise is describing another round.
        const auto trickCount = static_cast<unsigned int>(context.hand.size());
        if(trickCount != memory.roundTrickCount)
            throw std::logic_error("CyborgStrategy::getBestBet: the memory and the hand disagree on the trick count");

        cyborg::BidInputs inputs;
        inputs.hand = context.hand;
        inputs.playerCount = memory.playerCount;
        inputs.trickCount = trickCount;
        inputs.trump = context.trump;
        inputs.roundType = context.roundType;
        inputs.biddingPosition = memory.bidsPlaced();
        inputs.previousBidSum = memory.previousBidSum();

        inputs.bidsInBiddingOrder.reserve(inputs.biddingPosition);
        for(unsigned int step = 0; step < inputs.biddingPosition; ++step)
        {
            const unsigned int seat = (memory.opener + step) % memory.playerCount;
            inputs.bidsInBiddingOrder.push_back(memory.bids[seat]);
        }

        inputs.forbiddenBet = context.forbiddenBet;
        inputs.odds = cyborg::makeOddsContext(
            memory, myHand,
            context.trump ? std::optional(context.trump->suit) : std::nullopt,
            knobs.duckPropensity);

        const cyborg::BidResult result = cyborg::chooseBid(inputs, knobs);

        // Belt and braces. The rules are built never to produce either of these,
        // but a bid the engine would refuse deserves the degraded answer, not a
        // stopped game.
        if(result.bid > trickCount || (context.forbiddenBet && result.bid == *context.forbiddenBet))
        {
            ++fallbacksTaken;
            return heuristicBid(context);
        }

        plannedLead = result.plannedLead;
        return result.bid;
    }
    catch(const std::logic_error&)
    {
        // std::invalid_argument is one of these. Deliberately not a catch-all:
        // a bad_alloc is not this function's to swallow.
        return safestBid();
    }
}

std::optional<Card> CyborgStrategy::getBestChoice(const PlayContext& context)
{
    requireMemory("getBestChoice");

    if(context.hand.empty())
        return std::nullopt;

    // THE AUTHORITY ON WHAT MAY BE PLAYED IS THE CONTEXT, NOT THE MEMORY, which
    // is why this is computed first rather than only in the fallback. The memory
    // is a replica of the position built from callbacks; the context is the
    // engine's own account of it.
    const std::vector<Card> legal =
        cardValidator.getLegalCards(context.hand, context.trump, context.leadSuit);

    if(legal.empty())
        return std::nullopt;

    std::optional<Card> chosen;

    try
    {
        const common::Mask myHand = encodeHand(context.hand);

        if(agreesWith(myHand, context.hand.size(), context.playedCards.size()))
            chosen = heuristicPlay(context, legal);
    }
    catch(const std::logic_error&)
    {
        // As above: contained, not prevented. Whatever the replica says, a legal
        // card comes back.
    }

    if(chosen && std::find(legal.begin(), legal.end(), *chosen) != legal.end())
        return chosen;

    // Either the memory disagreed with the position or it could not describe it.
    // ReckonerStrategy settles for legal.front() here because it has no cheaper
    // good answer linked; Cyborg does - the heuristics are already built, already
    // tested, and pick a sensible card from the same legal list.
    fallbacksTaken++;

    if(const std::optional<Card> fallback = heuristicPlay(context, legal))
        return fallback;

    return legal.front();
}

void CyborgStrategy::onGameStarted(const GameEngine& engine)
{
    // An explicit setSeat() wins, and is the escape hatch for a Cyborg watching
    // a seat it does not occupy.
    if(mySeat)
        return;

    const unsigned int n = engine.getPlayerCount();

    for(unsigned int i = 0; i < n; ++i)
    {
        if(engine.getPlayers().at(i).getName() == playerName)
        {
            setSeat(Seat{i});
            return;
        }
    }

    // ReckonerStrategy leaves the seat unset here and later plays as though it
    // were seat 0, reading another player's bids and hand sizes as its own.
    // Refuse instead. onGameStarted() is the last thing start() does, so this
    // surfaces from the caller's start() before a card is dealt - which is the
    // earliest anything can tell that the name and the table disagree.
    throw std::logic_error(
        "CyborgStrategy::onGameStarted: no seat at this table is named \"" + playerName +
        "\", so this strategy cannot tell which bids and hand sizes are its own."
        " Build the seat with makeCyborgSeat(), whose name argument is the seat name,"
        " or call setSeat() before GameEngine::start().");
}

void CyborgStrategy::onRoundStarted(const GameEngine& engine)
{
    plannedLead = std::nullopt;
    memory.initRound(engine.getPlayerCount(), engine.getCurrentRoundTrickCount(),
                     mySeat ? mySeat->index : 0, engine.getRoundLeaderSeat().index,
                     engine.getCurrentTrumpCard());
}

void CyborgStrategy::onBetPlaced(const GameEngine& /*engine*/, Seat seat, unsigned int bet)
{
    memory.recordBid(seat.index, bet);
}

void CyborgStrategy::onCardPlayed(const GameEngine& /*engine*/, Seat seat, const Card& card)
{
    memory.recordCardPlayed(seat.index, card);
}

void CyborgStrategy::onTrickWon(const GameEngine& /*engine*/, Seat winner,
                                unsigned int /*trickNumber*/)
{
    memory.recordTrickWon(winner.index);
}

void CyborgStrategy::onRoundStart(unsigned int n, unsigned int r, std::optional<Card> trump,
                                  unsigned int openerSeat, unsigned int mySeatIdx)
{
    plannedLead = std::nullopt;
    memory.initRound(n, r, mySeatIdx, openerSeat, trump);
}

void CyborgStrategy::onBid(unsigned int seat, unsigned int value)
{
    memory.recordBid(seat, value);
}

void CyborgStrategy::onCardPlayed(unsigned int seat, const Card& card)
{
    memory.recordCardPlayed(seat, card);
}

void CyborgStrategy::onTrickWon(unsigned int winner)
{
    memory.recordTrickWon(winner);
}

SeatSetup makeCyborgSeat(const std::string& name, GameEngine& engine,
                         const cyborg::CyborgKnobs& knobs)
{
    auto strategy = std::make_unique<CyborgStrategy>(knobs);
    strategy->setPlayerName(name);

    // Registered BEFORE ownership moves into the provider. Moving a unique_ptr
    // does not move the pointee, so the raw pointer stays valid; and start()
    // fires onGameStarted() last, so every observer must be in place by then.
    engine.addObserver(strategy.get());

    return SeatSetup{name, std::make_unique<AiMoveProvider>(std::move(strategy))};
}

} // namespace romanian_whist
