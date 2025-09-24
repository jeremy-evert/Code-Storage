#include "Client.h"
#include <iostream>
#include <sstream>
#include <algorithm>

bool Client::connectToServer(const std::string& ip, unsigned short port, const std::string& username) {
    if (socket.connect(ip, port, sf::milliseconds(3000)) != sf::Socket::Done) {
        std::cerr << "Client: Failed to connect to " << ip << ":" << port << "\n";
        connected = false;
        return false;
    }
    socket.setBlocking(false);
    connected = true;
    myUsername = username;

    sf::Packet p; p << std::string("HELLO ") + username;
    socket.send(p);
    std::cout << "Client: connected, sent HELLO " << username << "\n";
    return true;
}

bool Client::requestRoll() {
    if (!connected || gameStarted) return false; // allow only once
    sf::Packet p; p << std::string("ROLL");
    bool ok = (socket.send(p) == sf::Socket::Done);
    if (ok) gameStarted = true;
    return ok;
}

bool Client::sendBet(int count, int face) {
    if (!connected) return false;
    std::ostringstream oss; oss << "BET " << count << ' ' << face;
    sf::Packet p; p << oss.str();
    return socket.send(p) == sf::Socket::Done;
}

bool Client::sendDoubt() {
    if (!connected) return false;
    sf::Packet p; p << std::string("DOUBT");
    return socket.send(p) == sf::Socket::Done;
}

bool Client::sendNextRound() {
    if (!connected) return false;
    sf::Packet p; p << std::string("NEXT");
    return socket.send(p) == sf::Socket::Done;
}

bool Client::poll() {
    if (!connected) return false;

    sf::Packet p;
    auto s = socket.receive(p);
    if (s == sf::Socket::NotReady) return false;
    if (s == sf::Socket::Disconnected) {
        std::cerr << "Client: disconnected\n";
        connected = false;
        return false;
    }
    if (s != sf::Socket::Done) return false;

    std::string line;
    if (!(p >> line)) return false;

    lastMessage = line;

    if (line.rfind("WELCOME ", 0) == 0) {
        return true;
    }

    if (line.rfind("PHASE ", 0) == 0) {
        phase = line.substr(6);
        if (phase == "BETTING") {
            // clear previous reveal
            for (auto& kv : players) kv.second.revealedDice.clear();
        }
        return true;
    }

    if (line.rfind("TURN ", 0) == 0) {
        currentTurn = line.substr(5);
        return true;
    }

    if (line.rfind("CURRENTBET ", 0) == 0) {
        std::istringstream iss(line.substr(11));
        std::string who; int count, face;
        if (iss >> who >> count >> face) {
            if (who == "None") {
                currentBetter.clear();
                currentBetCount = 0;
                currentBetFace = 0;
            }
            else {
                currentBetter = who;
                currentBetCount = count;
                currentBetFace = face;
            }
        }
        return true;
    }

    if (line.rfind("DICECOUNT ", 0) == 0) {
        std::istringstream iss(line.substr(10));
        std::string name; int n;
        if (iss >> name >> n) {
            players[name].diceCount = n;
        }
        return true;
    }

    if (line.rfind("REVEAL ", 0) == 0) {
        std::istringstream iss(line.substr(7));
        std::string name;
        if (iss >> name) {
            players[name].revealedDice.clear();
            int v;
            while (iss >> v) players[name].revealedDice.push_back(v);
        }
        return true;
    }

    if (line.rfind("MYDICE", 0) == 0) {
        std::istringstream iss(line.substr(6));
        players[myUsername].revealedDice.clear();
        int v;
        while (iss >> v) players[myUsername].revealedDice.push_back(v);
        std::cout << "Client: MYDICE -> ";
        for (int d : players[myUsername].revealedDice) std::cout << d << " ";
        std::cout << "\n";
        return true;
    }

    if (line.rfind("INFO ", 0) == 0) {
        std::cout << "Server info: " << line.substr(5) << "\n";
        return true;
    }

    std::cout << "Client: unknown line: " << line << "\n";
    return true;
}
