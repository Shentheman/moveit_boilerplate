cmake_minimum_required(VERSION 2.8.3)
project(moveit_boilerplate)

# C++ 11
add_compile_options(-std=c++11)

find_package(catkin REQUIRED COMPONENTS
  moveit_core
  moveit_visual_tools
  cmake_modules
  controller_manager_msgs
  rosparam_shortcuts
  roslint
  tf_conversions
)

find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED)

catkin_package(
  CATKIN_DEPENDS
    moveit_core
    moveit_visual_tools
    controller_manager_msgs
    rosparam_shortcuts
  INCLUDE_DIRS
    include
  LIBRARIES
    ${PROJECT_NAME}_fix_state_bounds
    ${PROJECT_NAME}_execution_interface
    ${PROJECT_NAME}_planning_interface
    ${PROJECT_NAME}_trajectory_io
    ${PROJECT_NAME}_moveit_base
    ${PROJECT_NAME}_get_planning_scene_service
    ${PROJECT_NAME}
)

###########
## Build ##
###########
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

include_directories(SYSTEM
  ${Boost_INCLUDE_DIR}
  ${EIGEN3_INCLUDE_DIRS}
)

# Fix_state_bounds library
add_library(${PROJECT_NAME}_fix_state_bounds
  src/fix_state_bounds.cpp
)
target_link_libraries(${PROJECT_NAME}_fix_state_bounds
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# get planning scene library
add_library(${PROJECT_NAME}_get_planning_scene_service
  src/get_planning_scene_service.cpp
)
target_link_libraries(${PROJECT_NAME}_get_planning_scene_service
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# execution interface library
add_library(${PROJECT_NAME}_execution_interface
  src/execution_interface.cpp
)
target_link_libraries(${PROJECT_NAME}_execution_interface
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# Joint-space path planners
add_library(${PROJECT_NAME}_planning_interface
  src/planning_interface.cpp
)
target_link_libraries(${PROJECT_NAME}_planning_interface
  ${PROJECT_NAME}_execution_interface
  ${PROJECT_NAME}_fix_state_bounds
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# Trajectory input/output
add_library(${PROJECT_NAME}_trajectory_io
  src/trajectory_io.cpp
)
target_link_libraries(${PROJECT_NAME}_trajectory_io
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# Simplified reusable class for MoveIt!
add_library(${PROJECT_NAME}_moveit_base
  src/moveit_base.cpp
)
target_link_libraries(${PROJECT_NAME}_moveit_base
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

# Reusable class for MoveIt!
# TODO(davetcoleman) depend on ${PROJECT_NAME}_moveit_base
add_library(${PROJECT_NAME}
  src/boilerplate.cpp
)
target_link_libraries(${PROJECT_NAME}
  ${PROJECT_NAME}_execution_interface
  ${PROJECT_NAME}_planning_interface
  ${PROJECT_NAME}_get_planning_scene_service
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

add_executable(${PROJECT_NAME}_example src/example.cpp)
target_link_libraries(${PROJECT_NAME}_example
  ${PROJECT_NAME}_moveit_base
  ${catkin_LIBRARIES}
  ${Boost_LIBRARIES}
)

#############
## Testing ##
#############

## Test for correct C++ source code
roslint_cpp()

#############
## Install ##
#############

## Mark executables and/or libraries for installation
install(TARGETS
    ${PROJECT_NAME}_fix_state_bounds
    ${PROJECT_NAME}_execution_interface
    ${PROJECT_NAME}_planning_interface
    ${PROJECT_NAME}_trajectory_io
    ${PROJECT_NAME}_moveit_base
    ${PROJECT_NAME}_get_planning_scene_service
    ${PROJECT_NAME}
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

# Mark cpp header files for installation
install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.h"
  PATTERN ".svn" EXCLUDE
)
