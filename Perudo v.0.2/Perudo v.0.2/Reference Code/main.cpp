#include <SFML/Graphics.hpp>
#include "ResourceManager.h"
#include "SeatManager.h"
#include "Client.h"
#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <string>
#include <algorithm>
#include <random>

// ---------- helpers ----------
static void scaleSpriteToFit(sf::Sprite& sprite, float targetW, float targetH) {
    sf::FloatRect b = sprite.getLocalBounds();
    if (b.width == 0 || b.height == 0) return;
    sprite.setScale(targetW / b.width, targetH / b.height);
}

struct Button {
    sf::FloatRect rect;
    std::string label;
};

static bool hit(const Button& b, sf::Vector2f p) {
    return b.rect.contains(p);
}

static void drawButton(sf::RenderWindow& window, const Button& b, const sf::Font* font) {
    sf::RectangleShape r;
    r.setPosition(b.rect.left, b.rect.top);
    r.setSize({ b.rect.width, b.rect.height });
    r.setFillColor(sf::Color(30, 30, 30, 200));
    r.setOutlineColor(sf::Color::White);
    r.setOutlineThickness(1.f);
    window.draw(r);

    if (font) {
        sf::Text t;
        t.setFont(*font);
        t.setCharacterSize(18);
        t.setFillColor(sf::Color::White);
        t.setString(b.label);
        sf::FloatRect tb = t.getLocalBounds();
        float tx = b.rect.left + (b.rect.width - tb.width) * 0.5f - tb.left;
        float ty = b.rect.top + (b.rect.height - tb.height) * 0.5f - tb.top;
        t.setPosition(tx, ty);
        window.draw(t);
    }
}

