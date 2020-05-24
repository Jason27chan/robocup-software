#include "Processor.hpp"

#include <protobuf/messages_robocup_ssl_detection.pb.h>

#include <Constants.hpp>
#include <Geometry2d/Util.hpp>
#include <LogUtils.hpp>
#include <QMutexLocker>
#include <Robot.hpp>
#include <RobotConfig.hpp>
#include <Utils.hpp>
#include <gameplay/GameplayModule.hpp>
#include <planning/IndependentMultiRobotPathPlanner.hpp>
#include <rc-fshare/git_version.hpp>

#include "DebugDrawer.hpp"
#include "radio/PacketConvert.hpp"
#include "radio/RadioNode.hpp"
#include "vision/VisionFilter.hpp"

REGISTER_CONFIGURABLE(Processor)

using namespace boost;
using namespace Geometry2d;
using namespace google::protobuf;

static const auto Command_Latency = 0ms;

// TODO: Remove this and just use the one in Context.
Field_Dimensions* currentDimensions = &Field_Dimensions::Current_Dimensions;

// A temporary place to store RobotStatus/RobotConfig variables as we create
// them. They are initialized in createConfiguration, before the Processor class
// is initialized, so we need to temporarily store them somewhere.
std::vector<RobotStatus> robot_status_init;
std::unique_ptr<RobotConfig> Processor::robot_config_init;

void Processor::createConfiguration(Configuration* cfg) {
    // If robot_config_init is not null, then we've already done this.
    // That means we're doing FromRegisteredConfigurables() in python code,
    // and so we shouldn't reinitialize anything.
    if (robot_config_init) {
        return;
    }

    robot_config_init = std::make_unique<RobotConfig>(cfg, "Rev2015");

    for (size_t s = 0; s < Num_Shells; ++s) {
        robot_status_init.emplace_back(
            cfg, QString("Robot Statuses/Robot %1").arg(s));
    }
}

Processor::Processor(bool sim, bool blueTeam, const std::string& readLogFile)
    : _loopMutex(), _readLogFile(readLogFile) {
    _running = true;
    _framerate = 0;
    _initialized = false;
    _radio = nullptr;

    // Configuration-time variables.
    _context.robot_config = std::move(robot_config_init);
    for (int i = Num_Shells - 1; i >= 0; i--) {
        // Set up fields in Context
        _context.robot_status[i] = std::move(robot_status_init.back());
        robot_status_init.pop_back();
    }

    _context.field_dimensions = *currentDimensions;

    _vision = std::make_shared<VisionFilter>();
    _refereeModule = std::make_shared<Referee>(&_context);
    _refereeModule->start();
    _gameplayModule = std::make_shared<Gameplay::GameplayModule>(
        &_context, _refereeModule.get());
    _pathPlanner = std::unique_ptr<Planning::MultiRobotPathPlanner>(
        new Planning::IndependentMultiRobotPathPlanner());
    _motionControl = std::make_unique<MotionControlNode>(&_context);
    _radio = std::make_unique<RadioNode>(&_context, sim, blueTeam);
    _visionReceiver = std::make_unique<VisionReceiver>(
        &_context, sim, sim ? SimVisionPort : SharedVisionPortSinglePrimary);
    _grSimCom = std::make_unique<GrSimCommunicator>(&_context);

    // Joystick
    _sdl_joystick_node = std::make_unique<joystick::SDLJoystickNode>(&_context);
    _manual_control_node =
        std::make_unique<joystick::ManualControlNode>(&_context);

    if (!readLogFile.empty()) {
        _logger.readFrames(readLogFile.c_str());
        firstLogTime = _logger.startTime();
    }

    _nodes.push_back(_visionReceiver.get());
    _nodes.push_back(_motionControl.get());
    _nodes.push_back(_grSimCom.get());
}

Processor::~Processor() {
    stop();

    // Put back configurables where we found them.
    // This is kind of a hack, but if we don't do that they get destructed
    // when Processor dies. That normally isn't a problem, but in unit tests,
    // we create and destroy multiple instances of Processor for each test.
    robot_config_init = std::move(_context.robot_config);

    for (size_t i = 0; i < Num_Shells; i++) {
        robot_status_init.push_back(std::move(_context.robot_status[i]));
    }
}

void Processor::stop() {
    if (_running) {
        _running = false;
    }
}

