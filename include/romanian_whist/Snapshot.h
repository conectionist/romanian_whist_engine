#ifndef ROMANIAN_WHIST_SNAPSHOT_H
#define ROMANIAN_WHIST_SNAPSHOT_H

#include <romanian_whist/Card.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/Seat.h>
#include <romanian_whist/Trick.h>

#include <optional>
#include <string>
#include <vector>

namespace romanian_whist
{
// One seat as a viewer may see it. GameSnapshot::seats[i] is Seat{i}.
struct SeatSnapshot
{
    std::string name;

    // 1 for the round leader, then round the table.
    unsigned int biddingOrder = 0;

    // Empty until that seat has bid, which tells a bid of 0 from no bid.
    std::optional<unsigned int> bet;

    unsigned int tricksWon = 0;

    // Public even when the hand is not: it is how many card backs to draw.
    unsigned int cardsLeft = 0;

    // As getRoundScore() and getTotalScore(): inside onRoundScored() the total does not yet
    // include the round.
    int roundScore = 0;
    int totalScore = 0;

    unsigned int winStreak = 0;
    unsigned int lossStreak = 0;

    // Engaged only when the viewer may see this hand, in the engine's order. Absent rather than
    // flagged hidden, so a serialiser that forgets to check has nothing to leak.
    std::optional<std::vector<Card>> hand;
};

// The game as one viewer may see it, by value, so it can be handed to another thread or sent
// to another machine. It is copyable and default-constructible, and Seat has no default
// constructor, so a seat held directly in it is an optional.
struct GameSnapshot
{
    // Who this was built for. Empty for a spectator, who sees no hand at all.
    std::optional<Seat> viewer;

    GameStatus status = GameStatus::NotStarted;
    GamePhase phase = GamePhase::NotStarted;

    // 0-based. At onRoundComplete() it already names the next round, except after the last.
    unsigned int roundIndex = 0;
    unsigned int roundCount = 0;
    unsigned int trickCount = 0;

    // 1-based; 0 until the round's first trick starts.
    unsigned int trickNumber = 0;

    RoundType roundType = RoundType::Normal;

    // Empty in 8-card rounds, and before a round is dealt.
    std::optional<Card> trump;

    // Engaged in every snapshot.
    std::optional<Seat> roundLeader;

    // Empty between turns.
    std::optional<Seat> activeSeat;

    // Public: anyone can work it out from the bids.
    std::optional<unsigned int> forbiddenBet;

    // The cards on the table, in the order they were played. A finished trick stays here until
    // the next trick starts or the game moves on to the next round.
    std::vector<PlayedCard> table;

    // The engine's own ranking of the cards on the table; empty while there are none.
    std::optional<Seat> winning;

    // Who led the cards on the table, or who will lead while there are none. Engaged in every
    // snapshot. Not getTrickLeaderSeat(), which has already moved to the winner by onTrickWon().
    std::optional<Seat> ledBy;

    std::optional<Suit> leadSuit;

    std::vector<SeatSnapshot> seats;

    unsigned int betsPlaced = 0;
    unsigned int betSum = 0;
};

// The viewer has no default, so a snapshot built without deciding who will see it cannot be
// written. Call it on the thread running the game. Throws std::logic_error before start(), and
// std::out_of_range for a viewer not at the table.
GameSnapshot snapshotFrom(const GameEngine& engine, std::optional<Seat> viewer);

} // namespace romanian_whist

#endif
