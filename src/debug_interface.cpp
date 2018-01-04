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
   Desc:   Contains all hooks for debug interface
*/

// C++
#include <string>

#include <moveit_boilerplate/debug_interface.h>
#include <moveit/macros/console_colors.h>

// Parameter loading
#include <rosparam_shortcuts/rosparam_shortcuts.h>

namespace moveit_boilerplate
{

/**
 * \brief Constructor
 */
DebugInterface::DebugInterface(ros::NodeHandle nh)
  : nh_(nh)
  , name_("debug_interface")
  , is_waiting_(false)
  , next_step_ready_(false)
  , stop_(false)
{
  bool autonomous = false;
  bool full_autonomous = false;
  // Load rosparams
  ros::NodeHandle rpnh(nh_, name_);
  std::size_t error = 0;
  error += !rosparam_shortcuts::get(name_, rpnh, "autonomous", autonomous);
  error += !rosparam_shortcuts::get(name_, rpnh, "full_autonomous", full_autonomous);
  DebugInterface(nh_, autonomous, full_autonomous);
  ROS_ERROR("Please don't initiate ExecutionInterface in this way!");
}

DebugInterface::DebugInterface(ros::NodeHandle nh,
  bool autonomous, bool full_autonomous)
  : nh_(nh)
  , name_("debug_interface")
  , is_waiting_(false)
  , next_step_ready_(false)
  , autonomous_(autonomous)
  , full_autonomous_(full_autonomous)
  , stop_(false)
{

  // Warnings
  if (autonomous_)
    ROS_INFO_STREAM_NAMED(name_, "In autonomous mode - will only stop at breakpoints");
  if (full_autonomous_)
    ROS_INFO_STREAM_NAMED(name_, "In FULL autonomous mode - will ignore breakpoints");

  ROS_INFO_STREAM_NAMED(name_, "DebugInterface Ready.");
}

bool DebugInterface::setReadyForNextStep()
{
  stop_ = false;

  if (is_waiting_)
  {
    next_step_ready_ = true;
  }
  return true;
}

void DebugInterface::setAutonomous(bool autonomous)
{
  autonomous_ = autonomous;
  stop_ = false;
}

void DebugInterface::setFullAutonomous(bool autonomous)
{
  full_autonomous_ = autonomous;
  autonomous_ = autonomous;
  stop_ = false;
}

void DebugInterface::setStop(bool stop)
{
  stop_ = stop;
  if (stop)
  {
    autonomous_ = false;
    full_autonomous_ = false;
  }
}

bool DebugInterface::getStop()
{
  return stop_;
}
bool DebugInterface::getAutonomous()
{
  return autonomous_;
}
bool DebugInterface::getFullAutonomous()
{
  return full_autonomous_;
}
bool DebugInterface::waitForNextStep(const std::string& caption)
{
  // Check if we really need to wait
  if (!(!next_step_ready_ && !autonomous_ && ros::ok()))
    return true;

  // Show message
  std::cout << std::endl
            << std::endl;
  std::cout << MOVEIT_CONSOLE_COLOR_CYAN << "Waiting to " << caption << MOVEIT_CONSOLE_COLOR_RESET << std::endl;

  is_waiting_ = true;
  // Wait until next step is ready
  while (!next_step_ready_ && !autonomous_ && ros::ok())
  {
    ros::Duration(0.25).sleep();
    ros::spinOnce();
  }
  if (!ros::ok())
    return false;
  next_step_ready_ = false;
  is_waiting_ = false;
  return true;
}

bool DebugInterface::waitForNextFullStep(const std::string& caption)
{
  // Check if we really need to wait
  if (!(!next_step_ready_ && !full_autonomous_ && ros::ok()))
    return true;

  // Show message
  std::cout << MOVEIT_CONSOLE_COLOR_CYAN << "Waiting to " << caption << MOVEIT_CONSOLE_COLOR_RESET << std::endl;

  is_waiting_ = true;
  // Wait until next step is ready
  while (!next_step_ready_ && !full_autonomous_ && ros::ok())
  {
    ros::Duration(0.25).sleep();
    ros::spinOnce();
  }
  if (!ros::ok())
    return false;
  next_step_ready_ = false;
  is_waiting_ = false;
  return true;
}

}  // namespace moveit_boilerplate