void Processor::runModels() {
    std::vector<CameraFrame> frames;

    for (auto& packet : _context.vision_packets) {
        const SSL_DetectionFrame* frame = packet->wrapper.mutable_detection();
        std::vector<CameraBall> ballObservations;
        std::vector<CameraRobot> yellowObservations;
        std::vector<CameraRobot> blueObservations;

        RJ::Time time =
            RJ::Time(std::chrono::duration_cast<std::chrono::microseconds>(
                RJ::Seconds(frame->t_capture())));

        // Add ball observations
        ballObservations.reserve(frame->balls().size());
        for (const SSL_DetectionBall& ball : frame->balls()) {
            ballObservations.emplace_back(
                time, _worldToTeam * Point(ball.x() / 1000, ball.y() / 1000));
        }

        // Collect camera data from all robots
        yellowObservations.reserve(frame->robots_yellow().size());
        for (const SSL_DetectionRobot& robot : frame->robots_yellow()) {
            yellowObservations.emplace_back(
                time,
                Pose(Point(_worldToTeam *
                           Point(robot.x() / 1000, robot.y() / 1000)),
                     fixAngleRadians(robot.orientation() + _teamAngle)),
                robot.robot_id());
        }

        // Collect camera data from all robots
        blueObservations.reserve(frame->robots_blue().size());
        for (const SSL_DetectionRobot& robot : frame->robots_blue()) {
            blueObservations.emplace_back(
                time,
                Pose(Point(_worldToTeam *
                           Point(robot.x() / 1000, robot.y() / 1000)),
                     fixAngleRadians(robot.orientation() + _teamAngle)),
                robot.robot_id());
        }

        frames.emplace_back(time, frame->camera_id(), ballObservations,
                            yellowObservations, blueObservations);
    }

    _vision->addFrames(frames);

    // Fill the list of our robots/balls based on whether we are the blue team
    // or not
    _vision->fillBallState(_context.state);
    _vision->fillRobotState(_context.state, _context.game_state.blueTeam);
}

/**
 * program loop
 */
