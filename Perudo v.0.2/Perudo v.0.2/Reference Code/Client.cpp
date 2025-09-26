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

    sf::Packet p;
    p << std::string("HELLO ") + username;
    if (socket.send(p) == sf::Socket::Done) {
        std::cout << "Client: connected, sent HELLO " << username << "\n";
    }
    else {
        std::cerr << "Client: FAILED to send HELLO\n";
    }
    return true;
}

bool Client::requestRoll() {
    if (!connected) { std::cerr << "Client: ROLL blocked (not connected)\n"; return false; }
    if (gameStarted) { std::cout << "Client: ROLL ignored (game already started)\n"; return false; }

    sf::Packet p; p << std::string("ROLL");
    auto st = socket.send(p);
    if (st == sf::Socket::Done) {
        gameStarted = true;          // prevent repeated R in this session
        // Mark that we are expecting dice even if phase is still Lobby
        // (server may send MYDICE before PHASE BETTING broadcast)
        // We'll honor that MYDICE once and animate.
        std::cout << "Client: sent ROLL (ok)\n";
        return true;
    }
    else {
        std::cerr << "Client: FAILED to send ROLL (status=" << (int)st << ")\n";
        return false;
    }
}

bool Client::sendBet(int count, int face) {
    if (!connected) { std::cerr << "Client: BET blocked (not connected)\n"; return false; }
    std::ostringstream oss; oss << "BET " << count << ' ' << face;
    sf::Packet p; p << oss.str();
    auto st = socket.send(p);
    if (st == sf::Socket::Done) {
        std::cout << "Client: sent " << oss.str() << " (ok)\n";
        return true;
    }
    else {
        std::cerr << "Client: FAILED to send " << oss.str() << " (status=" << (int)st << ")\n";
        return false;
    }
}

bool Client::sendDoubt() {
    if (!connected) { std::cerr << "Client: DOUBT blocked (not connected)\n"; return false; }
    sf::Packet p; p << std::string("DOUBT");
    auto st = socket.send(p);
    if (st == sf::Socket::Done) {
        std::cout << "Client: sent DOUBT (ok)\n";
        return true;
    }
    else {
        std::cerr << "Client: FAILED to send DOUBT (status=" << (int)st << ")\n";
        return false;
    }
}

bool Client::sendNextRound() {
    if (!connected) { std::cerr << "Client: NEXT blocked (not connected)\n"; return false; }
    sf::Packet p; p << std::string("NEXT");
    auto st = socket.send(p);
    if (st == sf::Socket::Done) {
        std::cout << "Client: sent NEXT (ok)\n";
        return true;
    }
    else {
        std::cerr << "Client: FAILED to send NEXT (status=" << (int)st << ")\n";
        return false;
    }
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

    // Keep only the first token in lastMessage (useful for UI/animation decisions)
    {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        lastMessage = token; // e.g., "PHASE", "TURN", "MYDICE", ...
    }

    // ---- Protocol ----

    if (line.rfind("WELCOME ", 0) == 0) {
        return true;
    }

    if (line.rfind("PHASE ", 0) == 0) {
        phase = line.substr(6);
        if (phase == "BETTING") {
            // Clear any previous reveals at the start of betting
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
        // Accept MYDICE in BETTING (normal), or in LOBBY only if we just started (ROLL sent)
        // because some servers might send MYDICE right before PHASE BETTING broadcast.
        if (phase != "BETTING" && phase != "Lobby") {
            std::cout << "Client: ignoring MYDICE (phase=" << phase << ")\n";
            return true;
        }

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
