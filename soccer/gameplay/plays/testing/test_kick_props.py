import play
import behavior
import skills.pivot_kick
import skills.move
import enum
import main
import constants
import robocup
import role_assignment
import statistics

# this test repeatedly runs the PivotKick behavior aimed at our goal
class TestKickProps(play.Play):
    def __init__(self):
        super().__init__(continuous=True)

        self.maxSpeeds = []
        self.distances = []

        self.add_transition(behavior.Behavior.State.start,
                            behavior.Behavior.State.running, lambda: True,
                            'immediately')

        self.add_transition(
            behavior.Behavior.State.running, 
            behavior.Behavior.State.start,
            lambda: self.subbehavior_with_name('kick').state == behavior.Behavior.State.completed,
            'done with this kick')

        self.curr_robot = None
        self.shell_id = None
        self.target = constants.Field.OurGoalSegment.center()
        print("REINITIALIZE")

    def on_enter_running(self):
        self.dist_to_target = 0
        self.curr_max_vel = 0
        kick = skills.pivot_kick.PivotKick()
        kick.aim_params['desperate_timeout'] = 5
        kick.target = self.target
        self.add_subbehavior(kick, 'kick', required=False)

    def execute_running(self):
        # print(self.all_subbehaviors())
        for bhvr in self.all_subbehaviors():
            if bhvr.robot != None:
                self.shell_id = bhvr.robot.shell_id()
        # print(self.shell_id)
        self.curr_robot = self.subbehavior_with_name('kick').robot
        print(self.curr_robot)
        self.shell_id = self.curr_robot.shell_id()
        # print(self.shell_id)
        
        kick = self.subbehavior_with_name('kick')

        if main.ball().vel.mag() > self.curr_max_vel:
            self.curr_max_vel = main.ball().vel.mag()

        if main.ball().pos.y == 0:
            self.dist_to_target = main.ball().pos.x

        # horizontal_line = robocup.Line(self.robot.pos, (self.robot.pos.x + 1, self.robot.pos.y))
        # ideal_line = robocup.Line(self.robot.pos, self.target)

        # if kick.robot is not None:
        #     print(kick.robot.angle)


    def on_exit_running(self):
        self.maxSpeeds.append(self.curr_max_vel)
        self.distances.append(self.dist_to_target)
        print("speeds:", self.maxSpeeds)
        print("mean:", statistics.mean(self.maxSpeeds))
        if (len(self.maxSpeeds) > 1):
            print("variance:", statistics.variance(self.maxSpeeds))
            print("standard deviation:", statistics.stdev(self.maxSpeeds))
        self.remove_subbehavior('kick')

    def role_requirements(self):
        reqs = super().role_requirements()
        if self.shell_id is not None:
            for req in role_assignment.iterate_role_requirements_tree_leaves(reqs):
                req.required_shell_id = self.shell_id if self.shell_id != None else -1
        return reqs
