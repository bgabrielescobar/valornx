// ValorNX - lo que la interfaz necesita de la plataforma (Switch o simulador de PC).
#pragma once
#include <string>

#include "../core/game.hpp"

namespace vnx {

class Platform {
public:
    virtual ~Platform() = default;
    // Carpeta raíz de la microSD ("sdmc:/" en la Switch).
    virtual std::string sdRoot() const = 0;
    // Conecta con el juego abierto. nullptr + mensaje si no se puede.
    virtual IGame* connectGame(std::string& error) = 0;
    // Teclado del sistema. false si se canceló o no está disponible.
    virtual bool keyboard(const std::string& title, const std::string& initial, std::string& out) = 0;
    // Evita que la consola se atenúe/suspenda durante búsquedas largas.
    virtual void keepAwake(bool on) {}
};

}  // namespace vnx
