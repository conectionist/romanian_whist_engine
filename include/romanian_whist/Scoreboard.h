#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include <romanian_whist/Round.h>
#include <romanian_whist/PlayerList.h>

#include <vector>
#include <utility>

namespace romanian_whist
{
// Exact bids, or misses, in a row that pay +10 or -10. The run then starts over,
// so the counters never read more than StreakLength - 1 once a round is scored.
inline constexpr unsigned int StreakLength = 5;

enum class GameStructure
{
    S_181,
    S_818
};

class Scoreboard
{
private:
    std::vector<Round> rounds;
    unsigned int currentRound;

public:
    Scoreboard();
    void initialize(const GameStructure& structure, 
                    bool endWithForeheadAndHidden, 
                    bool all1GamesAreForehead,
                    PlayerList& players);
    
    void incrementCurrentRound();
    Round& getCurrentRound();
    const Round& getCurrentRound() const;

    // Any round in the schedule. initialize() lays the whole schedule out up
    // front, so a round past getCurrentRoundIndex() exists but has not been
    // played yet - no bets, no tricks, no trump. Throws std::out_of_range
    // past the end of the schedule; GameEngine::getRound() checks first and
    // throws with a message naming the schedule length.
    const Round& getRound(unsigned int index) const;

    unsigned int getRoundCount() const;
    unsigned int getCurrentRoundIndex() const;
    
    void calculateScores(PlayerList& players);
    void commitRoundScores(PlayerList& players);

private:
    void addRound(Round&& round);
    int calculateRoundScore(unsigned int bid, unsigned int actual) const;
    bool shouldCountForStreaks(const Round& round) const;
};

} // namespace romanian_whist

#endif
