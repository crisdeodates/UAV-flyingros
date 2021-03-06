/*
 * test_node.cpp
 *
 * The test node helps to verify the algorithm by connecting the Pixhawk
 *  and 6 lasers and printing at 1Hz:
 *  - Initial position for each laser
 *  - Initial orientation for each laser
 *  - Actual position for each laser
 *  - Actual orientation for each laser
 *  - Actual orientation of the Pixhawk (roll, pitch, yaw)
 *
 * This file is a part of FlyingROS
 *
 * FlyingROS free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FlyingROS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FlyingROS.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Software created by Alexis Paques and Nabil Nehri for the UCL
 * in a Drone-Based Additive Manufacturing of Architectural Structures
 * project financed by the MIT Seed Fund
 *
 * Copyright (c) Alexis Paques 2016
 *
 */

#include <typeinfo>
#include <iostream>
#include "ros/ros.h"
#include <tf/transform_datatypes.h>
#include <geometry_msgs/Pose.h>
#include <sensor_msgs/Imu.h>
#include "flyingros_pose/laser_algorithm_functions.h"
#include "flyingros_msgs/MultiEcho.h"
#include <ros/console.h> 
#include <cmath>

using namespace std;
using namespace flyingros_pose;

Laser lasers[6];
tf::Quaternion q_imu(0,0,0,1);
ros::Publisher position_publisher;
double laser_measures_raw[6] = {0,0,0,0,0,0};
double laser_measures_corrected[6] = {0,0,0,0,0,0};
tf::Vector3 projected_position[6];
tf::Vector3 projected_orientation[6];
double projected_target[6] = {0,0,0,0,0,0};
double projected_yaw[8] = {0,0,0,0,0,0,0,0};
tf::Vector3 targets[6];

void callback_laser_raw(const flyingros_msgs::MultiEcho::ConstPtr& msg){
  double roll, pitch, yaw;

  // Correct offset
  double measures[6];
  for(int i = 0; i < 6; i++){
      laser_measures_raw[i] = double(msg->measures[i])/100.0;
      // measures are in cm and have an offset
      measures[i] = double(msg->measures[i])/100.0;
  }

  // Get pitch yaw roll from the PixHawk
  tf::Matrix3x3 m(q_imu);
  m.getRPY(roll, pitch, yaw);
  tf::Quaternion q_zero = tf::createQuaternionFromRPY(roll, pitch, 0);

  // Get yaw
  tf::Vector3 targetx1 = lasers[0].project(measures[0], q_zero);
  tf::Vector3 targetx2 = lasers[1].project(measures[1], q_zero);
  double yaw_x = getYawFromTargets(targetx2, targetx1,0,1);

  tf::Vector3 targety1 = lasers[2].project(measures[2], q_zero);
  tf::Vector3 targety2 = lasers[3].project(measures[3], q_zero);
  double yaw_y = -getYawFromTargets(targety2, targety1,1,0); 
  // As the configuration is the opposite as the X configuration, we need to 
  // take the opposite.

  // Get position
  tf::Quaternion q_correct = tf::createQuaternionFromRPY(roll, pitch, yaw_x);

  for(int i = 0; i < 6; i ++){
    laser_measures_corrected[i] = measures[i] - lasers[i].offset;
    tf::Vector3 r_orientation = tf::quatRotate(q_zero, lasers[i].orientation);
    tf::Vector3 r_position = tf::quatRotate(q_zero, lasers[i].position);
    projected_orientation[i] = r_orientation;
    projected_position[i] = r_position;
    targets[i] = laser_measures_corrected[i]*r_orientation + r_position;
  }

  projected_target[0] = abs(targets[0].x());
  projected_target[1] = abs(targets[1].x());
  projected_target[2] = abs(targets[2].y());
  projected_target[3] = abs(targets[3].y());
  projected_target[4] = abs(targets[4].z());
  projected_target[5] = abs(targets[5].z());

  projected_yaw[0] = getYawFromTargets(targets[0], targets[1], 0, 1); // in X indices: atan2(x,y) Good in our configuration
  projected_yaw[1] = getYawFromTargets(targets[0], targets[1], 1, 0); // in X indices: atan2(x,y)
  projected_yaw[2] = getYawFromTargets(targets[1], targets[0], 0, 1); // in X indices: atan2(x,y)
  projected_yaw[3] = getYawFromTargets(targets[1], targets[0], 1, 0); // in X indices: atan2(x,y)
  projected_yaw[4] = getYawFromTargets(targets[2], targets[3], 1, 0); // in X indices: atan2(y,x) 
  projected_yaw[5] = getYawFromTargets(targets[2], targets[3], 0, 1); // in X indices: atan2(y,x) 
  projected_yaw[6] = getYawFromTargets(targets[3], targets[2], 1, 0); // in X indices: atan2(y,x) Good in our configuration
  projected_yaw[7] = getYawFromTargets(targets[3], targets[2], 0, 1); // in X indices: atan2(y,x) 

  for(int i = 0; i < 8; i++){
    projected_yaw[i] = projected_yaw[i]*180/3.14159265358979323846;
  }

  // publish
  geometry_msgs::Pose UAVPose;
  tf::quaternionTFToMsg(q_correct, UAVPose.orientation);
  UAVPose.position.x = -(targets[0].x() + targets[1].x())/2.0;
  UAVPose.position.y = -(targets[2].y() + targets[3].y())/2.0;
  UAVPose.position.z = -(targets[4].z() + targets[5].z())/2.0;
  position_publisher.publish(UAVPose);
}

