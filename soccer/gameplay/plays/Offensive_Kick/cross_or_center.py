import main
import constants
import robocup
import behavior
import standard_play
import evaluation

from tactics import coordinated_pass
from situations import Situation

#Holds a point or a line, with the associted priority ranging from 0 to the number of lines/pts - 1 (0 is highest priority)
class PointAndPriority:
    def __init__(self, line_pt, priority):
        self.line_pt = line_pt
        self.priority = priority

## 
# A play that passes to the center in front of the opponent goal
#
class CrossOrCenter(standard_play.StandardPlay):
    #point with lowest probability will have its probability scaled by MINIMUM_MULTIPLIER
    MINIMUM_MULTIPLIER = 0.5
    priorities = {'line1': 0, 'line2': 1, 'line3': 2}

    _situationList = [
        Situation.OFFENSIVE_KICK
    ] # yapf: disable

    def __init__(self):
        super().__init__(continuous = True)
        priorities = CrossOrCenter.priorities
        reflector = 1
        #Depending on corner's location the points are refelcted
        if main.ball().pos.x < 0:
            reflector = -1

        line1 = PointAndPriority(robocup.Segment(robocup.Point(constants.Field.Width / -2 * 
            0.633 * reflector, 7.45), 
            robocup.Point(constants.Field.Width / -2 * 0.633 * reflector, 8.5)), priorities.get('line1',0))
        line2 = PointAndPriority(robocup.Segment(robocup.Point(constants.Field.Width / -2 
            * 0.187 * reflector, 7.11), 
            robocup.Point(constants.Field.Width / 2 * 0.147 * reflector, 6.2)), priorities.get('line2',0))
        line3 = PointAndPriority(robocup.Segment(robocup.Point(constants.Field.Width / 2 * 
            0.8 * reflector, 5.85), 
            robocup.Point(constants.Field.Width / 2 * 0.633 * reflector, 5.85)), priorities.get('line3',0))

        lines = [line1, line2, line3]
        best_points = []

        for line in lines:
            best_points.append(PointAndPriority(self.best_point_on_line(line), line.priority))

        multiplier_increment = CrossOrCenter.MINIMUM_MULTIPLIER/(len(best_points) - 1)

        best_point = max(best_points,key = lambda point: 
                evaluation.passing.eval_pass(main.ball().pos, point.line_pt) 
                - multiplier_increment * point.priority)
        
        self.add_subbehavior(coordinated_pass.CoordinatedPass(best_point.line_pt), 'passing')
        
    #Takes three points on the line (the two ends and the midpoint) and returns one with highest probability
    def best_point_on_line(self, PointAndPriority):
        line = PointAndPriority.line_pt
        points = [line.get_pt(0), line.get_pt(1), line.center()]
        best_point = max(points,key = lambda point: 
            evaluation.passing.eval_pass(main.ball().pos, point))
        return best_point
