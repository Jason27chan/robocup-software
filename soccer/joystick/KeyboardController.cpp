#include "KeyboardController.hpp"

using namespace std;

namespace {
constexpr auto Dribble_Step_Time = RJ::Seconds(0.125);
constexpr auto Kicker_Step_Time = RJ::Seconds(0.125);
const float AXIS_MAX = 32768.0f;
// cutoff for counting triggers as 'on'
const float TRIGGER_CUTOFF = 0.9;
}

KeyboardController::KeyboardController()
	// I dont know what this does 
    : _controller(nullptr), _lastDribblerTime(), _lastKickerTime() {
    // Check if the keyboard is plugged in?

    // Current problem is that we need to use an event detector inside
    // our UI, from which we will request information from our KeyboardController. 

    // We need to detect certain key presses, and relay that information to our
    // KeyboardController, which will give joystick values to the processor

    // Controllers will be detected later if needed.
    connected = false;
    controllerId = -1;
    robotId = -1;
    openJoystick();
}

void KeyboardController::update() {
    QMutexLocker(&mutex());
    SDL_GameControllerUpdate();

    RJ::Time now = RJ::now();

    /*
     *  DRIBBLER ON/OFF
     */

    /*
     *  DRIBBLER POWER
     */

    /*
     *  KICKER POWER
     */

    /*
     *  KICK TRUE/FALSE
     */

    /*
     *  CHIP TRUE/FALSE
     */

    /*
     *  VELOCITY ROTATION
     */
    
    /*
     *  VELOCITY TRANSLATION
     */
   
   // if "i" is pressed
    //   input.y() = 0.5
	//   input.x() = 0    	
    // if "k" is pressed
    //   input.y() = -0.5
	//   input.x() = 0    	
    // if "j" is pressed
    //   input.y() = 0
	//   input.x() = -0.5    	
    // if "l" is pressed
    //   input.y() = 0
	//   input.x() = 0.5 

    Geometry2d::Point input(0, 0);

    char currKey = getKeyboardControlFilter.getKeyPressed

    // Align along an axis using the DPAD as modifier buttons
    if (currKey == 'I') {
        input.y() = -0.5;
        input.x() = 0;
    } else if (currKey == 'K') {
        input.y() = 0.5;
        input.x() = 0;
    } else if (currKey == 'J') {
        input.y() = 0;
        input.x() = -0.5;
    } else if (currKey == 'L') {
        input.y() = 0;
        input.x() = 0.5;
    }

    _controls.translation = Geometry2d::Point(input.x(), input.y());
}

JoystickControlValues KeyboardController::getJoystickControlValues() {
    QMutexLocker(&mutex());
    return _controls;
}