// ---------- main ----------
int main(int argc, char* argv[]) {
    std::string username = "Player";
    if (argc > 1) username = argv[1];

    sf::RenderWindow window(sf::VideoMode(1000, 720), "Perudo - Multiplayer (Client)");
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = font.loadFromFile("assets/fonts/OpenSans-Regular.ttf");

    SeatManager seats(window.getSize().x, window.getSize().y);

    Client client;
    client.connectToServer("127.0.0.1", 54000, username);

    // Textures
    std::vector<sf::Texture*> diceTex;
    for (int i = 1; i <= 6; ++i)
        diceTex.push_back(&ResourceManager::getTexture("assets/dice/die" + std::to_string(i) + ".png"));
    sf::Texture* cupTex = &ResourceManager::getTexture("assets/cup.png");

    // My dice sprites (size dynamically tracks my diceCount)
    std::vector<sf::Sprite> myDiceSprites(5);
    for (auto& s : myDiceSprites) { s.setTexture(*diceTex[0]); scaleSpriteToFit(s, 64.f, 64.f); }

    std::vector<std::string> seatNames; seatNames.push_back(username);

    // HUD selection state
    int selCount = 1, selFace = 2;

    auto makeText = [&](const std::string& str, unsigned size, float x, float y) {
        sf::Text t;
        if (hasFont) { t.setFont(font); t.setCharacterSize(size); t.setFillColor(sf::Color::White); }
        t.setString(str); t.setPosition(x, y);
        return t;
        };

    // Buttons (top-right)
    std::vector<Button> buttons = {
        {{780, 14, 90, 30}, "Count +"},
        {{680, 14, 90, 30}, "Count -"},
        {{780, 54, 90, 30}, "Face +"},
        {{680, 54, 90, 30}, "Face -"},
        {{680,100,190, 34}, "BET"},
        {{680,144,190, 34}, "DOUBT"},
        {{680,188,190, 34}, "NEXT ROUND"}
    };

    // Rolling animation state
    bool rolling = false;
    sf::Clock rollClock;
    const float rollDuration = 1.0f;
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<> faceDist(1, 6);

    // Track state to robustly trigger animation & resize dice ring
    std::vector<int> lastMyDice; // last received MYDICE faces
    int lastMyCount = 5;

    std::cout << "Controls: R=start once (Lobby), B/Enter=Bet, D=Doubt, N=Next Round\n";

    while (window.isOpen()) {
        // ---- Input ----
        sf::Event e;
        while (window.pollEvent(e)) {
            if (e.type == sf::Event::Closed) window.close();

            if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f mp((float)e.mouseButton.x, (float)e.mouseButton.y);
                if (hit(buttons[0], mp)) selCount = std::min(selCount + 1, 50);
                else if (hit(buttons[1], mp)) selCount = std::max(selCount - 1, 1);
                else if (hit(buttons[2], mp)) selFace = std::min(selFace + 1, 6);
                else if (hit(buttons[3], mp)) selFace = std::max(selFace - 1, 1);
                else if (hit(buttons[4], mp)) client.sendBet(selCount, selFace);
                else if (hit(buttons[5], mp)) client.sendDoubt();
                else if (hit(buttons[6], mp)) client.sendNextRound();
            }

            if (e.type == sf::Event::KeyPressed) {
                if (e.key.code == sf::Keyboard::R) client.requestRoll(); // Lobby only; client logs success/fail
                if (e.key.code == sf::Keyboard::N) client.sendNextRound(); // keyboard fallback for NEXT
                if (e.key.code == sf::Keyboard::D) client.sendDoubt();
                if (e.key.code == sf::Keyboard::Enter || e.key.code == sf::Keyboard::B) client.sendBet(selCount, selFace);
                if (e.key.code == sf::Keyboard::Up)   selCount = std::min(selCount + 1, 50);
                if (e.key.code == sf::Keyboard::Down) selCount = std::max(selCount - 1, 1);
                if (e.key.code >= sf::Keyboard::Num1 && e.key.code <= sf::Keyboard::Num6)
                    selFace = (int)e.key.code - (int)sf::Keyboard::Num1 + 1;
            }
        }

        // ---- Network ----
        // Drain a few packets each frame (helps responsiveness)
        for (int i = 0; i < 4; ++i) {
            if (!client.poll()) break;
        }

        // Build seat list (me + others)
        seatNames.resize(1);
        for (auto& kv : client.players) {
            if (kv.first != username) seatNames.push_back(kv.first);
            if ((int)seatNames.size() >= 8) break;
        }
        int totalPlayers = std::max(1, (int)seatNames.size());

        // Resize my dice ring when my diceCount changes
        int myCount = 5;
        auto itMe = client.players.find(username);
        if (itMe != client.players.end()) {
            myCount = std::max(0, itMe->second.diceCount);
        }
        if (myCount != lastMyCount) {
            lastMyCount = myCount;
            myDiceSprites.resize(myCount);
            for (auto& s : myDiceSprites) { s.setTexture(*diceTex[0]); scaleSpriteToFit(s, 64.f, 64.f); }
        }

        // Trigger animation when MYDICE (faces) change and we’re in BETTING
        std::vector<int> curMyDice;
        if (itMe != client.players.end()) curMyDice = itMe->second.revealedDice;

        if (client.phase == "BETTING" && curMyDice != lastMyDice) {
            lastMyDice = curMyDice;
            if (!curMyDice.empty()) {
                rolling = true;
                rollClock.restart();
                std::cout << "Client: starting roll animation (" << curMyDice.size() << " dice)\n";
            }
        }

        // ---- Draw ----
        window.clear(sf::Color(30, 120, 50));

        // Opponents
        for (int p = 1; p < totalPlayers; ++p) {
            sf::Vector2f seat = seats.getSeatPosition(p, totalPlayers);
            const std::string& name = seatNames[p];
            auto it = client.players.find(name);
            bool revealed = (client.phase == "REVEAL" && it != client.players.end() && !it->second.revealedDice.empty());

            if (!revealed) {
                sf::Sprite cup(*cupTex);
                scaleSpriteToFit(cup, 120.f, 120.f);
                cup.setOrigin(cup.getLocalBounds().width / 2.f, cup.getLocalBounds().height / 2.f);
                cup.setPosition(seat);
                window.draw(cup);
            }
            else {
                float R = 60.f;
                int n = (int)it->second.revealedDice.size();
                for (int i = 0; i < n; ++i) {
                    int face = std::clamp(it->second.revealedDice[i], 1, 6);
                    sf::Sprite d(*diceTex[face - 1]);
                    scaleSpriteToFit(d, 50.f, 50.f);
                    d.setOrigin(d.getLocalBounds().width / 2.f, d.getLocalBounds().height / 2.f);
                    float angle = i * (2.f * 3.14159f / std::max(1, n)) - 3.14159f / 2.f;
                    d.setPosition(seat.x + R * std::cos(angle), seat.y + R * std::sin(angle));
                    window.draw(d);
                }
            }

            if (hasFont) {
                int dcount = (client.players.count(name) ? client.players[name].diceCount : 0);
                auto t = makeText(name + " (" + std::to_string(dcount) + ")", 18, seat.x - 60, seat.y + 70);
                window.draw(t);
            }
        }

        // Me (pentagon)
        {
            sf::Vector2f center = seats.getSeatPosition(0, totalPlayers);
            float R = 90.f;
            int n = (int)myDiceSprites.size();

            for (int i = 0; i < n; ++i) {
                if (rolling) {
                    int rf = faceDist(rng);
                    myDiceSprites[i].setTexture(*diceTex[rf - 1]);
                }
                else {
                    int face = 1;
                    if (i < (int)lastMyDice.size()) face = std::clamp(lastMyDice[i], 1, 6);
                    myDiceSprites[i].setTexture(*diceTex[face - 1]);
                }

                myDiceSprites[i].setOrigin(myDiceSprites[i].getLocalBounds().width / 2.f,
                    myDiceSprites[i].getLocalBounds().height / 2.f);
                float angle = i * (2.f * 3.14159f / std::max(1, n)) - 3.14159f / 2.f;
                myDiceSprites[i].setPosition(center.x + R * std::cos(angle),
                    center.y + R * std::sin(angle));
                window.draw(myDiceSprites[i]);
            }

            if (rolling && rollClock.getElapsedTime().asSeconds() >= rollDuration) {
                rolling = false;
                // No extra work; textures lock to lastMyDice automatically
                std::cout << "Client: roll animation finished\n";
            }
        }

        // HUD
        if (hasFont) {
            std::string betStr = client.currentBetCount > 0
                ? (client.currentBetter + ": " + std::to_string(client.currentBetCount) + " × " + std::to_string(client.currentBetFace) + "s")
                : "No bet yet";
            auto t1 = makeText("Phase: " + client.phase + "    Turn: " + client.currentTurn, 20, 14, 10);
            auto t2 = makeText("Current Bet: " + betStr, 20, 14, 36);
            auto t3 = makeText("Select -> Count: " + std::to_string(selCount) + "  Face: " + std::to_string(selFace), 18, 14, 62);
            window.draw(t1); window.draw(t2); window.draw(t3);
        }

        // Buttons
        for (const auto& b : buttons) drawButton(window, b, hasFont ? &font : nullptr);

        window.display();
    }

    return 0;
}
