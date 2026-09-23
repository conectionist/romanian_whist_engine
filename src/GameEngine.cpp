#include <romanian_whist/GameEngine.h>

#include <romanian_whist/CardValidator.h>
#include <romanian_whist/IGameObserver.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace romanian_whist
{
GameEngine::GameEngine() : status(GameStatus::NotStarted)
{}

void GameEngine::start(GameSetup setup)
{
    // Everything below validates before anything is applied, so a rejected
    // start() leaves the engine exactly as it was: still NotStarted, with no
    // seats, no schedule and no deck. A client that catches the error can fix
    // its setup and call start() again.

    // A second start() would deal over a game in progress; on a finished or
    // stopped one it would replay a schedule whose scores are already counted.
    // Neither is recoverable, and both used to be three separate guards, each
    // complaining about whichever of addPlayer()/initializeScoreboard()/
    // initializeDeck() happened to run first.
    if(status != GameStatus::NotStarted)
        throw std::logic_error("GameEngine::start: the game has already been started");

    if(setup.seats.size() < 2 || setup.seats.size() > 6)
        throw std::invalid_argument("GameEngine::start: a game needs between 2 and 6 seats");

    for(const SeatSetup& seat : setup.seats)
    {
        if(seat.name.empty())
            throw std::invalid_argument("GameEngine::start: a seat's name cannot be empty");

        if(seat.moveProvider == nullptr)
            throw std::invalid_argument("GameEngine::start: seat '" + seat.name +
                                        "' has no move provider");
    }

    // Bets and tricks are keyed by Seat, so a shared name no longer corrupts
    // scoring. It is still rejected because a name is how a client labels a seat
    // on screen and how a player identifies their own: two "Ana" rows that
    // cannot be told apart is a bad game, just no longer a wrong one.
    //
    // Compared byte for byte - see GameSetup::seats for why there is no case
    // folding here. Quadratic over at most six names.
    for(std::size_t i = 0 ; i < setup.seats.size() ; i++)
        for(std::size_t j = i + 1 ; j < setup.seats.size() ; j++)
        {
            if(setup.seats[i].name == setup.seats[j].name)
                throw std::invalid_argument("GameEngine::start: duplicate seat name: " +
                                            setup.seats[i].name);
        }

    generator.seed(setup.shuffleSeed.value_or(std::random_device{}()));

    for(SeatSetup& seat : setup.seats)
        addPlayer(seat.name, std::move(seat.moveProvider));

    // Order matters and is no longer anyone else's to remember: the schedule is
    // sized and its opener rotation laid out for the seats now in place, and the
    // deck is built for the same count.
    initializeScoreboard(setup.structure,
                         setup.endWithForeheadAndHidden,
                         setup.all1GamesAreForehead);
    initializeDeck();

    status = GameStatus::InProgress;

    // Last, and from here rather than from run(): onGameStarted() belongs to the
    // setup path, so a client that starts the game and then hands the engine to
    // a worker thread still gets the event where it registered its observers.
    // run() may also be called more than once, which would duplicate it.
    notifyGameStarted();
}

void GameEngine::addPlayer(const std::string &name, std::unique_ptr<IMoveProvider> moveProvider)
{
    players.addPlayer(name, std::move(moveProvider));
}

void GameEngine::initializeScoreboard(const GameStructure &structure, 
                                      bool endWithForeheadAndHidden, 
                                      bool all1GamesAreForehead)
{
    scoreboard.initialize(structure, endWithForeheadAndHidden, all1GamesAreForehead, players);
}

void GameEngine::initializeDeck()
{
    // Its own count, rather than one passed in that had to match: dealCards()
    // indexes the deck by players.size(), so that was always the number this
    // wanted and a second copy of it was only ever a way to disagree.
    const unsigned int playerCount = static_cast<unsigned int>(players.size());

    for(int s = 0 ; s < 4 ; s++)
        for(int r = 1 + (6 - static_cast<int>(playerCount)) * 2 ; r < 13 ; r++)
        {
            Card card(static_cast<Rank>(r), static_cast<Suit>(s));
            deck.addCard(std::move(card));
        }
}

GameStatus GameEngine::getStatus() const
{
    return status;
}

bool GameEngine::isInProgress() const
{
    return status == GameStatus::InProgress;
}

GamePhase GameEngine::getPhase() const
{
    return phase;
}

bool GameEngine::isSetUp() const
{
    // The same condition requireStarted() guards on, which is what makes this
    // an honest answer rather than a second opinion about it.
    return status != GameStatus::NotStarted;
}

void GameEngine::addObserver(IGameObserver* observer)
{
    if(observer == nullptr)
        throw std::invalid_argument("GameEngine::addObserver: observer must not be null");

    requireNotDispatching("addObserver");

    if(std::find(observers.begin(), observers.end(), observer) != observers.end())
        return;

    observers.push_back(observer);
}

void GameEngine::removeObserver(IGameObserver* observer)
{
    requireNotDispatching("removeObserver");

    observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
}

void GameEngine::run()
{
    requireNotDriving("run");
    requireInProgress();

    while(isInProgress())
        playRound();

    // No stop check of its own: playRound() reads the flag before it deals, so
    // the loop just sees isInProgress() go false and falls out.
    //
    // onGameOver() and onGameStopped() are fired by playRound() and
    // honourStopIfRequested(), at the point the transition actually happens -
    // so a client driving playRound() itself is told the same things in the
    // same order as one calling run().
}

void GameEngine::playRound()
{
    requireNotDriving("playRound");
    requireInProgress();

    // Raised for the whole round, so every provider call and every observer
    // callback below is made with the engine visibly busy.
    DrivingGuard guard(*this);

    // The round boundary. Read here rather than in run()'s loop, because
    // playRound() is a documented entry point in its own right: a client
    // driving it directly used to deal a whole extra round after requestStop()
    // and, with a human provider, ask three players to bid on a hand thrown
    // away a moment later.
    if(honourStopIfRequested())
        return;

    dealRound();
    notifyRoundStarted();

    // Bidding reads the stop flag between bids, so it can give up partway
    // through the table rather than collecting bids for a round that is being
    // abandoned. It has already fired onGameStopped() by the time it says so.
    if(runBidding())
        return;

    notifyBettingComplete();

    const unsigned int trickCount = getCurrentRoundTrickCount();

    for(unsigned int trickNumber = 1 ; trickNumber <= trickCount ; trickNumber++)
    {
        // The boundary a stop lands on. Checked here rather than mid-turn, so a
        // trick already under way always finishes.
        if(honourStopIfRequested())
            return;

        Round& round = scoreboard.getCurrentRound();

        currentTrickNumber = trickNumber;
        round.resetCurrentTrick();
        notifyTrickStarted(trickNumber, round.getTrickLeaderSeat());

        playTrick();

        const Seat winner = determineTrickWinner(round.getCurrentTrick());

        // Filed before observers are told, so getTricksWon(winner) already
        // counts this trick inside onTrickWon() - and deliberately not cleared,
        // so getCurrentTrick() is still the finished trick there. The next
        // resetCurrentTrick() above is what clears it.
        round.finishCurrentTrick(winner);
        round.setTrickLeaderSeat(winner);

        activeSeat.reset();
        notifyTrickWon(winner, trickNumber);
    }

    // The last trick has no trick boundary after it, so the flag gets one last
    // read here. Without this a stop requested during the final trick scores and
    // commits the very round it was meant to abandon - and on the final round
    // ends the game Finished with onGameOver(), so onGameStopped() never fires
    // at all.
    if(honourStopIfRequested())
        return;

    // Scored but not committed, so an observer can show what the round was
    // worth alongside the total it is about to fold into.
    calculateScores();
    notifyRoundScored();

    commitRoundScores();
    completeCurrentRound();
    notifyRoundComplete();

    // No stop check between here and the end of the round: the flag is read at
    // boundaries, and past this point the round has none left. On any round but
    // the last, a stop raised inside onRoundScored()/onRoundComplete() is
    // caught by the check at the top of the next playRound() - the round it was
    // raised in is already scored and committed either way. On the last round
    // there is no next deal, completeCurrentRound() has already moved the game
    // to Finished by the time this line runs, so a stop raised in either of
    // those two callbacks is a no-op: it names no round to abandon and nothing
    // will read the flag again. Note the game was still InProgress inside
    // onRoundScored() - what settles it is the missing read, not the status at
    // the moment the stop was asked for. onGameOver() is what fires, and
    // stopRequested stays raised with nothing left to answer it.
    if(status == GameStatus::Finished)
        notifyGameOver();
}

void GameEngine::requestStop()
{
    stopRequested.store(true);
}

void GameEngine::dealRound()
{
    shuffleDeck();
    dealCards();

    // A fresh round starts with an empty table, not with the previous round's
    // last trick still standing in it.
    scoreboard.getCurrentRound().resetCurrentTrick();

    currentTrickNumber = 0;
    activeSeat.reset();
    phase = GamePhase::Betting;
}

bool GameEngine::runBidding()
{
    Seat seat = scoreboard.getCurrentRound().getRoundLeaderSeat();

    for(unsigned int i = 0 ; i < players.size() ; i++)
    {
        // The bid boundary, and the same harm the round boundary exists to
        // avoid one step earlier: without it a stop raised in onRoundStarted(),
        // or in an earlier seat's onBetRequested()/onBetPlaced(), still walked
        // the rest of the table asking for bids on a hand discarded unscored a
        // moment later - with a human provider, three needless prompts.
        if(honourStopIfRequested())
            return true;

        activeSeat = seat;
        notifyBetRequested(seat);

        Player& player = players.at(seat.index);

        // Asked fresh each time round: getForbiddenBet() only names a value
        // once everyone but the last player has bid.
        const unsigned int bet = player.getBet(getCurrentTrumpCard(), i == 0, getForbiddenBet(),
                                               getCurrentRoundType());

        // The engine is the one asking now, so it is the one that has to judge
        // the answer. Legality used to live entirely in the providers, which is
        // defensible only while every provider ships in this repo - a
        // WebMoveProvider is a thin shim over an untrusted browser.
        if(!isBetLegal(bet))
            throw std::logic_error(player.getName() + " bid " + std::to_string(bet)
                                   + ", which this round does not allow");

        placeBet(seat, bet);
        notifyBetPlaced(seat, bet);

        seat = getNextSeat(seat);
    }

    // The last bid has no boundary inside the loop after it, so the flag gets
    // one last read here - and gets it before the phase moves on, which is what
    // makes "a stop honoured anywhere in bidding leaves getPhase() saying
    // Betting" true rather than nearly true.
    if(honourStopIfRequested())
        return true;

    activeSeat.reset();
    phase = GamePhase::Playing;

    return false;
}

void GameEngine::playTrick()
{
    Round& round = scoreboard.getCurrentRound();
    const CardValidator validator;

    Seat seat = round.getTrickLeaderSeat();

    for(unsigned int i = 0 ; i < players.size() ; i++)
    {
        activeSeat = seat;
        notifyCardRequested(seat);

        Player& player = players.at(seat.index);
        const std::optional<Card> trump = getCurrentTrumpCard();

        const Trick& trick = round.getCurrentTrick();
        const std::optional<Suit> leadSuit =
            trick.hasLeadSuit() ? std::optional<Suit>(trick.getLeadSuit()) : std::nullopt;

        // Worked out before the provider is asked, because Player::playCard
        // erases the chosen card from the hand as part of the same call - after
        // it returns there is no longer a hand to judge the choice against.
        // A copy of the cards, so it stays meaningful across that erase.
        const std::vector<Card> legalCards = validator.getLegalCards(player.getHand(), trump, leadSuit);

        // The trick so far, plus what this player owes on their bid, so a
        // strategy can tell whether a card would actually win and whether it
        // wants it to.
        const std::optional<Card> playedCard = player.playCard(trump,
                                                               leadSuit,
                                                               trick.cardsInPlayOrder(),
                                                               round.getBet(seat).value_or(0),
                                                               round.getTricksWon(seat));

        // Contractually possible: a move provider returns empty when it has no
        // legal play. It should not be reachable, since a player holding cards
        // always has at least one legal one - so say so loudly rather than
        // playing on regardless.
        if(!playedCard)
            throw std::runtime_error(player.getName() + " had no legal card to play.");

        // Only the follow-suit half is live now. The other half - a card the
        // player does not hold - stopped being expressible when the provider
        // boundary became an index: Player::playCard range-checks it and reads
        // the card out of the hand, so whatever comes back was in there.
        if(std::find(legalCards.begin(), legalCards.end(), *playedCard) == legalCards.end())
            throw std::logic_error(player.getName() + " played a card that is not legal in this trick");

        round.addCardToCurrentTrick(seat, *playedCard);
        notifyCardPlayed(seat, *playedCard);

        seat = getNextSeat(seat);
    }
}

bool GameEngine::honourStopIfRequested()
{
    if(!stopRequested.load())
        return false;

    // Idempotent: run() and playRound() both check, and a stop must be reported
    // once however many boundaries it crosses.
    if(status == GameStatus::Stopped)
        return true;

    // The phase is deliberately left where it was, whatever it was: getStatus()
    // is what says the game is over, and getPhase() is left saying where the
    // game had got to. See GamePhase for which phases a stop can leave behind.
    status = GameStatus::Stopped;
    activeSeat.reset();

    notifyGameStopped();

    return true;
}

void GameEngine::requireInProgress() const
{
    if(status != GameStatus::InProgress)
        throw std::logic_error("GameEngine: the game is not in progress - it has "
                               "not been started, or it has already finished or "
                               "been stopped");
}

void GameEngine::shuffleDeck()
{
    deck.shuffle(generator);
}

void GameEngine::dealCards()
{
    // The deck and the schedule this reads are both built by start(), which is
    // the only way to reach a game that can be dealt at all - so the two guards
    // that used to stand here have no way left to fire.
    requireStarted();

    clearAllPlayerHands();

    unsigned int gameCount = scoreboard.getCurrentRound().getTrickCount();
    unsigned int index = 0;

    for(unsigned int i = 0 ; i < gameCount ; i++)
    {
        for(unsigned int j = 0 ; j < players.size() ; j++)
        {
            index = gameCount * j + i;
            players[j].addCardToHand(deck[index]);
        }
    }

    if(gameCount < 8)
    {
        scoreboard.getCurrentRound().setTrumpCard(deck[index + 1]);
    }
}

std::optional<Card> GameEngine::getCurrentTrumpCard() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getTrumpCard();
}

