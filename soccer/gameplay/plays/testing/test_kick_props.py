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

        self.add_transition(behavior.Behavior.State.start,
                            behavior.Behavior.State.running, lambda: True,
                            'immediately')

        kick = skills.pivot_kick.PivotKick()
        kick.target = constants.Field.OurGoalSegment
        kick.aim_params['desperate_timeout'] = 5
        self.add_subbehavior(kick, 'kick', required=False)
        
        self.shell_id = None
        self.curr_max_vel = 0

    def execute_running(self):
        for bhvr in self.all_subbehaviors():
            if bhvr.robot != None:
                self.shell_id=bhvr.robot.shell_id()

        kick = self.subbehavior_with_name('kick')

        if main.ball().vel.mag() > self.curr_max_vel:
            self.curr_max_vel = main.ball().vel.mag()

        if kick.is_done_running():
            self.maxSpeeds.append(self.curr_max_vel)
            self.curr_max_vel = 0
            print("speeds:", self.maxSpeeds)
            print("mean:", statistics.mean(self.maxSpeeds))
            if (len(self.maxSpeeds) > 1):
                print("variance:", statistics.variance(self.maxSpeeds))
                print("standard deviation:", statistics.stdev(self.maxSpeeds))
            kick.restart()

    def role_requirements(self):
        reqs = super().role_requirements()
        if self.shell_id is not None:
            for req in role_assignment.iterate_role_requirements_tree_leaves(reqs):
                req.required_shell_id = self.shell_id if self.shell_id != None else -1
        return reqs