void callback_imu(const sensor_msgs::Imu::ConstPtr& msg){
    tf::quaternionMsgToTF(msg->orientation, q_imu);
}

void reconfigure_lasers(){
    XmlRpc::XmlRpcValue offsetsList, positionsList, orientationsList;
    XmlRpc::XmlRpcValue p, v;
    int count;
    ros::param::get("/flyingros/lasers/count", count);
    ROS_ASSERT(count == 6);
    ros::param::get("/flyingros/lasers/offsets", offsetsList);
    ROS_ASSERT(offsetsList.getType() == XmlRpc::XmlRpcValue::TypeArray);
    ros::param::get("/flyingros/lasers/positions", positionsList);
    ROS_ASSERT(positionsList.getType() == XmlRpc::XmlRpcValue::TypeArray);
    ros::param::get("/flyingros/lasers/orientations", orientationsList);
    ROS_ASSERT(orientationsList.getType() == XmlRpc::XmlRpcValue::TypeArray);

    tf::Vector3 postition, orientation;
    double offset;
    for(int i = 0; i < count; i++){
        offset = double(offsetsList[i]);
        p = positionsList[i];
        v = orientationsList[i];
        ROS_ASSERT(p.getType() == XmlRpc::XmlRpcValue::TypeArray);
        ROS_ASSERT(v.getType() == XmlRpc::XmlRpcValue::TypeArray);

        postition.setX(double(p[0]));
        postition.setY(double(p[1]));
        postition.setZ(double(p[2]));
        orientation.setX(double(v[0]));
        orientation.setY(double(v[1]));
        orientation.setZ(double(v[2]));
        lasers[i].configure(postition, orientation, offset);
    }
}

void callback_print_data(){
  double roll, pitch, yaw;
  for(int i = 0; i<6; i++){
    std::cout << "Laser: " << i << "\n";
    std::cout << "Measure raw:" << laser_measures_raw[i] << "\t" << "deoffsetted: " << laser_measures_corrected[i] << "\n";
    std::cout << "Position       xyz: " << std::setprecision(3) << lasers[i].position.x() << " \t" << lasers[i].position.y() << " \t" << lasers[i].position.z() << "\n";
    std::cout << "Position_final xyz: " << std::setprecision(3) << projected_position[i].x() << " \t" << projected_position[i].y() << " \t" << projected_position[i].z() << "\n";
    std::cout << "Orientation    xyz: " << std::setprecision(3) << lasers[i].orientation.x() << " \t" << lasers[i].orientation.y() << " \t" << lasers[i].orientation.z() << "\n";
    std::cout << "Orientation_fi xyz: " << std::setprecision(3) << projected_orientation[i].x() << " \t" << projected_orientation[i].y() << " \t" << projected_orientation[i].z() << "\n";
    std::cout << "target         xyz: " << std::setprecision(3) << targets[i].x() << " \t" << targets[i].y() << " \t" << targets[i].z() << "\n";;
  }

  std::cout << "Quaternion xyzw: " << std::setprecision(3) << q_imu.x() << " \t"<< q_imu.y() << " \t" << q_imu.z() << " \t" << q_imu.w() << "\n";

  tf::Matrix3x3 m(q_imu);
  m.getRPY(roll, pitch, yaw);

  std::cout << "Rotation sxyz  : " << std::setprecision(3) << roll*180/3.14159265358979323846 << " \t"<< pitch*180/3.14159265358979323846 << " \t" << yaw*180/3.14159265358979323846 << "\n";


  std::cout << "Projected   xxyyzz: " << std::setprecision(3) << projected_target[0] << " \t"<< projected_target[1] << " \t" << projected_target[2] << " \t" << projected_target[3] << " \t" << projected_target[4] << " \t" << projected_target[5] << "\n";
  std::cout << "YAW  X,Y: " << std::setprecision(3) << projected_yaw[0] << " \t"<< projected_yaw[1] << " \t"<< projected_yaw[2] << " \t"<< projected_yaw[3] << " \t"<< projected_yaw[4] << " \t"<< projected_yaw[5] << " \t"<< projected_yaw[6] << " \t"<< projected_yaw[7] << "\n";
  std::cout << "--------- \n" << "\n";
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "laser_node_test");
  ros::NodeHandle nh;

  // Reconfigure laser values (from ROS parameters) before using them.
  reconfigure_lasers();

  std::string raw_laser_topic, position_pub_topic, imu_topic;
  ros::param::param<std::string>("laser_raw_topic", raw_laser_topic, "/flyingros/lasers/raw");
  ros::param::param<std::string>("laser_pose_topic", position_pub_topic, "/flyingros/lasers/pose");
  ros::param::param<std::string>("imu_topic", imu_topic, "/mavros/imu/data");

  ros::Subscriber raw_laser_sub = nh.subscribe(raw_laser_topic, 1, callback_laser_raw);
  ros::Subscriber imu_sub = nh.subscribe(imu_topic, 1, callback_imu);
  position_publisher = nh.advertise<geometry_msgs::Pose>(position_pub_topic, 1);

  double last = ros::Time::now().toSec();
  double now = ros::Time::now().toSec();
  while (ros::ok()){
    ros::spinOnce();
    now = ros::Time::now().toSec();
    if(now - last > 2.0){
      callback_print_data();
      last = now;
    }
  }
  return 0;
}


/*
 rostopic pub /flyingros/lasers/raw flyingros_msgs/Distance -- [1.0,1.0,1.0,1.0,1.0,1.0] [0,0,0,0,0,0]

*/