Seat GameEngine::getTrickLeaderSeat() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getTrickLeaderSeat();
}

Seat GameEngine::getRoundLeaderSeat() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getRoundLeaderSeat();
}

unsigned int GameEngine::getBiddingOrder(Seat seat) const
{
    requireStarted();

    // The cast is explicit because MSVC is right to ask: size() is a size_t.
    // start() accepts 2 to 6 seats and nothing else, so the value always fits -
    // and seats are unsigned int throughout the public API, which is the type
    // this has to end up as anyway.
    const auto playerCount = static_cast<unsigned int>(players.size());

    if(seat.index >= playerCount)
        throw std::out_of_range("GameEngine::getBiddingOrder: seat out of range");

    const unsigned int roundLeader = getRoundLeaderSeat().index;

    return ((seat.index + playerCount - roundLeader) % playerCount) + 1;
}

Seat GameEngine::getNextSeat(Seat seat) const
{
    return players.nextSeat(seat);
}

unsigned int GameEngine::getPlayerCount() const
{
    return static_cast<unsigned int>(players.size());
}

std::optional<unsigned int> GameEngine::getForbiddenBet() const
{
    requireStarted();

    const Round& round = scoreboard.getCurrentRound();

    unsigned int betsPlaced = 0;
    unsigned int total = 0;

    for(unsigned int i = 0 ; i < players.size() ; i++)
    {
        if(const std::optional<unsigned int> bet = round.getBet(Seat{i}))
        {
            betsPlaced++;
            total += *bet;
        }
    }

    if(betsPlaced + 1 != players.size())
        return std::nullopt;

    const unsigned int trickCount = round.getTrickCount();

    if(total > trickCount)
        return std::nullopt;

    return trickCount - total;
}