void Processor::run() {
    Status curStatus;

    bool first = true;
    // main loop
    while (_running) {
        RJ::Time startTime = RJ::now();
        auto deltaTime = startTime - curStatus.lastLoopTime;
        _framerate = RJ::Seconds(1) / deltaTime;
        curStatus.lastLoopTime = startTime;
        _context.state.time = startTime;

        if (!firstLogTime) {
            firstLogTime = startTime;
        }

        ////////////////
        // Reset

        // Make a new log frame
        _context.state.logFrame = std::make_shared<Packet::LogFrame>();
        _context.state.logFrame->set_timestamp(RJ::timestamp());
        _context.state.logFrame->set_command_time(
            RJ::timestamp(startTime + Command_Latency));
        _context.state.logFrame->set_use_our_half(
            _context.game_settings.use_our_half);
        _context.state.logFrame->set_use_opponent_half(
            _context.game_settings.use_their_half);
        _context.state.logFrame->set_manual_id(
            _context.game_settings.joystick_config.manualID);
        _context.state.logFrame->set_blue_team(_context.game_state.blueTeam);
        _context.state.logFrame->set_defend_plus_x(
            _context.game_settings.defendPlusX);
        _context.debug_drawer.setLogFrame(_context.state.logFrame.get());

        if (first) {
            first = false;

            Packet::LogConfig* logConfig =
                _context.state.logFrame->mutable_log_config();
            logConfig->set_generator("soccer");
            logConfig->set_git_version_hash(git_version_hash);
            logConfig->set_git_version_dirty(git_version_dirty);
            logConfig->set_simulation(_context.game_settings.simulation);
        }

        ////////////////
        // Inputs
        _sdl_joystick_node->run();
        _manual_control_node->run();

        updateOrientation();

        // TODO(Kyle): Don't do this here.
        // Because not everything is on modules yet, but we still need things to
        // run in order, we can't just do everything via the for loop (yet).
        _visionReceiver->run();

        if (_context.field_dimensions != *currentDimensions) {
            std::cout << "Updating field geometry based off of vision packet."
                      << std::endl;
            setFieldDimensions(_context.field_dimensions);
        }

        curStatus.lastVisionTime = _visionReceiver->getLastVisionTime();

        _radio->run();

        if (_radio) {
            curStatus.lastRadioRxTime = _radio->getLastRadioRxTime();
        }

        runModels();

        _context.vision_packets.clear();

        // Log referee data
        _refereeModule->run();
        std::vector<RefereePacket> refereePackets;
        _refereeModule->getPackets(refereePackets);
        for (const RefereePacket& packet : refereePackets) {
            SSL_Referee* log = _context.state.logFrame->add_raw_refbox();
            log->CopyFrom(packet.wrapper);
            curStatus.lastRefereeTime =
                std::max(curStatus.lastRefereeTime, packet.receivedTime);
        }

        std::string yellowname;
        std::string bluename;

        if (_context.game_state.blueTeam) {
            bluename = _context.game_state.OurInfo.name;
            yellowname = _context.game_state.TheirInfo.name;
        } else {
            yellowname = _context.game_state.OurInfo.name;
            bluename = _context.game_state.TheirInfo.name;
        }

        _context.state.logFrame->set_team_name_blue(bluename);
        _context.state.logFrame->set_team_name_yellow(yellowname);

        // Run high-level soccer logic
        _gameplayModule->run();

        // recalculates Field obstacles on every run through to account for
        // changing inset
        if (_gameplayModule->hasFieldEdgeInsetChanged()) {
            _gameplayModule->calculateFieldObstacles();
        }
        /// Collect global obstacles
        Geometry2d::ShapeSet globalObstacles =
            _gameplayModule->globalObstacles();
        Geometry2d::ShapeSet globalObstaclesWithGoalZones = globalObstacles;
        Geometry2d::ShapeSet goalZoneObstacles =
            _gameplayModule->goalZoneObstacles();
        globalObstaclesWithGoalZones.add(goalZoneObstacles);

        // Build a plan request for each robot.
        std::map<int, Planning::PlanRequest> requests;
        for (OurRobot* r : _context.state.self) {
            if (r != nullptr && r->visible()) {
                if (_context.game_state.state == GameState::Halt) {
                    r->setPath(nullptr);
                    continue;
                }

                // Visualize local obstacles
                for (auto& shape : r->localObstacles().shapes()) {
                    _context.debug_drawer.drawShape(shape, Qt::black,
                                                    "LocalObstacles");
                }

                auto& globalObstaclesForBot =
                    (r->shell() == _context.game_state.getGoalieId() ||
                     r->isPenaltyKicker || r->isBallPlacer)
                        ? globalObstacles
                        : globalObstaclesWithGoalZones;

                // create and visualize obstacles
                Geometry2d::ShapeSet staticObstacles =
                    r->collectStaticObstacles(
                        globalObstaclesForBot,
                        !(r->shell() == _context.game_state.getGoalieId() ||
                          r->isPenaltyKicker || r->isBallPlacer));

                std::vector<Planning::DynamicObstacle> dynamicObstacles =
                    r->collectDynamicObstacles();

                requests.emplace(
                    r->shell(),
                    Planning::PlanRequest(
                        &_context, Planning::MotionInstant(r->pos(), r->vel()),
                        r->motionCommand()->clone(), r->robotConstraints(),
                        std::move(r->angleFunctionPath().path),
                        std::move(staticObstacles), std::move(dynamicObstacles),
                        r->shell(), r->getPlanningPriority()));
            }
        }

        // Run path planner and set the path for each robot that was planned for
        auto pathsById = _pathPlanner->run(std::move(requests));
        for (auto& entry : pathsById) {
            OurRobot* r = _context.state.self[entry.first];
            auto& path = entry.second;
            path->draw(&_context.debug_drawer, Qt::magenta, "Planning");
            path->drawDebugText(&_context.debug_drawer);
            r->setPath(std::move(path));

            r->angleFunctionPath().angleFunction =
                angleFunctionForCommandType(r->rotationCommand());
        }

        // Visualize obstacles
        for (auto& shape : globalObstacles.shapes()) {
            _context.debug_drawer.drawShape(shape, Qt::black,
                                            "Global Obstacles");
        }

        _motionControl->run();
        _grSimCom->run();
        // Run all nodes in sequence
        // TODO(Kyle): This is dead code for now. Once everything is ported over
        // to modules we can delete the if (false), but for now we still have to
        // update things manually.

        ////////////////
        // Store logging information

        // Debug layers
        const QStringList& layers = _context.debug_drawer.debugLayers();
        for (const QString& str : layers) {
            _context.state.logFrame->add_debug_layers(str.toStdString());
        }

        // Add our robots data to the LogFrame
        for (OurRobot* r : _context.state.self) {
            if (r->visible()) {
                r->addStatusText();

                Packet::LogFrame::Robot* log =
                    _context.state.logFrame->add_self();
                *log->mutable_pos() = r->pos();
                *log->mutable_world_vel() = r->vel();
                *log->mutable_body_vel() =
                    r->vel().rotated(M_PI_2 - r->angle());
                //*log->mutable_cmd_body_vel() = r->
                // *log->mutable_cmd_vel() = r->cmd_vel;
                // log->set_cmd_w(r->cmd_w);
                log->set_shell(static_cast<float>(r->shell()));
                log->set_angle(static_cast<float>(r->angle()));
                auto radioRx = r->radioRx();
                if (radioRx.has_kicker_voltage()) {
                    log->set_kicker_voltage(radioRx.kicker_voltage());
                }

                if (radioRx.has_kicker_status()) {
                    log->set_charged((radioRx.kicker_status() & 0x01) != 0u);
                    log->set_kicker_works((radioRx.kicker_status() & 0x90) ==
                                          0u);
                }

                if (radioRx.has_ball_sense_status()) {
                    log->set_ball_sense_status(radioRx.ball_sense_status());
                }

                if (radioRx.has_battery()) {
                    log->set_battery_voltage(radioRx.battery());
                }

                log->mutable_motor_status()->Clear();
                log->mutable_motor_status()->MergeFrom(radioRx.motor_status());

                if (radioRx.has_quaternion()) {
                    log->mutable_quaternion()->Clear();
                    log->mutable_quaternion()->MergeFrom(radioRx.quaternion());
                } else {
                    log->clear_quaternion();
                }

                for (const Packet::DebugText& t : r->robotText) {
                    log->add_text()->CopyFrom(t);
                }
            }
        }

        // Opponent robots
        for (OpponentRobot* r : _context.state.opp) {
            if (r->visible()) {
                Packet::LogFrame::Robot* log =
                    _context.state.logFrame->add_opp();
                *log->mutable_pos() = r->pos();
                log->set_shell(r->shell());
                log->set_angle(r->angle());
                *log->mutable_world_vel() = r->vel();
                *log->mutable_body_vel() =
                    r->vel().rotated(2 * M_PI - r->angle());
            }
        }

        // Ball
        if (_context.state.ball.valid) {
            Packet::LogFrame::Ball* log =
                _context.state.logFrame->mutable_ball();
            *log->mutable_pos() = _context.state.ball.pos;
            *log->mutable_vel() = _context.state.ball.vel;
        }

        ////////////////
        // Outputs

        if (_context.game_state.halt()) {
            stopRobots();
        }
        updateIntentActive();
        _manual_control_node->run();

        // Write to the log unless we are viewing logs or main window is paused
        if (_readLogFile.empty() && !_context.game_settings.paused) {
            _logger.addFrame(_context.state.logFrame);
        }

        // Store processing loop status
        _statusMutex.lock();
        _status = curStatus;
        _statusMutex.unlock();

        // Processor Initialization Completed
        _initialized = true;

        ////////////////
        // Timing

        auto endTime = RJ::now();
        auto timeLapse = endTime - startTime;
        if (timeLapse < _framePeriod) {
            ::usleep(RJ::numMicroseconds(_framePeriod - timeLapse));
        } else {
            //   printf("Processor took too long: %d us\n", lastFrameTime);
        }
    }
}

