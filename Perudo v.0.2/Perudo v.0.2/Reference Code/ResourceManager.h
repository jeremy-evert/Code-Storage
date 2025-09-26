#pragma once
#include <SFML/Graphics.hpp>
#include <map>
#include <string>
#include <memory>
#include <iostream>

class ResourceManager {
public:
    static sf::Texture& getTexture(const std::string& filename) {
        // If texture is already loaded, return it
        auto it = textures.find(filename);
        if (it != textures.end()) {
            return *it->second;
        }

        // Otherwise, load it
        auto texture = std::make_unique<sf::Texture>();
        if (!texture->loadFromFile(filename)) {
            std::cerr << "Error: could not load texture " << filename << std::endl;
            static sf::Texture dummy; // fallback
            return dummy;
        }

        sf::Texture& ref = *texture;
        textures[filename] = std::move(texture);
        return ref;
    }

private:
    static std::map<std::string, std::unique_ptr<sf::Texture>> textures;
};