/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2015, PickNik LLC
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the PickNik LLC nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Dave Coleman <dave@dav.ee>
   Desc:   Interface between MoveIt! execution tools and MoveItManipulation

   **Developer Notes**

   Methods for publishing commands:

   TrajectoryExecutionInterface using MoveItControllerMananger:
   moveit_simple_controller_manager   average: 0.00450083
   moveit_ros_control_interface       average: 0.222877 TODO this has gotten faster
   Direct publishing on ROS topic:    average: 0.00184441  (59% faster)
*/

// C++
#include <string>

// MoveItManipulation
#include <moveit_boilerplate/execution_interface.h>

// MoveIt
#include <moveit/trajectory_execution_manager/trajectory_execution_manager.h>

// Parameter loading
#include <rosparam_shortcuts/rosparam_shortcuts.h>

// ros_control
#include <controller_manager_msgs/ListControllers.h>

namespace moveit_boilerplate
{
ExecutionInterface::ExecutionInterface(psm::PlanningSceneMonitorPtr planning_scene_monitor,
                                       mvt::MoveItVisualToolsPtr visual_tools)
  : nh_("~")
  , planning_scene_monitor_(planning_scene_monitor)
  , visual_tools_(visual_tools)
{
  // Create initial robot state
  {
    psm::LockedPlanningSceneRO scene(planning_scene_monitor_);  // Lock planning scene
    current_state_.reset(new moveit::core::RobotState(scene->getCurrentState()));
  }  // end scoped pointer of locked planning scene

  // Debug tools for visualizing in Rviz
  if (!visual_tools_)
    loadVisualTools();

  std::string joint_trajectory_topic;
  std::string cartesian_command_topic;
  std::string command_mode;

  // Load rosparams
  ros::NodeHandle rpnh(nh_, name_);
  std::size_t error = 0;
  error += !rosparam_shortcuts::get(name_, rpnh, "command_mode", command_mode);
  error += !rosparam_shortcuts::get(name_, rpnh, "joint_trajectory_topic", joint_trajectory_topic);
  error += !rosparam_shortcuts::get(name_, rpnh, "cartesian_command_topic", cartesian_command_topic);
  error += !rosparam_shortcuts::get(name_, rpnh, "save_traj_to_file_path", save_traj_to_file_path_);
  error += !rosparam_shortcuts::get(name_, rpnh, "save_traj_to_file", save_traj_to_file_);
  error += !rosparam_shortcuts::get(name_, rpnh, "visualize_trajectory_line", visualize_trajectory_line_);
  error += !rosparam_shortcuts::get(name_, rpnh, "visualize_trajectory_path", visualize_trajectory_path_);
  error += !rosparam_shortcuts::get(name_, rpnh, "check_for_waypoint_jumps", check_for_waypoint_jumps_);
  rosparam_shortcuts::shutdownIfError(name_, error);

  // Choose mode from string
  joint_command_mode_ = stringToJointCommandMode(command_mode);

  // Load the proper joint command execution method
  const std::size_t queue_size = 1;
  switch (joint_command_mode_)
  {
    case JOINT_EXECUTION_MANAGER:
      ROS_DEBUG_STREAM_NAMED(name_, "Connecting to trajectory execution manager");
      if (!trajectory_execution_manager_)
      {
        trajectory_execution_manager_.reset(new trajectory_execution_manager::TrajectoryExecutionManager(
            planning_scene_monitor_->getRobotModel(), planning_scene_monitor_->getStateMonitor()));
      }
      break;
    case JOINT_PUBLISHER:
      ROS_DEBUG_STREAM_NAMED(name_, "Connecting to joint publisher on topic " << joint_trajectory_topic);
      // Alternative method to sending trajectories than trajectory_execution_manager
      joint_trajectory_pub_ = nh_.advertise<trajectory_msgs::JointTrajectory>(joint_trajectory_topic, queue_size);
      break;
    default:
      ROS_ERROR_STREAM_NAMED(name_, "Unknown control mode");
  }

  // TODO(davetcoleman): check if publishers have connected yet

  // Load cartesian control method
  ROS_DEBUG_STREAM_NAMED(name_, "Connecting to cartesian publisher on topic " << cartesian_command_topic);
  cartesian_command_pub_ = nh_.advertise<geometry_msgs::PoseStamped>(cartesian_command_topic, queue_size);

  // Set the world frame for cartesian control
  pose_stamped_msg_.header.frame_id = "world";

  ROS_INFO_STREAM_NAMED(name_, "ExecutionInterface Ready.");
}

bool ExecutionInterface::executePose(const Eigen::Affine3d &pose)
{
  pose_stamped_msg_.header.stamp = ros::Time::now();
  visual_tools_->convertPoseSafe(pose, pose_stamped_msg_.pose);
  cartesian_command_pub_.publish(pose_stamped_msg_);
  return true;
}

bool ExecutionInterface::executeTrajectory(const robot_trajectory::RobotTrajectoryPtr robot_trajectory, JointModelGroup *jmg,
                                           bool wait_for_execution)
{
  // Convert trajectory to a message
  moveit_msgs::RobotTrajectory trajectory_msg;
  robot_trajectory->getRobotTrajectoryMsg(trajectory_msg);

  trajectory_msgs::JointTrajectory &trajectory = trajectory_msg.joint_trajectory;

  // Debug
  ROS_DEBUG_STREAM_NAMED(name_ + ".summary", "Executing trajectory with " << trajectory.points.size() << " waypoints");
  ROS_DEBUG_STREAM_NAMED(name_ + ".trajectory", "Publishing:\n" << trajectory_msg);

  // Error check
  if (trajectory.points.empty())
  {
    ROS_ERROR_STREAM_NAMED(name_, "No points to execute, aborting trajectory execution");
    return false;
  }

  // Optionally Remove velocity and acceleration from trajectories for testing
  bool clear_dynamics = false;
  if (clear_dynamics)
  {
    ROS_WARN_STREAM_NAMED(name_, "clearing dynamics");
    for (std::size_t i = 0; i < trajectory.points.size(); ++i)
    {
      trajectory.points[i].velocities.clear();
      trajectory.points[i].accelerations.clear();
    }
  }

  // Optionally save to file
  if (save_traj_to_file_)
  {
    saveTrajectory(trajectory_msg, jmg->getName() + "_moveit_trajectory_" +
                                       boost::lexical_cast<std::string>(trajectory_filename_count_++) + ".csv",
                   save_traj_to_file_path_);
  }

  // Optionally visualize the hand/wrist path in Rviz
  if (visualize_trajectory_line_)
  {
    if (trajectory.points.size() > 1 && !jmg->isEndEffector())
    {
      visual_tools_->deleteAllMarkers();
      visual_tools_->publishTrajectoryLine(robot_trajectory, jmg, rvt::LIME_GREEN);
      visual_tools_->trigger();
    }
    else
      ROS_WARN_STREAM_NAMED(name_, "Not visualizing path because trajectory only has "
                                       << trajectory.points.size() << " points or because is end effector");
  }

  // Optionally visualize trajectory in Rviz
  if (visualize_trajectory_path_)
  {
    const bool wait_for_trajetory = false;
    visual_tools_->publishTrajectoryPath(trajectory_msg, getCurrentState(), wait_for_trajetory);
  }

  // Optionally check for errors in trajectory
  if (check_for_waypoint_jumps_)
    checkForWaypointJumps(trajectory);

  // Confirm trajectory before continuing
  if (!visual_tools_->getRemoteControl()->getFullAutonomous())
  {
    visual_tools_->getRemoteControl()->waitForNextFullStep("execute trajectory");
    ROS_INFO_STREAM_NAMED(name_, "Remote confirmed trajectory execution.");
  }

  // Send new trajectory
  switch (joint_command_mode_)
  {
    case JOINT_EXECUTION_MANAGER:
      ROS_INFO_STREAM_NAMED(name_, "Joint execution manager");

      // Reset trajectory manager
      trajectory_execution_manager_->clear();

      if (trajectory_execution_manager_->pushAndExecute(trajectory_msg))
      {
        // Optionally wait for completion
        if (wait_for_execution)
        {
          ros::Duration(trajectory.points.back().time_from_start).sleep();
          //waitForExecution();
        }
        else
          ROS_DEBUG_STREAM_NAMED("exceution_interface", "Not waiting for execution to finish");
      }
      else
      {
        visual_tools_->getRemoteControl()->waitForNextFullStep("after execute trajectory 2");
        ROS_ERROR_STREAM_NAMED(name_, "Failed to execute trajectory");
        return false;
      }
      break;
    case JOINT_PUBLISHER:
      ROS_INFO_STREAM_NAMED(name_, "Joint publisher");
      joint_trajectory_pub_.publish(trajectory);

      if (wait_for_execution)
      {
        ROS_INFO_STREAM_NAMED(name_, "Sleeping while trajectory executes");
        ros::Duration(trajectory.points.back().time_from_start).sleep();
      }
      break;
    default:
      ROS_ERROR_STREAM_NAMED(name_, "Unknown control mode");
  }

  return true;
}

bool ExecutionInterface::stopExecution()
{
  trajectory_msgs::JointTrajectory blank_trajectory;

  switch (joint_command_mode_)
  {
    case JOINT_EXECUTION_MANAGER:
      ROS_ERROR_STREAM_NAMED(name_, "Joint execution manager stopping not implemented");
      // TODO(davetcoleman) Just send a blank trajectory
      break;
    case JOINT_PUBLISHER:
      // Just send a blank trajectory
      ROS_DEBUG_STREAM_NAMED(name_, "Recieved stop motion command");
      joint_trajectory_pub_.publish(blank_trajectory);
      return true;
      break;
    default:
      ROS_ERROR_STREAM_NAMED(name_, "Unknown control mode");
  }
  return false;
}

bool ExecutionInterface::waitForExecution()
{
  if (joint_command_mode_ != JOINT_EXECUTION_MANAGER)
  {
    ROS_WARN_STREAM_NAMED(name_, "Not waiting for execution because not in execution_manager mode");
    return true;
  }

  ROS_DEBUG_STREAM_NAMED(name_, "Waiting for executing trajectory to finish");

  //visual_tools_->getRemoteControl()->waitForNextFullStep("after execute trajectory - TODO, fix this bug");


  // wait for the trajectory to complete
  moveit_controller_manager::ExecutionStatus execution_status = trajectory_execution_manager_->waitForExecution();


  if (execution_status == moveit_controller_manager::ExecutionStatus::SUCCEEDED)
  {
    ROS_DEBUG_STREAM_NAMED(name_, "Trajectory execution succeeded");
    return true;
  }

  if (execution_status == moveit_controller_manager::ExecutionStatus::PREEMPTED)
    ROS_INFO_STREAM_NAMED(name_, "Trajectory execution preempted");
  else if (execution_status == moveit_controller_manager::ExecutionStatus::TIMED_OUT)
    ROS_ERROR_STREAM_NAMED(name_, "Trajectory execution timed out");
  else
    ROS_ERROR_STREAM_NAMED(name_, "Trajectory execution control failed");

  return false;
}

void ExecutionInterface::checkForWaypointJumps(const trajectory_msgs::JointTrajectory &trajectory)
{
  // Debug: check for errors in trajectory
  static const double MAX_TIME_STEP_SEC = 4.0;
  ros::Duration max_time_step(MAX_TIME_STEP_SEC);
  static const double WARN_TIME_STEP_SEC = 3.0;
  ros::Duration warn_time_step(WARN_TIME_STEP_SEC);
  ros::Duration diff;
  for (std::size_t i = 0; i < trajectory.points.size() - 1; ++i)
  {
    diff = (trajectory.points[i + 1].time_from_start - trajectory.points[i].time_from_start);
    if (diff > max_time_step)
    {
      ROS_ERROR_STREAM_NAMED(
          name_, "Max time step between points exceeded, likely because of wrap around/IK bug. Point " << i);
      std::cout << "First time: " << trajectory.points[i].time_from_start.toSec() << std::endl;
      std::cout << "Next time: " << trajectory.points[i + 1].time_from_start.toSec() << std::endl;
      std::cout << "Diff time: " << diff.toSec() << std::endl;
      std::cout << "-------------------------------------------------------" << std::endl;
      std::cout << std::endl;

      visual_tools_->getRemoteControl()->setAutonomous(false);
      visual_tools_->getRemoteControl()->setFullAutonomous(false);

      // return false;
    }
    else if (diff > warn_time_step)
    {
      ROS_WARN_STREAM_NAMED(
          name_, "Warn time step between points exceeded, likely because of wrap around/IK bug. Point " << i);
      std::cout << "First time: " << trajectory.points[i].time_from_start.toSec() << std::endl;
      std::cout << "Next time: " << trajectory.points[i + 1].time_from_start.toSec() << std::endl;
      std::cout << "Diff time: " << diff.toSec() << std::endl;
      std::cout << "-------------------------------------------------------" << std::endl;
      std::cout << std::endl;
    }
  }
}

bool ExecutionInterface::checkExecutionManager()
{
  ROS_INFO_STREAM_NAMED(name_, "Checking that execution manager is loaded.");

  /*
    JointModelGroup *arm_jmg = config_->dual_arm_ ? config_->both_arms_ : config_->right_arm_;

    // Get the controllers List
    std::vector<std::string> controller_list;
    trajectory_execution_manager_->getControllerManager()->getControllersList(controller_list);

    // Check active controllers are running
    if (!trajectory_execution_manager_->ensureActiveControllersForGroup(arm_jmg->getName()))
    {
    ROS_ERROR_STREAM_NAMED(name_,
    "Group '" << arm_jmg->getName() << "' does not have controllers loaded");
    std::cout << "Available controllers: " << std::endl;
    std::copy(controller_list.begin(), controller_list.end(),
    std::ostream_iterator<std::string>(std::cout, "\n"));
    return false;
    }

    // Check active controllers are running
    if (!trajectory_execution_manager_->ensureActiveControllers(controller_list))
    {
    ROS_ERROR_STREAM_NAMED(name_, "Robot does not have the desired controllers "
    "active");
    return false;
    }
  */

  return true;
}

bool ExecutionInterface::checkTrajectoryController(ros::ServiceClient &service_client, const std::string &hardware_name,
                                                   bool has_ee)
{
  // Try to communicate with controller manager
  controller_manager_msgs::ListControllers service;
  ROS_DEBUG_STREAM_NAMED(name_, "Calling list controllers service client");
  if (!service_client.call(service))
  {
    ROS_ERROR_STREAM_THROTTLE_NAMED(2, name_, "Unable to check if controllers for "
                                                  << hardware_name << " are loaded, failing. Using nh "
                                                                      "namespace " << nh_.getNamespace()
                                                  << ". Service response: " << service.response);
    return false;
  }

  std::string control_type = "position";  // fake_execution_ ? "position" : "velocity";

  // Check if proper controller is running
  bool found_main_controller = false;
  bool found_ee_controller = false;

  for (std::size_t i = 0; i < service.response.controller.size(); ++i)
  {
    if (service.response.controller[i].name == control_type + "_trajectory_controller")
    {
      found_main_controller = true;
      if (service.response.controller[i].state != "running")
      {
        ROS_WARN_STREAM_THROTTLE_NAMED(2, name_, "Controller for " << hardware_name << " is in manual mode");
        return false;
      }
    }
    if (service.response.controller[i].name == "ee_" + control_type + "_trajectory_controller")
    {
      found_ee_controller = true;
      if (service.response.controller[i].state != "running")
      {
        ROS_WARN_STREAM_THROTTLE_NAMED(2, name_, "Controller for " << hardware_name << " is in manual mode");
        return false;
      }
    }
  }

  if (has_ee && !found_ee_controller)
  {
    ROS_ERROR_STREAM_THROTTLE_NAMED(2, name_, "No end effector controller found for "
                                                  << hardware_name << ". Controllers are: " << service.response);
    return false;
  }
  if (!found_main_controller)
  {
    ROS_ERROR_STREAM_THROTTLE_NAMED(2, name_, "No main controller found for "
                                                  << hardware_name << ". Controllers are: " << service.response);
    return false;
  }

  return true;
}

bool ExecutionInterface::saveTrajectory(const moveit_msgs::RobotTrajectory &trajectory_msg,
                                        const std::string &file_name, const std::string &save_traj_to_file_path)
{
  const std::string name = "execution_interface"; // this function is static and can't use member variable name_
  const trajectory_msgs::JointTrajectory &joint_trajectory = trajectory_msg.joint_trajectory;

  // Error check
  if (!joint_trajectory.points.size() || !joint_trajectory.points[0].positions.size())
  {
    ROS_ERROR_STREAM_NAMED(name, "No trajectory points available to save");
    return false;
  }

  std::string file_path = save_traj_to_file_path + "/" + file_name;

  std::ofstream output_file;
  output_file.open(file_path.c_str());

  // Output header -------------------------------------------------------
  output_file << "time_from_start,";
  for (std::size_t j = 0; j < joint_trajectory.joint_names.size(); ++j)
  {
    output_file << joint_trajectory.joint_names[j] << "_pos," << joint_trajectory.joint_names[j] << "_vel,"
                << joint_trajectory.joint_names[j] << "_acc,";
  }
  output_file << std::endl;

  // Output data ------------------------------------------------------
  for (std::size_t i = 0; i < joint_trajectory.points.size(); ++i)
  {
    // Timestamp
    output_file.precision(20);
    // output_file << (joint_trajectory.header.stamp + joint_trajectory.points[i].time_from_start).toSec() << ",";
    output_file << (joint_trajectory.points[i].time_from_start).toSec() << ",";
    output_file.precision(5);
    // Output entire trajectory to single line
    for (std::size_t j = 0; j < joint_trajectory.points[i].positions.size(); ++j)
    {
      // Output State
      output_file << joint_trajectory.points[i].positions[j] << ",";
      if (joint_trajectory.points[i].velocities.size())
        output_file << joint_trajectory.points[i].velocities[j] << ",";
      else
        output_file << 0 << ",";
      if (joint_trajectory.points[i].accelerations.size())
        output_file << joint_trajectory.points[i].accelerations[j] << ",";
      else
        output_file << 0 << ",";
    }

    output_file << std::endl;
  }
  output_file.close();
  ROS_INFO_STREAM_NAMED(name, "Saved trajectory to file " << file_name);
  return true;
}

moveit::core::RobotStatePtr ExecutionInterface::getCurrentState()
{
  // Get the real current state
  psm::LockedPlanningSceneRO scene(planning_scene_monitor_);  // Lock planning scene
  (*current_state_) = scene->getCurrentState();
  return current_state_;
}

void ExecutionInterface::loadVisualTools()
{
  // TODO(davetcoleman): should this be a duplicate execution interface?
  visual_tools_.reset(new mvt::MoveItVisualTools(planning_scene_monitor_->getRobotModel()->getModelFrame(),
                                                 nh_.getNamespace() + "/markers", planning_scene_monitor_));

  visual_tools_->loadRobotStatePub(nh_.getNamespace() + "/robot_state");
  visual_tools_->loadTrajectoryPub(nh_.getNamespace() + "/display_trajectory");
  visual_tools_->loadMarkerPub();
  visual_tools_->setAlpha(0.8);
  visual_tools_->deleteAllMarkers();  // clear all old markers
  visual_tools_->setManualSceneUpdating(true);
  visual_tools_->hideRobot();  // show that things have been reset
}

}  // namespace moveit_boilerplate