bool GameEngine::isBetLegal(unsigned int bet) const
{
    requireStarted();

    if(bet > getCurrentRoundTrickCount())
        return false;

    const std::optional<unsigned int> forbidden = getForbiddenBet();

    return !forbidden || bet != *forbidden;
}

void GameEngine::placeBet(Seat seat, unsigned int bet)
{
    requireStarted();

    scoreboard.getCurrentRound().setBet(seat, bet);
}

void GameEngine::clearAllPlayerHands()
{
    for(auto& player : players)
        player.clearHand();
}

unsigned int GameEngine::getCurrentRoundTrickCount() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getTrickCount();
}

Seat GameEngine::determineTrickWinner(const Trick& trick) const
{
    const std::vector<PlayedCard>& playedCards = trick.getPlayedCards();

    if(playedCards.empty())
        throw std::logic_error("determineTrickWinner() needs at least one played card");

    std::size_t bestIndex = 0;

    for(std::size_t i = 1 ; i < playedCards.size() ; i++)
    {
        if(cardBeats(playedCards[i].card, playedCards[bestIndex].card, trick.getLeadSuit()))
            bestIndex = i;
    }

    // Read off the winning entry rather than counting seats round from the
    // leader: the trick records who played each card, so the position a card
    // was played in never has to be translated back into a seat.
    return playedCards[bestIndex].seat;
}

