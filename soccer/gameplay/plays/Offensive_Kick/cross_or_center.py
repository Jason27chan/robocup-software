import main
import constants
import robocup
import behavior
import standard_play
import situational_play_selection

from tactics import coordinated_pass

## 
# A play that passes to the center in front of the opponent goal
#
class CrossOrCenter(standard_play.StandardPlay):

    _situationList = [
        situational_play_selection.SituationalPlaySelector.Situation.OFFENSIVE_KICK
    ] # yapf: disable

    def __init__(self):
        super().__init__(continuous=True)

        self.add_transition(behavior.Behavior.State.start,
                            behavior.Behavior.State.running, lambda: True,
                            'Start Play Immediately')

        pass_pt = robocup.Point(0, constants.Field.Length * 3/4)

        self.add_subbehavior(coordinated_pass.CoordinatedPass(pass_pt), 'pass')