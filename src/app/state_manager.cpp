#include "state_manager.hpp"
#include "views/clock_view.hpp"
#include "island.hpp"

namespace {

FontHandle main_font{};
Object::Handle<Object::Layout> root{};

}

void StateManager::switch_state(StateType state) {
    const auto previous = Island::get_state();
    if (previous == StateType::Clock && state != StateType::Clock) ClockView::stop();
    Island::set_state(state);
    // start() is idempotent and can recreate a view after the scene was cleared.
    if (state == StateType::Clock) ClockView::start();
}
