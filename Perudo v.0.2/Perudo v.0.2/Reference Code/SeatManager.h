#pragma once
#include <SFML/Graphics.hpp>
#include <vector>

// SeatManager is responsible for calculating positions
// of players (your dice at bottom, others with cups).
class SeatManager {
public:
    SeatManager(float windowWidth, float windowHeight);

    // Get seat position for a given player index
    sf::Vector2f getSeatPosition(int playerIndex, int totalPlayers);

private:
    float winW;
    float winH;
};
