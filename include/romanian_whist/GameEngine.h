#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <romanian_whist/PlayerList.h>
#include <romanian_whist/Scoreboard.h>
#include <romanian_whist/Deck.h>

// SeatSetup holds a unique_ptr<IMoveProvider> by value, so the type has to be
// complete here. It arrives transitively through PlayerList.h -> Player.h
// anyway; named directly so a future reshuffle of those headers cannot quietly
// take it away.
#include <romanian_whist/IMoveProvider.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace romanian_whist
{
enum class GameStatus
{
    NotStarted,
    InProgress,
    Finished,

    // requestStop() was honoured. A stopped game is not resumable.
    Stopped
};

// Where in a round the game is. Deliberately has no Stopped of its own:
// getStatus() is what says the game is over, and getPhase() is left saying
// where the game had got to when it stopped. Which phase that is depends on
// where the stop was raised - Playing for the usual case of a stop honoured at
// a trick boundary, with the round left unscored; Betting for one honoured at a
// bid boundary, before the round it was raised in ever reached a trick; but
// RoundScored for one raised in onRoundScored()/onRoundComplete() and honoured
// before the next deal (that round *was* scored), and NotStarted for one
// honoured before the first round was ever dealt.
//
// RoundScored spans onRoundScored and, on every round but the last,
// onRoundComplete: the final round has already moved the game to GameOver by
// the time onRoundComplete fires, so an observer that gates a round summary on
// getPhase() == RoundScored there silently skips the last round. An observer
// that needs to tell the two callbacks apart uses the callback it is in, not
// getPhase().
enum class GamePhase
{
    NotStarted,
    Betting,
    Playing,
    RoundScored,
    GameOver
};

// Forward-declared rather than included: the engine only ever holds pointers to
// observers, and IGameObserver.h names GameEngine in return.
class IGameObserver;

// One seat at the table, in seat order. The engine takes ownership of the move
// provider, which is why this - and GameSetup with it - is move-only.
struct SeatSetup
{
    std::string name;
    std::unique_ptr<IMoveProvider> moveProvider;
};

// Everything start() needs, validated as a whole before any of it is applied.
struct GameSetup
{
    // 2..6 seats, each with a non-empty name, and no two names equal byte for
    // byte. Names are compared exactly: no case folding (these are Romanian
    // names, and a byte-wise tolower over UTF-8 either does nothing to a
    // multibyte sequence or corrupts it) and no trimming (validate, do not
    // mutate - a caller that passes "John " and reads back "John" has been
    // surprised for nothing). A client is free to be stricter.
    std::vector<SeatSetup> seats;

    GameStructure structure = GameStructure::S_181;
    bool endWithForeheadAndHidden = true;
    bool all1GamesAreForehead = false;

    // Set for a reproducible deal. Unset seeds from std::random_device.
    //
    // This is the only thing the seed reaches: a move provider is free to use
    // its own randomness (RandomCardStrategy's default constructor, for
    // instance, still draws from std::random_device), so a fully reproducible
    // game additionally requires seeding every such provider.
    std::optional<std::uint32_t> shuffleSeed;
};

// A seat's place in the final standings. Carries the seat, so two players who
// share a name stay distinguishable - the engine rejects duplicate names, but a
// client that renders a shortened or decorated name can still produce two equal
// strings, and it needs to know which row is which.
struct Standing
{
    Seat seat;
    std::string name;
    int score;
    // Competition ranking, from 1: seats on equal scores share a place, and the
    // places they consume are skipped. Scores 50, 40, 40, 30 rank 1, 2, 2, 4 -
    // there is no third place in that game. Ranking here rather than in each
    // client is what stops one of them deciding a tie differently from the
    // next; see getWinners() for the first-place case, which is the one clients
    // ask for most and get wrong most.
    unsigned int place;
};

class GameEngine
{
private:
    PlayerList players;
    Scoreboard scoreboard;
    Deck deck;
    GameStatus status;

    // Seeded by start(), from GameSetup::shuffleSeed or std::random_device.
    std::mt19937 generator;

    // Non-owning. Registration order is dispatch order.
    std::vector<IGameObserver*> observers;

    // True while a notify* helper is walking `observers`. addObserver() and
    // removeObserver() consult it and throw, because mutating the vector under
    // that walk invalidates it - see their declarations below.
    bool dispatching = false;

    // Raises `dispatching` for the duration of one dispatch and lowers it on
    // the way out, including when a callback throws. That case leaves the
    // engine unusable anyway (see run()), but an unbalanced flag would turn a
    // client's attempt to detach its observers during teardown into a second,
    // more confusing exception.
    class DispatchGuard
    {
    private:
        GameEngine& engine;

        // Dispatches nest when an observer calls back into the engine, so the
        // flag is restored on the way out rather than cleared.
        bool wasDispatching;

    public:
        explicit DispatchGuard(GameEngine& owner);
        ~DispatchGuard();

        DispatchGuard(const DispatchGuard&) = delete;
        DispatchGuard& operator=(const DispatchGuard&) = delete;
    };

    // True while run()/playRound() is inside a round. Both consult it and
    // throw, because a re-entrant call re-deals the round already in flight:
    // hands are cleared and bets overwritten under the loop that is still
    // playing them - see their declarations below.
    bool driving = false;

    // Raises `driving` for the duration of one round and lowers it on the way
    // out, including when a provider or observer throws. Same shape, and the
    // same reasoning, as DispatchGuard above.
    class DrivingGuard
    {
    private:
        GameEngine& engine;

    public:
        explicit DrivingGuard(GameEngine& owner);
        ~DrivingGuard();

        DrivingGuard(const DrivingGuard&) = delete;
        DrivingGuard& operator=(const DrivingGuard&) = delete;
    };

    GamePhase phase = GamePhase::NotStarted;

    // Whose turn it is right now; disengaged between turns. Held here rather
    // than in a loop local for the same reason Round holds the in-flight trick.
    std::optional<Seat> activeSeat;

    // 1-based. 0 between the deal and the first trick.
    unsigned int currentTrickNumber = 0;

    // Read at each trick and round boundary. The only member another thread may
    // touch, and the reason GameEngine is neither copyable nor movable.
    std::atomic<bool> stopRequested{false};

public:
    GameEngine();

    // Seats the table, lays out the round schedule, builds the deck and starts
    // the game - every setup step, in the one order that works, so no client
    // has to remember it.
    //
    // GameSetup owns the move providers and is move-only, so every call site
    // reads start(std::move(setup)).
    //
    // Throws std::invalid_argument if the setup is not playable: fewer than 2 or
    // more than 6 seats, an empty name, or two names equal byte for byte (see
    // GameSetup::seats). Throws std::logic_error if the game has already been
    // started - a second start() would deal over a game in progress, and on a
    // finished one it would replay a schedule whose scores are already counted.
    //
    // The whole setup is validated before any of it is applied, so a rejected
    // start() leaves the engine untouched and still NotStarted, ready to be
    // started again. The caller's GameSetup is not: taking it by value moves
    // from it - emptying seats, destroying the move providers - before any
    // validation runs, so a retry has to build a fresh one.
    //
    // Firing onGameStarted() is the last thing it does, so register every
    // observer first. The engine is set up but not yet dealt at that point -
    // see IGameObserver.h for exactly what is readable there.
    void start(GameSetup setup);

    GameStatus getStatus() const;
    bool isInProgress() const;

    // Where in the round the game is. Callable at any time, including before
    // setup.
    //
    // It does NOT answer whether the round-scoped accessors are safe to call:
    // the phase is still NotStarted right through onGameStarted(), where every
    // one of them already answers. isSetUp() is that question.
    GamePhase getPhase() const;

    // Whether the game has been set up far enough for the round-scoped
    // accessors to answer - true once start() has laid out the schedule.
    //
    // This, getStatus(), getPhase(), isInProgress() and getPlayerCount() are
    // callable at any time; everything round-scoped throws std::logic_error
    // until this is true.
    //
    // It is the same question as getStatus() != NotStarted, now that start()
    // does both at once. It stays as a method of its own because it is the one
    // that says what a client actually wants to know, and because the guard it
    // describes is a precondition on reading state rather than a fact about the
    // game's progress - a stopped or finished game is still readable.
    bool isSetUp() const;

    // Non-owning. Observers must outlive the engine. Registering the same
    // observer twice is a no-op.
    //
    // Neither may be called from inside a callback: the engine iterates its
    // observer list while dispatching, so adding or removing mid-dispatch
    // invalidates that iteration. Both throw std::logic_error rather than
    // corrupt the walk in progress - a removal would otherwise skip the next
    // observer silently and dispatch the last one twice, from a slot past the
    // vector's own end. An observer that wants to detach sets a flag and lets
    // the client detach it between rounds.
    //
    // Unlike requestStop(), NEITHER IS THREAD-SAFE. The engine has no internal
    // locking anywhere, so calling these from a second thread while the game
    // thread is running races the same iteration - it is just harder to hit.
    void addObserver(IGameObserver* observer);
    void removeObserver(IGameObserver* observer);

    // ---- driving ----
    // Both block until the work is done, calling into move providers and
    // observers on the calling thread. Both throw std::logic_error unless the
    // status is InProgress, which is also what rejects a second run() on a
    // finished or stopped engine.
    //
    // If a provider or observer throws, the exception propagates out and the
    // engine is left mid-round and NOT resumable - destroy it. Nothing here is
    // exception-safe in the strong sense, and it does not need to be: the state
    // is already inconsistent and pretending otherwise is worse.
    //
    // Neither may be called while a round is in flight - that is, from a move
    // provider, or from any observer callback except onGameStarted(), which the
    // engine makes while it is still idle. Both throw std::logic_error rather
    // than re-enter, because a re-entrant round deals over the hands and bets
    // the outer one is midway through playing. A callback that wants the game
    // to end asks for that with requestStop().
    void run();          // playRound() until the schedule runs out or a stop lands
    void playRound();    // deal, bet, play every trick, score, advance

    // Ends cleanly at the next round, bid or trick boundary; status -> Stopped,
    // and observers get onGameStopped() rather than onGameOver(). Safe to call
    // from another thread; it only sets an atomic flag.
    //
    // playRound() reads the flag before it deals, so a client driving rounds
    // itself honours a stop at the same boundaries, in the same order, as one
    // calling run().
    //
    // The boundary after a round's final trick counts, so a stop never scores
    // the round it landed in - not even one it caught on the last trick, and
    // not even on the last round of the schedule.
    //
    // Bidding has boundaries of its own, so a stop raised in onRoundStarted()
    // or in one seat's onBetRequested()/onBetPlaced() does not walk the rest of
    // the table asking for bids on a hand that is about to be abandoned. A stop
    // honoured anywhere in bidding leaves getPhase() saying Betting.
    //
    // A stop requested after the last round has been scored - from
    // onRoundScored() or onRoundComplete() on the final round, or any time
    // after the game reaches Finished - is a no-op: there is no round left to
    // abandon and no boundary left to land on, so the game stays Finished and
    // onGameOver(), not onGameStopped(), is what fired. Note the game need not
    // have reached Finished at the moment such a stop is asked for - from
    // onRoundScored() it has not - only before anything reads the flag again.
    //
    // It cannot interrupt a move provider already parked waiting for a human:
    // the flag is only read between bids, tricks and rounds. Unparking that
    // provider is the client's job, and throwing from it is the supported way
    // out.
    void requestStop();

    // Empty in 8-card rounds, which have no trump. By value: the deck it was
    // dealt from is reshuffled at the top of every round, so a pointer would
    // stop meaning this card the moment the round ended.
    std::optional<Card> getCurrentTrumpCard() const;

    // Who leads the next trick: the round leader until the first trick is won,
    // then each trick's winner.
    Seat getTrickLeaderSeat() const;

    // Who opened this round, fixed for its lifetime. Bidding runs from here,
    // wrapping round the table - which is what getBiddingOrder() below answers.
    //
    // Read the two apart carefully: until the first trick is won they name the
    // same seat, and from then on they do not. Bidding, and anything measured
    // from where the round began, wants this one.
    Seat getRoundLeaderSeat() const;

    // Where `seat` sits in this round's bidding order: 1 for the round leader,
    // then round the table. Saves every client reimplementing the modular
    // arithmetic, and getting it wrong once per client.
    unsigned int getBiddingOrder(Seat seat) const;

    Seat getNextSeat(Seat seat) const;
    unsigned int getPlayerCount() const;
    // The bidding restriction: the final bidder may not make the round's bids
    // add up to exactly the trick count. getForbiddenBet() names the single bid
    // that would, and is empty for everyone but the final bidder - and empty
    // for them too when the bids already exceed the trick count, since no bid
    // can bring the total back down to it.
    //
    // These are for prompting: the engine validates every bid it is given and
    // throws on an illegal one, so a client asks first in order to re-prompt a
    // human, rather than to keep the game alive.
    std::optional<unsigned int> getForbiddenBet() const;
    bool isBetLegal(unsigned int bet) const;

    unsigned int getCurrentRoundTrickCount() const;

    // ---- scores ----
    // The final (or running) standings, best first, one row per seat and each
    // row carrying its Standing::place. Sorted stably, so seats on equal scores
    // stay in seat order rather than in whatever order the sort happened to
    // leave them - a tie that rendered differently between two runs of the same
    // game would be a bug no test could reproduce.
    //
    // Readable from the moment the game starts, like every other accessor here,
    // so a live scoreboard is the same call as a final one. What separates them
    // is getStatus(): only a Finished game's standings are final.
    std::vector<Standing> getStandings() const;

    // Every seat sharing the top score - the game's winners, in seat order.
    //
    // Romanian Whist has no tie-breaker, so a tie at the top is shared and this
    // returns more than one row; it is never empty for a started game. Prefer
    // it to reading getStandings().front(), which quietly names one winner of a
    // drawn game and is the way this gets written wrong.
    //
    // Like getStandings() it answers during a game too, where it means "who is
    // currently leading" - a client announcing a *winner* checks that
    // getStatus() is Finished first.
    std::vector<Standing> getWinners() const;

    // getRoundScore() is what the current round is worth so far.
    // getTotalScore() is the *committed* total and does not include it until
    // the round is complete: scoring a round writes getRoundScore(), and
    // committing it is what folds that into getTotalScore() and clears it.
    //
    // So inside onRoundScored() the two are "what this round was worth" and
    // "the total it has not yet been added to", and a client wanting the
    // projected total adds them. By onRoundComplete() the round score is 0 and
    // the total includes it. Render one without the other and the scoreboard
    // reads differently either side of the commit.
    int getRoundScore(Seat seat) const;
    int getTotalScore(Seat seat) const;

    // Live game state, for clients that render more than a score table. Through
    // these a UI can read each seat's hand, streaks and scores, and the current
    // round's bets, results, trump and type - without keeping a parallel copy of
    // the game and hoping the two stay in step.
    //
    // Both hand out const access only. A Round names players by Seat, which
    // carries no access, so a const Round really does seal off the players
    // behind it.
    const PlayerList& getPlayers() const;
    const Round& getCurrentRound() const;

    // Any round in the schedule, played or not - this is how a client renders
    // "the previous round". start() lays the schedule out in full, so an index
    // past getCurrentRoundIndex() names a round that exists but has not been
    // played: no bets, no tricks, no trump. Throws std::out_of_range past the
    // end of the schedule, and std::logic_error before the game has started.
    //
    // Until Phase 4 of ENGINE_V4_PLAN.md this could not be trusted: a finished
    // Round held its trump and its tricks as Card* into a deck that is
    // reshuffled every round, so from round two on it reported cards that were
    // never played in it. Cards are held by value now, so a round means what it
    // says for the life of the game.
    const Round& getRound(unsigned int index) const;

    // Read-only access to the deck, for inspecting composition and shuffle
    // order (tests) without a test-only friend declaration.
    const Deck& getDeck() const;

    unsigned int getCurrentRoundIndex() const;
    unsigned int getRoundCount() const;
    RoundType getCurrentRoundType() const;

    // May `viewer` see `holder`'s hand in the current round? Normal: only your
    // own. Forehead: everyone's but your own. Hidden: nobody's. This is not
    // enforcement - getPlayers() and Player::getHand() stay public because the
    // renderer needs them - it is the one place both clients ask, so they
    // cannot disagree about the rule.
    bool canSeeHand(Seat viewer, Seat holder) const;

    // ---- live round state ----
    // Whose turn it is; empty between turns. Engaged from just before a
    // provider is asked until just after its answer is recorded, so it is still
    // that seat inside onBetPlaced() and onCardPlayed() - which is what lets a
    // renderer keep the highlight on whoever just moved.
    std::optional<Seat> getActiveSeat() const;

    // 1-based; 0 after the deal and before the first trick.
    unsigned int getCurrentTrickNumber() const;

    // The trick in flight - see Round::getCurrentTrick() for exactly what it
    // holds when. Every entry carries the seat that played it, so a client
    // never has to reconstruct turn order arithmetically.
    const Trick& getCurrentTrick() const;

    // Who is winning the trick in flight, ranking whatever has been played so
    // far; empty before the first card. This is the engine's own ranking, so a
    // "currently winning" highlight can never disagree with the winner the
    // engine eventually declares.
    std::optional<Seat> getCurrentTrickLeader() const;

    // Empty until that seat has bid.
    std::optional<unsigned int> getBet(Seat seat) const;

    // Derived from the round's stored tricks, so it counts a trick from the
    // moment that trick is complete.
    unsigned int getTricksWon(Seat seat) const;

private:
    // ---- setup, once start() has validated it ----
    // These were four public methods a client called in an order that was
    // load-bearing and only partly enforced, each guard throwing about itself
    // rather than about starting a game. start() is that order now, and it is
    // the only caller, so the guards they carried are gone with the ways of
    // reaching them out of sequence.
    void addPlayer(const std::string& name, std::unique_ptr<IMoveProvider> moveProvider);

    // Lays out the round schedule. Its length and the opener rotation both
    // depend on the player count, so every seat must be in place first.
    void initializeScoreboard(const GameStructure& structure,
                              bool endWithForeheadAndHidden,
                              bool all1GamesAreForehead);

    // Builds the deck for the seats already added; it reads its own player
    // count rather than being told one that has to match.
    void initializeDeck();

    // ---- the loop's own primitives ----
    // Likewise the public API a client used to drive the game with. playRound()
    // is that order now, so they are its business alone.
    void shuffleDeck();

    // Deals the current round's hands (and its trump card, below 8 tricks) out
    // of the deck. Needs both the deck (the cards) and the schedule (the trick
    // count), which start() builds together - so requireStarted() is the whole
    // precondition, where this used to carry one guard for each.
    void dealCards();

    void placeBet(Seat seat, unsigned int bet);

    // Ranks whatever has been played so far, so this also answers "who is
    // winning?" partway through a trick - which is what getCurrentTrickLeader()
    // exposes. Throws std::logic_error on a trick with no cards in it at all,
    // which has no answer to give.
    Seat determineTrickWinner(const Trick& trick) const;

    void completeCurrentRound();
    void calculateScores();
    void commitRoundScores();

    // Every accessor whose answer depends on a current round routes through
    // Scoreboard::getCurrentRound(), which is `rounds[currentRound]` on a vector
    // that is empty until the scoreboard is laid out - so asking early is a read
    // of arbitrary memory, not a thrown error. One guard rather than a check per
    // accessor, so a future accessor cannot be added without one.
    //
    // Keyed on the status, which start() sets in the same breath as it lays the
    // schedule out - the two conditions became the same thing once setup stopped
    // being something a client could do half of.
    void requireStarted() const;

    // run() and playRound() need a live game. Also what stops a second run() on
    // an engine that has finished or been stopped.
    void requireInProgress() const;

    // Rejects a change to the observer list made from inside a callback.
    // `caller` names the method for the message.
    void requireNotDispatching(const char* caller) const;

    // Rejects run()/playRound() re-entered from a provider or an observer while
    // a round is already in flight. `caller` names the method for the message.
    void requireNotDriving(const char* caller) const;

    // The three stages of a round, kept separate so that each one's place in
    // the callback order is readable. All of them advance state that lives in
    // members - the phase, the active seat, the trick number, the round's
    // in-flight trick - rather than in locals of a nested loop, which is what
    // keeps a future non-blocking playStep() an additive change.
    void dealRound();

    // Returns true if a stop was honoured partway through, in which case the
    // round has no bets to play out and the caller must give up on it.
    bool runBidding();

    void playTrick();

    // Returns true if the game has been stopped and the caller should give up.
    // Fires onGameStopped() exactly once.
    bool honourStopIfRequested();

    void clearAllPlayerHands();
    bool cardBeats(const Card& candidate, const Card& currentBest, Suit leadSuit) const;

    // One helper per callback, so the loop over `observers` lives in one place
    // and a dispatch site is a single line.
    void notifyGameStarted();
    void notifyRoundStarted();
    void notifyBetRequested(Seat seat);
    void notifyBetPlaced(Seat seat, unsigned int bet);
    void notifyBettingComplete();
    void notifyTrickStarted(unsigned int trickNumber, Seat leader);
    void notifyCardRequested(Seat seat);
    void notifyCardPlayed(Seat seat, const Card& card);
    void notifyTrickWon(Seat winner, unsigned int trickNumber);
    void notifyRoundScored();
    void notifyRoundComplete();
    void notifyGameOver();
    void notifyGameStopped();
};

} // namespace romanian_whist

#endif
