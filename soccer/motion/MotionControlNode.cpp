#include "MotionControlNode.hpp"
#include "Robot.hpp"

MotionControlNode::MotionControlNode(Context* context)
    : _context(context), _controllers(Num_Shells, std::nullopt) {
    _controllers.reserve(Num_Shells);
    for (OurRobot* robot : context->state.self) {
        _controllers[robot->shell()] = MotionControl(context, robot);
    }
}

void MotionControlNode::run() {
    bool force_stop = _context->game_state.state == GameState::State::Halt;
    for (auto& maybe_controller : _controllers) {
        if (!maybe_controller) {
            continue;
        }

        auto controller = maybe_controller.value();

        if (force_stop) {
            controller.stopped();
        } else {
            // TODO(Kyle): Ignore this if it's manually controlled.
            controller.run();
        }
    }
}