void Processor::stopRobots() {
    for (OurRobot* r : _context.state.self) {
        RobotIntent& intent = _context.robot_intents[r->shell()];
        MotionSetpoint& setpoint = _context.motion_setpoints[r->shell()];

        setpoint.clear();
        intent.dvelocity = 0;
        intent.kcstrength = 0;
        intent.shoot_mode = RobotIntent::ShootMode::KICK;
        intent.trigger_mode = RobotIntent::TriggerMode::STAND_DOWN;
        intent.song = RobotIntent::Song::STOP;
    }
}

void Processor::updateIntentActive() {
    // Intent is active if it's being joystick controlled, or
    // if it's visible.
    for (OurRobot* r : _context.state.self) {
        RobotIntent& intent = _context.robot_intents[r->shell()];
        intent.is_active = r->isJoystickControlled() || r->visible();
    }
}

void Processor::recalculateWorldToTeamTransform() {
    _worldToTeam = Geometry2d::TransformMatrix::translate(
        0, Field_Dimensions::Current_Dimensions.Length() / 2.0f);
    _worldToTeam *= Geometry2d::TransformMatrix::rotate(_teamAngle);
}

void Processor::setFieldDimensions(const Field_Dimensions& dims) {
    *currentDimensions = dims;
    recalculateWorldToTeamTransform();
    _gameplayModule->calculateFieldObstacles();
    _gameplayModule->updateFieldDimensions();
}

bool Processor::isRadioOpen() const { return _radio->isOpen(); }
bool Processor::isInitialized() const { return _initialized; }

void Processor::updateOrientation() {
    if (_context.game_settings.defendPlusX) {
        _teamAngle = -M_PI_2;
    } else {
        _teamAngle = M_PI_2;
    }

    recalculateWorldToTeamTransform();
}