void GameEngine::completeCurrentRound()
{
    requireStarted();

    if(scoreboard.getCurrentRoundIndex() + 1 >= scoreboard.getRoundCount())
    {
        status = GameStatus::Finished;
        phase = GamePhase::GameOver;
    }
    else
    {
        // The index has moved, so from here getCurrentRoundIndex() names the
        // NEXT round - which is why onRoundScored(), not onRoundComplete(), is
        // the hook a client draws a round's result from. The phase goes back to
        // Betting at that round's deal.
        scoreboard.incrementCurrentRound();

        // The trick number has to move with it. It counts tricks within the
        // current round, so leaving the finished round's last trick number
        // standing over the new index reports a trick the round it now names
        // has not played - and, since the schedule's rounds differ in length,
        // a number that round may not even reach: "trick 8 of 7" at
        // onRoundComplete(), and at onGameStopped() if the stop lands here.
        // dealRound() clears it too, but that is a round later than the index.
        currentTrickNumber = 0;
    }
}

void GameEngine::calculateScores()
{
    requireStarted();

    // Scoreboard::calculateScores accumulates into currentRoundScore and steps
    // the streak counters, neither with a guard - so a second call doubles the
    // round's points AND double-increments the streaks, walking a player on
    // their fifth consecutive hit straight from 4 to 6 so the == 5 bonus never
    // fires at all. That missing +-10 reads as a scoring-rule bug rather than
    // as a double call, and no assertion about round scores would see it. So
    // gate the whole function on the phase, not the arithmetic.
    //
    // Phase 2b of ENGINE_V4_PLAN.md tightens this to `phase != Playing` once
    // the duplicated test loop, which never sets a phase at all, is gone.
    if(phase == GamePhase::RoundScored)
        throw std::logic_error("GameEngine::calculateScores: this round has already been scored");

    scoreboard.calculateScores(players);

    if(phase == GamePhase::Playing)
        phase = GamePhase::RoundScored;
}

