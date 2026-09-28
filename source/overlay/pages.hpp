// ValorNX (overlay) - páginas que se usan desde varios sitios del panel.
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "../core/favorites.hpp"
#include "../core/search.hpp"
#include "ui_kit.hpp"

namespace vnx {
namespace ovl {

using TaskDone = std::function<void(Panel&, Task&)>;

// Pestaña "Buscar" de la página principal (search_pages.cpp).
Page* makeSearchTab();
// Barra de progreso de una búsqueda; B la cancela (search_pages.cpp).
Page* makeTaskPage(std::unique_ptr<Task> task, TaskDone done, uint64_t now);

// Teclado para escribir un valor nuevo en la memoria del juego. 'name' sale en el aviso final.
void editValue(Panel& panel, const std::string& title, const std::string& name, uint64_t addr, VType t);
// Guarda una dirección en "Mis valores" eligiendo el nombre de una lista (en el overlay no hay
// teclado de letras). 'after' recibe el valor guardado, o el que ya existía.
void saveFavorite(Panel& panel, uint64_t addr, VType t, std::function<void(Panel&, const Favorite&)> after = nullptr);
// x2, x3, x5, x10 para un valor guardado (panel.cpp).
void openFactorMenu(Panel& panel, const Favorite& f);

}  // namespace ovl
}  // namespace vnx
