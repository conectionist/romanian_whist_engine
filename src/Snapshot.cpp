#include <romanian_whist/Snapshot.h>

#include <romanian_whist/Player.h>
#include <romanian_whist/PlayerList.h>
#include <romanian_whist/Round.h>

#include <stdexcept>
#include <utility>

namespace romanian_whist
{
GameSnapshot snapshotFrom(const GameEngine& engine, std::optional<Seat> viewer)
{
    if(!engine.isSetUp())
        throw std::logic_error("snapshotFrom: the game has not been started");

    const unsigned int playerCount = engine.getPlayerCount();

    if(viewer && viewer->index >= playerCount)
        throw std::out_of_range("snapshotFrom: the viewer is not at this table");

    GameSnapshot snapshot;

    snapshot.viewer = viewer;
    snapshot.status = engine.getStatus();
    snapshot.phase = engine.getPhase();
    snapshot.roundIndex = engine.getCurrentRoundIndex();
    snapshot.roundCount = engine.getRoundCount();
    snapshot.trickCount = engine.getCurrentRoundTrickCount();
    snapshot.trickNumber = engine.getCurrentTrickNumber();
    snapshot.roundType = engine.getCurrentRoundType();
    snapshot.trump = engine.getCurrentTrumpCard();
    snapshot.roundLeader = engine.getRoundLeaderSeat();
    snapshot.activeSeat = engine.getActiveSeat();
    snapshot.forbiddenBet = engine.getForbiddenBet();

    const Trick& trick = engine.getCurrentTrick();

    snapshot.table = trick.getPlayedCards();
    snapshot.winning = engine.getCurrentTrickLeader();
    snapshot.ledBy = snapshot.table.empty() ? engine.getTrickLeaderSeat() : snapshot.table.front().seat;

    if(trick.hasLeadSuit())
        snapshot.leadSuit = trick.getLeadSuit();

    const PlayerList& players = engine.getPlayers();

    snapshot.seats.reserve(playerCount);

    for(unsigned int i = 0 ; i < playerCount ; i++)
    {
        const Seat holder{i};
        const Player& player = players.at(i);

        SeatSnapshot seat;

        seat.name = player.getName();
        seat.biddingOrder = engine.getBiddingOrder(holder);
        seat.bet = engine.getBet(holder);
        seat.tricksWon = engine.getTricksWon(holder);
        seat.roundScore = engine.getRoundScore(holder);
        seat.totalScore = engine.getTotalScore(holder);
        seat.winStreak = player.getConsecutiveWins();
        seat.lossStreak = player.getConsecutiveLosses();

        // The only read of a hand here, so every hand that leaves passes the check below.
        const std::vector<Card>& hand = player.getHand();

        seat.cardsLeft = static_cast<unsigned int>(hand.size());

        if(viewer && engine.canSeeHand(*viewer, holder))
            seat.hand = hand;

        if(seat.bet)
        {
            snapshot.betsPlaced++;
            snapshot.betSum += *seat.bet;
        }

        snapshot.seats.push_back(std::move(seat));
    }

    return snapshot;
}

} // namespace romanian_whist
