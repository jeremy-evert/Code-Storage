#pragma once
#include <SFML/Network.hpp>
#include <string>
#include <map>
#include <vector>

struct ClientPlayerState {
    int diceCount = 5;
    std::vector<int> revealedDice; // used for MYDICE (self) and REVEAL (others)
};

class Client {
public:
    bool connectToServer(const std::string& ip, unsigned short port, const std::string& username);
    bool requestRoll();
    bool sendBet(int count, int face);
    bool sendDoubt();
    bool sendNextRound();
    bool poll();

    // Public state for UI
    std::string myUsername = "Player";
    bool connected = false;
    bool gameStarted = false;   // R only allowed once
    std::string phase = "Lobby";
    std::string currentTurn;
    std::string currentBetter;
    int currentBetCount = 0;
    int currentBetFace = 0;
    std::string lastMessage;    // last top-level token

    std::map<std::string, ClientPlayerState> players;

private:
    sf::TcpSocket socket;
};