void GameEngine::commitRoundScores()
{
    scoreboard.commitRoundScores(players);
}

std::vector<Standing> GameEngine::getStandings() const
{
    requireStarted();

    std::vector<Standing> standings;
    standings.reserve(players.size());

    for(unsigned int i = 0 ; i < players.size() ; i++)
        standings.push_back(Standing{ Seat{i}, players.at(i).getName(), players.at(i).getTotalScore(), 0u });

    // Stable, so seats level on points come out in seat order rather than in
    // whatever order the sort happened to leave them. std::sort would let the
    // same tie in the same game render differently between two runs, which is a
    // difference no test could reproduce and no reader could explain.
    std::stable_sort(standings.begin(), standings.end(),
                     [](const Standing& left, const Standing& right)
                     {
                         return left.score > right.score;
                     });

    // Competition ranking: a row ties with the one above it or takes the place
    // its own index names, which is what skips the places a tie consumed. The
    // running counter that suggests itself instead - place++ per row - is the
    // dense version, and it hands the seat below a two-way tie for 1st a 2nd
    // place that nobody came second in.
    for(unsigned int i = 0 ; i < standings.size() ; i++)
        standings.at(i).place = (i > 0 && standings.at(i).score == standings.at(i - 1).score)
                                    ? standings.at(i - 1).place
                                    : i + 1;

    return standings;
}

