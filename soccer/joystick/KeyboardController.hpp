#pragma once

#include "Joystick.hpp"

/**
 * @brief Use Keyboard to control robots
 */
class KeyboardController : public Joystick {
public:
    KeyboardController();
private:
    JoystickControlValues _controls;
};
