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

    // My dice sprites
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

    // Buttons (layout top-right)
    std::vector<Button> buttons = {
        {{780, 14, 90, 30}, "Count +"},
        {{680, 14, 90, 30}, "Count -"},
        {{780, 54, 90, 30}, "Face +"},
        {{680, 54, 90, 30}, "Face -"},
        {{680, 100, 190, 34}, "BET"},
        {{680, 144, 190, 34}, "DOUBT"},
        {{680, 188, 190, 34}, "NEXT ROUND"}
    };

    // Rolling animation
    bool rolling = false;
    sf::Clock rollClock;
    const float rollDuration = 1.0f; // seconds
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<> faceDist(1, 6);

    std::cout << "Controls: Use buttons or keys: R (start once), B/Enter (Bet), D (Doubt)\n";

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
                if (e.key.code == sf::Keyboard::R) client.requestRoll(); // only works once
                if (e.key.code == sf::Keyboard::D) client.sendDoubt();
                if (e.key.code == sf::Keyboard::Enter || e.key.code == sf::Keyboard::B) client.sendBet(selCount, selFace);
                if (e.key.code == sf::Keyboard::Up)   selCount = std::min(selCount + 1, 50);
                if (e.key.code == sf::Keyboard::Down) selCount = std::max(selCount - 1, 1);
                if (e.key.code >= sf::Keyboard::Num1 && e.key.code <= sf::Keyboard::Num6)
                    selFace = (int)e.key.code - (int)sf::Keyboard::Num1 + 1;
            }
        }

        // ---- Network ----
        client.poll();

        // Trigger animation on first R (ROLL) and whenever we get our MYDICE
        if (!rolling && (client.lastMessage.rfind("MYDICE", 0) == 0)) {
            rolling = true;
            rollClock.restart();
        }


        // update list of names (me first, then others)
        seatNames.resize(1);
        for (auto& kv : client.players) {
            if (kv.first != username) seatNames.push_back(kv.first);
            if ((int)seatNames.size() >= 8) break; // show up to 8 players
        }
        int totalPlayers = std::max(1, (int)seatNames.size());

        // If MYDICE arrived, and we’re not currently rolling, set my sprites to actual faces
        auto itMe = client.players.find(username);
        bool haveMyDice = (itMe != client.players.end() && !itMe->second.revealedDice.empty());

        if (!rolling && haveMyDice) {
            for (int i = 0; i < (int)myDiceSprites.size(); ++i) {
                int face = (i < (int)itMe->second.revealedDice.size()) ? itMe->second.revealedDice[i] : 1;
                myDiceSprites[i].setTexture(*diceTex[std::clamp(face, 1, 6) - 1]);
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
                    sf::Sprite d(*diceTex[std::clamp(it->second.revealedDice[i], 1, 6) - 1]);
                    scaleSpriteToFit(d, 50.f, 50.f);
                    d.setOrigin(d.getLocalBounds().width / 2.f, d.getLocalBounds().height / 2.f);
                    float angle = i * (2.f * 3.14159f / std::max(1, n)) - 3.14159f / 2.f;
                    d.setPosition(seat.x + R * std::cos(angle), seat.y + R * std::sin(angle));
                    window.draw(d);
                }
            }

            // opponent name + dice count
            if (hasFont) {
                int dcount = (client.players.count(name) ? client.players[name].diceCount : 0);
                auto t = makeText(name + " (" + std::to_string(dcount) + ")", 18, seat.x - 60, seat.y + 70);
                window.draw(t);
            }
        }

        // Me (seat 0) — draw pentagon of dice
        {
            sf::Vector2f center = seats.getSeatPosition(0, totalPlayers);
            float R = 90.f;
            int n = (int)myDiceSprites.size();

            for (int i = 0; i < n; ++i) {
                // rolling animation: show random faces until timer ends
                if (rolling) {
                    // During animation, only show random faces
                    int rf = faceDist(rng);
                    myDiceSprites[i].setTexture(*diceTex[rf - 1]);
                }
                else if (!rolling && haveMyDice) {
                    // Only apply real faces *after* rolling finishes
                    int face = (i < (int)itMe->second.revealedDice.size()) ? itMe->second.revealedDice[i] : 1;
                    myDiceSprites[i].setTexture(*diceTex[std::clamp(face, 1, 6) - 1]);
                }

                myDiceSprites[i].setOrigin(myDiceSprites[i].getLocalBounds().width / 2.f,
                    myDiceSprites[i].getLocalBounds().height / 2.f);

                float angle = i * (2.f * 3.14159f / n) - 3.14159f / 2.f;
                myDiceSprites[i].setPosition(center.x + R * std::cos(angle),
                    center.y + R * std::sin(angle));
                window.draw(myDiceSprites[i]);
            }

            // end of animation window
            if (rolling && rollClock.getElapsedTime().asSeconds() >= rollDuration) {
                rolling = false;
                // lock in actual faces if we have them
                if (haveMyDice) {
                    for (int i = 0; i < (int)myDiceSprites.size(); ++i) {
                        int face = (i < (int)itMe->second.revealedDice.size()) ? itMe->second.revealedDice[i] : 1;
                        myDiceSprites[i].setTexture(*diceTex[std::clamp(face, 1, 6) - 1]);
                    }
                }
            }
        }

        // HUD text / current state
        if (hasFont) {
            std::string betStr = client.currentBetCount > 0
                ? (client.currentBetter + ": " + std::to_string(client.currentBetCount) + " x " + std::to_string(client.currentBetFace) + "s")
                : "No bet yet";

            auto t1 = makeText("Phase: " + client.phase + "    Turn: " + client.currentTurn, 20, 14, 10);
            auto t2 = makeText("Current Bet: " + betStr, 20, 14, 36);
            auto t3 = makeText("Select -> Count: " + std::to_string(selCount) + "  Face: " + std::to_string(selFace), 18, 14, 62);
            window.draw(t1); window.draw(t2); window.draw(t3);
        }

        // Draw buttons
        if (hasFont) {
            for (const auto& b : buttons) {
                sf::RectangleShape rect; rect.setPosition(b.rect.left, b.rect.top);
                rect.setSize({ b.rect.width, b.rect.height });
                rect.setFillColor(sf::Color(20, 20, 20, 180));
                rect.setOutlineColor(sf::Color::White);
                rect.setOutlineThickness(1.f);
                auto lbl = makeText(b.label, 18, b.rect.left + 10, b.rect.top + 5);
                window.draw(rect); window.draw(lbl);
            }
        }

        window.display();
    }

    return 0;
}