std::vector<Standing> GameEngine::getWinners() const
{
    // Built from getStandings() rather than from a scan of its own: one
    // definition of the top score, so the winners can never disagree with the
    // rows the same client is about to render.
    std::vector<Standing> standings = getStandings();

    const auto firstLoser = std::find_if(standings.begin(), standings.end(),
                                         [](const Standing& standing)
                                         {
                                             return standing.place != 1;
                                         });

    standings.erase(firstLoser, standings.end());

    return standings;
}

int GameEngine::getRoundScore(Seat seat) const
{
    requireStarted();

    return players.at(seat.index).getCurrentRoundScore();
}

int GameEngine::getTotalScore(Seat seat) const
{
    requireStarted();

    return players.at(seat.index).getTotalScore();
}

const PlayerList &GameEngine::getPlayers() const
{
    return players;
}


const Round &GameEngine::getCurrentRound() const
{
    requireStarted();

    return scoreboard.getCurrentRound();
}

const Round &GameEngine::getRound(unsigned int index) const
{
    requireStarted();

    // Scoreboard::getRound() bounds-checks too, so this is not what keeps the
    // read in range. It is here to say how far the schedule actually goes,
    // which is what a caller who got the index wrong needs to know.
    if(index >= scoreboard.getRoundCount())
        throw std::out_of_range("GameEngine::getRound: round " + std::to_string(index)
                                + " is past the end of a " + std::to_string(scoreboard.getRoundCount())
                                + "-round schedule");

    return scoreboard.getRound(index);
}

std::optional<Seat> GameEngine::getActiveSeat() const
{
    return activeSeat;
}

unsigned int GameEngine::getCurrentTrickNumber() const
{
    requireStarted();

    return currentTrickNumber;
}

const Trick &GameEngine::getCurrentTrick() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getCurrentTrick();
}

std::optional<Seat> GameEngine::getCurrentTrickLeader() const
{
    requireStarted();

    const Trick& trick = scoreboard.getCurrentRound().getCurrentTrick();

    if(trick.getPlayedCards().empty())
        return std::nullopt;

    // The very same call that will declare the winner once the trick is full -
    // it ranks whatever has been played so far and reads the seat off the
    // winning entry. Reimplementing the ranking here is how a "currently
    // winning" highlight ends up disagreeing with the trick's actual winner.
    return determineTrickWinner(trick);
}

std::optional<unsigned int> GameEngine::getBet(Seat seat) const
{
    requireStarted();

    return scoreboard.getCurrentRound().getBet(seat);
}

unsigned int GameEngine::getTricksWon(Seat seat) const
{
    requireStarted();

    return scoreboard.getCurrentRound().getTricksWon(seat);
}

void GameEngine::requireStarted() const
{
    if(status == GameStatus::NotStarted)
        throw std::logic_error("GameEngine: the game has not been set up yet - "
                               "there is no current round to read");
}

const Deck &GameEngine::getDeck() const
{
    return deck;
}

unsigned int GameEngine::getCurrentRoundIndex() const
{
    requireStarted();

    return scoreboard.getCurrentRoundIndex();
}

unsigned int GameEngine::getRoundCount() const
{
    return scoreboard.getRoundCount();
}

RoundType GameEngine::getCurrentRoundType() const
{
    requireStarted();

    return scoreboard.getCurrentRound().getRoundType();
}

bool GameEngine::canSeeHand(Seat viewer, Seat holder) const
{
    requireStarted();

    // Checked here rather than left to yield a confident answer about a seat
    // that does not exist, which is how every other seat-taking accessor on
    // this class behaves.
    const auto playerCount = static_cast<unsigned int>(players.size());

    if(viewer.index >= playerCount || holder.index >= playerCount)
        throw std::out_of_range("GameEngine::canSeeHand: seat out of range");

    switch(getCurrentRoundType())
    {
        case RoundType::Forehead: return viewer != holder;
        case RoundType::Hidden:   return false;
        case RoundType::Normal:   break;
    }

    return viewer == holder;
}

