#include "SeatManager.h"
#include <cmath>

SeatManager::SeatManager(float windowWidth, float windowHeight)
    : winW(windowWidth), winH(windowHeight) {
}

// Seats:
// 0 = you (bottom center)
// others arranged clockwise around "table"
sf::Vector2f SeatManager::getSeatPosition(int playerIndex, int totalPlayers) {
    if (playerIndex == 0) {
        return sf::Vector2f(winW / 2.f, winH - 150.f); // bottom center
    }

    // Circle around table
    float radius = std::min(winW, winH) / 2.5f;
    float angleStep = 2 * 3.14159f / (totalPlayers - 1);

    float angle = (playerIndex - 1) * angleStep - 3.14159f / 2.f;
    float x = winW / 2.f + radius * std::cos(angle);
    float y = winH / 2.f + radius * std::sin(angle);

    return sf::Vector2f(x, y);
}
