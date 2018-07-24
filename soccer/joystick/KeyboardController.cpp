#include "KeyboardController.hpp"

using namespace std;

namespace {
constexpr auto Dribble_Step_Time = RJ::Seconds(0.125);
constexpr auto Kicker_Step_Time = RJ::Seconds(0.125);
const float AXIS_MAX = 32768.0f;
// cutoff for counting triggers as 'on'
const float TRIGGER_CUTOFF = 0.9;
}