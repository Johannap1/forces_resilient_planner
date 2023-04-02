cmake_minimum_required(VERSION 2.8.3)
project(map_generator)

set(CMAKE_CXX_FLAGS "-std=c++14 ${CMAKE_CXX_FLAGS} -O3 -Wall")

find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  std_msgs
  geometry_msgs
  pcl_conversions
  pcl_ros
)
find_package(PCL 1.7 REQUIRED)
find_package(Eigen3 REQUIRED)

catkin_package()

include_directories(
# include
  ${catkin_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIR}
  ${PCL_INCLUDE_DIRS}
)

add_executable (random_forest src/random_forest_sensing.cpp )
target_link_libraries(random_forest
  ${catkin_LIBRARIES}
  ${PCL_LIBRARIES})
  
add_dependencies(random_forest
   ${catkin_EXPORTED_TARGETS}
)