bool GameEngine::cardBeats(const Card& candidate, const Card& currentBest, Suit leadSuit) const
{
    // The ranking itself lives on CardValidator, so that a strategy weighing up
    // "would this card win?" reasons with the very rule that will later declare
    // the winner, and the two can never drift apart.
    return CardValidator::beats(candidate, currentBest, leadSuit, getCurrentTrumpCard());
}

GameEngine::DrivingGuard::DrivingGuard(GameEngine& owner) : engine(owner)
{
    engine.driving = true;
}

GameEngine::DrivingGuard::~DrivingGuard()
{
    // Cleared rather than restored, unlike DispatchGuard: rounds cannot nest,
    // which is the whole point of the flag, so there is never an outer round
    // whose raised flag this would have to hand back.
    engine.driving = false;
}

void GameEngine::requireNotDriving(const char* caller) const
{
    if(driving)
        throw std::logic_error(std::string("GameEngine::") + caller + ": the game "
                               "cannot be driven from inside a move provider or an "
                               "observer callback - a round is already in flight, and "
                               "a second one would deal over its hands and bets. Call "
                               "requestStop() instead");
}

// Dispatch. Each of these iterates `observers` directly rather than through a
// shared template, so that a stack trace names the callback that threw.
// Registering or removing an observer from inside one of these invalidates the
// iteration, which is why addObserver()/removeObserver() forbid it - and why
// each one raises the flag those two check.

GameEngine::DispatchGuard::DispatchGuard(GameEngine& owner)
    : engine(owner), wasDispatching(owner.dispatching)
{
    engine.dispatching = true;
}

GameEngine::DispatchGuard::~DispatchGuard()
{
    // Restored rather than cleared, because dispatches nest: an observer that
    // calls back into the engine from onGameStarted() - the one callback the
    // engine makes with no round in flight, so the one place run()/playRound()
    // is still allowed - walks the list again inside the outer walk, and
    // clearing here would leave the outer one unguarded for the rest of its
    // iteration: the removeObserver() it exists to reject would be accepted,
    // and the range-for would then read past the vector it is halfway through.
    engine.dispatching = wasDispatching;
}

void GameEngine::requireNotDispatching(const char* caller) const
{
    if(dispatching)
        throw std::logic_error(std::string("GameEngine::") + caller + ": an observer "
                               "list cannot be changed from inside a callback - the "
                               "engine is iterating it. Set a flag and let the client "
                               "add or remove between rounds");
}

void GameEngine::notifyGameStarted()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onGameStarted(*this);
}

void GameEngine::notifyRoundStarted()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onRoundStarted(*this);
}

void GameEngine::notifyBetRequested(Seat seat)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onBetRequested(*this, seat);
}

void GameEngine::notifyBetPlaced(Seat seat, unsigned int bet)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onBetPlaced(*this, seat, bet);
}

void GameEngine::notifyBettingComplete()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onBettingComplete(*this);
}

void GameEngine::notifyTrickStarted(unsigned int trickNumber, Seat leader)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onTrickStarted(*this, trickNumber, leader);
}

void GameEngine::notifyCardRequested(Seat seat)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onCardRequested(*this, seat);
}

void GameEngine::notifyCardPlayed(Seat seat, const Card& card)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onCardPlayed(*this, seat, card);
}

void GameEngine::notifyTrickWon(Seat winner, unsigned int trickNumber)
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onTrickWon(*this, winner, trickNumber);
}

void GameEngine::notifyRoundScored()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onRoundScored(*this);
}

void GameEngine::notifyRoundComplete()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onRoundComplete(*this);
}

void GameEngine::notifyGameOver()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onGameOver(*this);
}

void GameEngine::notifyGameStopped()
{
    DispatchGuard guard(*this);

    for(IGameObserver* observer : observers)
        observer->onGameStopped(*this);
}

} // namespace romanian_whist
