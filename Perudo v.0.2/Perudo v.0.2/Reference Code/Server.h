#pragma once
#include <SFML/Network.hpp>
#include <map>
#include <vector>
#include <memory>
#include <string>

class Server {
public:
    struct PlayerInfo {
        sf::TcpSocket* sock = nullptr;
        std::string name;
        int diceCount = 5;
        bool connected = false;
    };

    enum class Phase { Lobby, Betting, Reveal };

    bool start(unsigned short port);

private:
    // Networking
    sf::TcpListener listener;
    sf::SocketSelector selector;
    std::vector<std::unique_ptr<sf::TcpSocket>> clients;

    // Game state
    std::map<sf::TcpSocket*, PlayerInfo> playersBySock;
    std::map<std::string, std::vector<int>> roundDice; // name -> dice
    std::vector<sf::TcpSocket*> turnOrder;
    int turnIndex = 0;

    Phase phase = Phase::Lobby;

    // Current bet
    std::string currentBetter;
    int currentBetCount = 0;
    int currentBetFace = 0; // 1..6

    // Round starters
    std::string firstRoundStarter;  // who pressed R first
    std::string lastRoundLoser;     // who lost a die last round (starts next)

    // ---- Internals ----
    void handleNewConnection();
    bool handleClientMessage(sf::TcpSocket* client);

    void sendLine(sf::TcpSocket* client, const std::string& line);
    void broadcast(const std::string& line);

    std::string nameOf(sf::TcpSocket* s) const;
    PlayerInfo* getPlayerByName(const std::string& name);

    void setupTurnOrderIfNeeded();
    void startBettingIfPossible();
    void advanceTurn();

    void broadcastPlayerDiceCounts();
    void broadcastTurn();
    void broadcastCurrentBet();
    void broadcastRevealAll();

    void rollAllDice();
    void sendPrivateDiceToOwners(); // sends "MYDICE ..." to each owner

    // Rules / helpers
    bool isValidRaise(int newCount, int newFace) const;
    int  countMatching(const std::map<std::string, std::vector<int>>& allDice, int betFace) const;
    bool isPalificoRound() const; // true if the player whose turn it is has exactly 1 die

    void resolveDoubt(sf::TcpSocket* challenger);
    void beginNextRound();
};
