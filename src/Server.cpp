#include "Server.h"
#include <iostream>
#include <random>
#include <sstream>
#include <algorithm>
#include <cmath>

bool Server::start(unsigned short port) {
    if (listener.listen(port) != sf::Socket::Done) {
        std::cerr << "Server: Failed to bind port " << port << "\n";
        return false;
    }
    listener.setBlocking(false);
    selector.add(listener);
    phase = Phase::Lobby;
    std::cout << "Server: started on port " << port << ". Waiting for players...\n";

    while (true) {
        selector.wait(sf::milliseconds(30));

        if (selector.isReady(listener)) handleNewConnection();

        for (auto it = clients.begin(); it != clients.end();) {
            sf::TcpSocket* sock = it->get();
            if (selector.isReady(*sock)) {
                if (!handleClientMessage(sock)) {
                    selector.remove(*sock);
                    std::cout << "Server: disconnected " << nameOf(sock) << "\n";
                    playersBySock.erase(sock);
                    turnOrder.erase(std::remove(turnOrder.begin(), turnOrder.end(), sock), turnOrder.end());
                    it = clients.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
}

void Server::handleNewConnection() {
    auto sock = std::make_unique<sf::TcpSocket>();
    if (listener.accept(*sock) == sf::Socket::Done) {
        sock->setBlocking(false);
        selector.add(*sock);
        std::cout << "Server: new client connected\n";
        clients.push_back(std::move(sock));
    }
}

bool Server::handleClientMessage(sf::TcpSocket* client) {
    sf::Packet p;
    auto s = client->receive(p);
    if (s == sf::Socket::Disconnected) return false;
    if (s != sf::Socket::Done) return true;

    std::string line;
    if (!(p >> line)) return true;

    // ---- Protocol ----
    if (line.rfind("HELLO ", 0) == 0) {
        std::string name = line.substr(6);
        PlayerInfo info{ client, name, 5, true };
        playersBySock[client] = info;
        std::cout << "Server: HELLO from " << name << "\n";
        sendLine(client, "WELCOME " + name);
        broadcastPlayerDiceCounts();
        return true;
    }

    if (line == "ROLL") {
        // Only meaningful in Lobby/after reveal (first round is started by first R)
        if (turnOrder.empty())
            firstRoundStarter = nameOf(client);

        setupTurnOrderIfNeeded();
        rollAllDice();
        sendPrivateDiceToOwners(); // give each client their own dice
        broadcast("PHASE BETTING");
        phase = Phase::Betting;
        currentBetter.clear();
        currentBetCount = 0;
        currentBetFace = 0;
        startBettingIfPossible();
        return true;
    }

    if (line.rfind("BET ", 0) == 0) {
        if (phase != Phase::Betting) return true;
        std::istringstream iss(line.substr(4));
        int count, face;
        if (!(iss >> count >> face)) return true;

        if (turnOrder.empty() || turnOrder[turnIndex] != client) {
            sendLine(client, "INFO NotYourTurn");
            return true;
        }
        if (!isValidRaise(count, face)) {
            sendLine(client, "INFO InvalidBet");
            return true;
        }
        currentBetCount = count;
        currentBetFace = face;
        currentBetter = nameOf(client);

        broadcastCurrentBet();
        advanceTurn();
        return true;
    }

    if (line == "DOUBT") {
        if (phase != Phase::Betting) return true;
        resolveDoubt(client);
        return true;
    }

    if (line == "NEXT" && phase == Phase::Reveal) {
        beginNextRound();
        return true;
    }

    return true;
}

void Server::sendLine(sf::TcpSocket* client, const std::string& line) {
    sf::Packet out; out << line;
    client->send(out);
}

void Server::broadcast(const std::string& line) {
    for (auto& up : clients) {
        sendLine(up.get(), line);
    }
}

std::string Server::nameOf(sf::TcpSocket* s) const {
    auto it = playersBySock.find(const_cast<sf::TcpSocket*>(s));
    if (it == playersBySock.end()) return "Unknown";
    return it->second.name;
}

Server::PlayerInfo* Server::getPlayerByName(const std::string& name) {
    for (auto& kv : playersBySock) {
        if (kv.second.name == name) return &kv.second;
    }
    return nullptr;
}

void Server::setupTurnOrderIfNeeded() {
    if (!turnOrder.empty()) return;
    for (auto& up : clients) {
        auto* s = up.get();
        if (playersBySock.count(s) && playersBySock[s].diceCount > 0)
            turnOrder.push_back(s);
    }
    turnIndex = 0;
}

void Server::startBettingIfPossible() {
    if (turnOrder.empty()) setupTurnOrderIfNeeded();
    int attempts = 0;
    while (!turnOrder.empty() && playersBySock[turnOrder[turnIndex]].diceCount <= 0 && attempts < (int)turnOrder.size()) {
        turnIndex = (turnIndex + 1) % (int)turnOrder.size();
        attempts++;
    }
    if (!turnOrder.empty()) broadcastTurn();
    broadcastCurrentBet();
}

void Server::advanceTurn() {
    if (turnOrder.empty()) return;
    int n = (int)turnOrder.size();
    for (int i = 0; i < n; ++i) {
        turnIndex = (turnIndex + 1) % n;
        auto* s = turnOrder[turnIndex];
        if (playersBySock[s].diceCount > 0) break;
    }
    broadcastTurn();
}

void Server::broadcastPlayerDiceCounts() {
    for (auto& kv : playersBySock) {
        std::ostringstream oss;
        oss << "DICECOUNT " << kv.second.name << ' ' << kv.second.diceCount;
        broadcast(oss.str());
    }
}
void Server::broadcastTurn() {
    if (turnOrder.empty()) return;
    broadcast("TURN " + nameOf(turnOrder[turnIndex]));
}
void Server::broadcastCurrentBet() {
    if (currentBetCount == 0) broadcast("CURRENTBET None 0 0");
    else {
        std::ostringstream oss;
        oss << "CURRENTBET " << currentBetter << ' ' << currentBetCount << ' ' << currentBetFace;
        broadcast(oss.str());
    }
}

void Server::broadcastRevealAll() {
    phase = Phase::Reveal;
    broadcast("PHASE REVEAL");
    for (auto& kv : roundDice) {
        std::ostringstream oss;
        oss << "REVEAL " << kv.first;
        for (int d : kv.second) oss << ' ' << d;
        broadcast(oss.str());
    }
}

void Server::rollAllDice() {
    roundDice.clear();
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 6);

    for (auto& kv : playersBySock) {
        auto& pi = kv.second;
        if (pi.diceCount <= 0) continue;
        std::vector<int> v; v.reserve(pi.diceCount);
        for (int i = 0; i < pi.diceCount; ++i) v.push_back(dist(gen));
        roundDice[pi.name] = std::move(v);
    }
    broadcastPlayerDiceCounts();
}

void Server::sendPrivateDiceToOwners() {
    for (auto& kv : playersBySock) {
        const std::string& name = kv.second.name;
        sf::TcpSocket* sock = kv.second.sock;
        auto it = roundDice.find(name);
        if (it == roundDice.end()) continue;
        std::ostringstream oss;
        oss << "MYDICE";
        for (int d : it->second) oss << ' ' << d;
        sendLine(sock, oss.str());
    }
}

int Server::countMatching(const std::map<std::string, std::vector<int>>& allDice, int betFace) const {
    // Ones are wild unless:
    //  - Palifico round (not wild), or
    //  - Bet is ONES (ones count only as ones)
    bool palifico = isPalificoRound();
    int total = 0;
    for (auto& kv : allDice) {
        for (int d : kv.second) {
            if (betFace == 1) {                 // betting in ones
                if (d == 1) total++;
            }
            else if (palifico) {              // palifico: ones NOT wild
                if (d == betFace) total++;
            }
            else {                            // normal: ones wild
                if (d == betFace || d == 1) total++;
            }
        }
    }
    return total;
}

bool Server::isPalificoRound() const {
    if (turnOrder.empty()) return false;
    auto* s = turnOrder[turnIndex];
    auto it = playersBySock.find(s);
    if (it == playersBySock.end()) return false;
    return it->second.diceCount == 1;
}

// ---- Perudo raise rules with 1's conversions ----
bool Server::isValidRaise(int newCount, int newFace) const {
    if (newFace < 1 || newFace > 6 || newCount <= 0) return false;

    bool palifico = isPalificoRound();

    if (palifico) {
        // Palifico: 1's are NOT wild and cannot be bet; face cannot change; quantity must increase.
        if (newFace == 1) return false; // no ones bids
        if (currentBetCount == 0) return true; // first bet in round
        if (newFace != currentBetFace) return false; // face locked
        return newCount > currentBetCount;
    }

    // Non-palifico:
    // Opening bet cannot be ones (per your rule).
    if (currentBetCount == 0) {
        if (newFace == 1) return false;
        return true;
    }

    // Current bet context:
    int cCount = currentBetCount;
    int cFace = currentBetFace;

    // From non-ones to non-ones: increase count OR same count higher face
    if (cFace != 1 && newFace != 1) {
        if (newCount > cCount) return true;
        if (newCount == cCount && newFace > cFace) return true;
        return false;
    }

    // From non-ones to ones:
    // New ones count must be at least ceil(cCount / 2)
    if (cFace != 1 && newFace == 1) {
        int minOnes = (cCount + 1) / 2; // ceil
        return newCount >= minOnes;
    }

    // From ones to ones: must strictly increase count
    if (cFace == 1 && newFace == 1) {
        return newCount > cCount;
    }

    // From ones to non-ones:
    // New non-ones must be >= (2 * cCount + 1)
    if (cFace == 1 && newFace != 1) {
        int minNonOnes = 2 * cCount + 1;
        return newCount >= minNonOnes;
    }

    return false;
}

void Server::resolveDoubt(sf::TcpSocket* challenger) {
    if (currentBetCount == 0 || currentBetFace == 0 || currentBetter.empty()) return;

    broadcastRevealAll(); // sends everyone’s dice
    phase = Phase::Reveal;

    int matches = countMatching(roundDice, currentBetFace);
    bool betHolds = (matches >= currentBetCount);

    std::string bettor = currentBetter;
    std::string challengerName = nameOf(challenger);
    std::string loser = betHolds ? challengerName : bettor;
    lastRoundLoser = loser;

    auto* loserP = getPlayerByName(loser);
    if (loserP && loserP->diceCount > 0) {
        loserP->diceCount--;
        broadcast("INFO LostDie " + loser + " " + std::to_string(loserP->diceCount));
        broadcastPlayerDiceCounts();
        if (loserP->diceCount == 0) broadcast("INFO Eliminated " + loser);
    }

    int alive = 0;
    for (auto& kv : playersBySock) if (kv.second.diceCount > 0) alive++;
    if (alive < 2) {
        std::string winner = "Unknown";
        for (auto& kv : playersBySock) if (kv.second.diceCount > 0) winner = kv.second.name;
        broadcast("INFO Winner " + winner);
        phase = Phase::Lobby;
    }
}

void Server::beginNextRound() {
    // prune eliminated
    turnOrder.erase(
        std::remove_if(turnOrder.begin(), turnOrder.end(),
            [&](sf::TcpSocket* s) { return !playersBySock.count(s) || playersBySock[s].diceCount <= 0; }),
        turnOrder.end()
    );
    if (turnOrder.empty()) setupTurnOrderIfNeeded();

    currentBetCount = currentBetFace = 0;
    currentBetter.clear();

    rollAllDice();
    sendPrivateDiceToOwners(); // so clients update their own dice immediately

    broadcast("PHASE BETTING");
    phase = Phase::Betting;

    if (!turnOrder.empty()) {
        // opener = loser of last round; if none (first round), the first roller
        std::string opener = !lastRoundLoser.empty() ? lastRoundLoser : firstRoundStarter;
        for (int i = 0; i < (int)turnOrder.size(); ++i) {
            if (playersBySock[turnOrder[i]].name == opener) {
                turnIndex = i;
                break;
            }
        }
        broadcast("TURN " + nameOf(turnOrder[turnIndex]));
    }
    broadcastCurrentBet();
}
