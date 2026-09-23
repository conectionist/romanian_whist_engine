#include <romanian_whist/Scoreboard.h>

#include <utility>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace romanian_whist
{
Scoreboard::Scoreboard() : currentRound(0)
{
}

void Scoreboard::initialize(const GameStructure &structure, 
                            bool endWithForeheadAndHidden, 
                            bool all1GamesAreForehead, 
                            PlayerList &players)
{
    Seat opener{0};

    std::vector<unsigned int> gameNumbers;

    if(structure == GameStructure::S_181)
    {
        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(1);

        for(unsigned int i = 2 ; i <= 7 ; i++)
            gameNumbers.push_back(i);

        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(8);

        for(unsigned int i = 7 ; i >= 2 ; i--)
            gameNumbers.push_back(i);

        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(1);
    }
    else    // 8-1-8
    {
        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(8);

        for(unsigned int i = 7 ; i >= 2 ; i--)
            gameNumbers.push_back(i);

        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(1);

        for(unsigned int i = 2 ; i <= 7 ; i++)
            gameNumbers.push_back(i);

        for(unsigned int i = 0 ; i < players.size() ; i++)
            gameNumbers.push_back(8);
    }

    const unsigned int seatCount = static_cast<unsigned int>(players.size());

    for(int i : gameNumbers)
    {
        Round r(i, opener, seatCount);

        if(i == 1 && all1GamesAreForehead)
            r.setRoundType(RoundType::Forehead);

        addRound(std::move(r));

        opener = players.nextSeat(opener);
    }

    if(endWithForeheadAndHidden)
    {
        std::vector<RoundType> roundTypes = { RoundType::Forehead, RoundType::Hidden };

        for(auto type : roundTypes)
        {
            Round r(1, opener, seatCount, type);

            addRound(std::move(r));

            opener = players.nextSeat(opener);
        }
    }
}

void Scoreboard::addRound(Round &&round)
{
    rounds.push_back(std::move(round));
}

void Scoreboard::incrementCurrentRound()
{
    currentRound++;
}

Round &Scoreboard::getCurrentRound()
{
    return rounds[currentRound];
}

const Round &Scoreboard::getCurrentRound() const
{
    return rounds[currentRound];
}

const Round &Scoreboard::getRound(unsigned int index) const
{
    // The index comes from a caller rather than from our own state, so the
    // bound is ours to enforce - .at() rather than [], even though
    // GameEngine::getRound() checks first and says something more useful.
    return rounds.at(index);
}

unsigned int Scoreboard::getRoundCount() const
{
    // A schedule is at most a few dozen rounds, so the narrowing is safe; it is
    // spelled out because size() is a size_t and MSVC says so at /W4.
    return static_cast<unsigned int>(rounds.size());
}

unsigned int Scoreboard::getCurrentRoundIndex() const
{
    return currentRound;
}

void Scoreboard::calculateScores(PlayerList& players)
{
    Round& round = getCurrentRound();

    // Collect every bid before crediting anybody. Scoring only ever runs once
    // betting is complete, so a seat with no bid means the loop skipped a
    // bidder. Scoring that as a bid of zero would be a plausible-looking number
    // covering a real bug, so say so instead - and throw rather than assert,
    // which compiles out of a release build.
    //
    // Throwing from the scoring loop below would leave the round half scored,
    // with the seats before the gap already credited and their streaks already
    // advanced, so a caller that caught and retried would credit them twice.
    // Gathering the bids first makes this all or nothing.
    std::vector<unsigned int> bids;
    bids.reserve(players.size());

    for(unsigned int i = 0 ; i < players.size() ; i++)
    {
        const std::optional<unsigned int> bid = round.getBet(Seat{i});

        if(!bid)
            throw std::logic_error("Scoreboard::calculateScores: seat has not bid");

        bids.push_back(*bid);
    }

    for(unsigned int i = 0 ; i < players.size() ; i++)
    {
        const Seat seat{i};
        Player& player = players.at(seat.index);

        const unsigned int bid = bids[i];
        const unsigned int actual = round.getTricksWon(seat);

        int roundScore = calculateRoundScore(bid, actual);
        player.addToScore(roundScore);
        
        // Update streak counters
        if(shouldCountForStreaks(round))
        {
            if(bid == actual)
            {
                player.incrementConsecutiveWins();
                player.resetConsecutiveLosses();
                
                // Check for 5 consecutive wins
                if(player.getConsecutiveWins() == 5)
                {
                    player.addToScore(10); // Bonus for 5 consecutive wins
                }
            }
            else
            {
                player.incrementConsecutiveLosses();
                player.resetConsecutiveWins();
                
                // Check for 5 consecutive losses
                if(player.getConsecutiveLosses() == 5)
                {
                    player.addToScore(-10); // Penalty for 5 consecutive losses
                }
            }
        }
    }
}

void Scoreboard::commitRoundScores(PlayerList& players)
{
    // Commit round scores to total scores
    for(auto& player : players)
    {
        player.resetCurrentRoundScore();
    }
}

int Scoreboard::calculateRoundScore(unsigned int bid, unsigned int actual) const
{
    if(bid == actual)
    {
        return 5 + static_cast<int>(bid);
    }
    else
    {
        return -static_cast<int>(std::abs(static_cast<int>(bid) - static_cast<int>(actual)));
    }
}

bool Scoreboard::shouldCountForStreaks(const Round& round) const
{
    // 1-card rounds don't count for streaks
    return round.getTrickCount() != 1;
}

} // namespace romanian_whist
