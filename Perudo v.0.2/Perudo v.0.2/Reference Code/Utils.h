#pragma once
#include <SFML/Graphics.hpp>

inline void scaleSpriteToSize(sf::Sprite& sprite, float targetWidth, float targetHeight) {
    sf::FloatRect bounds = sprite.getLocalBounds();
    sprite.setScale(
        targetWidth / bounds.width,
        targetHeight / bounds.height
    );
}