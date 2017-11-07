#include "Drone.h"
#include "ros/ros.h"
#include <iostream>
#include <vector>
#include <cstring>
#include "common/Common.hpp"
#include "coord.h"
#include <geometry_msgs/Point.h>
#include "timer.h"
#include <geometry_msgs/PoseWithCovarianceStamped.h>
Drone::Drone() : client(0)
{
	connect();
    initial_pos = gps();
}

Drone::Drone(const std::string& ip_addr, uint16_t port) : client(0), collision_count(0)
{
	connect(ip_addr, port);
    initial_pos = gps();
}

Drone::~Drone()
{
	if (client != 0)
		delete client;
}

void Drone::connect()
{
	if (client != 0)
		delete client;
	client = new msr::airlib::MultirotorRpcLibClient();
    client->enableApiControl(true);
}

void Drone::connect(const std::string& ip_addr, uint16_t port)
{
	if (client != 0)
		delete client;
	client = new msr::airlib::MultirotorRpcLibClient(ip_addr, port);
    client->enableApiControl(true);
}

void Drone::arm()
{
	client->armDisarm(true);
}

void Drone::disarm()
{
	client->armDisarm(false);
}

bool Drone::takeoff(double h)
{
	const double takeoff_timeout = 60.0;
    auto ground_pos = client->getPosition();

	try {
		client->takeoff(takeoff_timeout);
	} catch (...) {
		std::cout << "Taking off failed" << std::endl;
	}

	std::cout << "Press enter to set offboard mode";
	while (std::cin.get() != '\n') {}

	try {
		// client->setOffboardMode(true);
	} catch (...) {
		std::cout << "Setting offboard mode failed" << std::endl;
	}

	std::cout << "Press enter to move to height " << h;
	while (std::cin.get() != '\n') {}

	try{
    	client->moveToPosition(ground_pos.x(), ground_pos.y(), ground_pos.z() - h, 1);
	} catch (...) {
		std::cout << "Moving to z position failed" << std::endl;
	}

	return true;
}

bool Drone::set_yaw(float y)
{
	try {
		client->rotateToYaw(y, 60, 5);
	}catch(...){
		std::cerr << "set_yaw failed" << std::endl;
		return false;
	}

	return true;
}

bool Drone::set_yaw_based_on_quaternion(geometry_msgs::Quaternion q)
{
	float pitch, roll, yaw;
    Eigen::Quaternion<float,Eigen::DontAlign> q_airsim_style;
    q_airsim_style.x() = q.x;
    q_airsim_style.y() = q.y;
    q_airsim_style.z() = q.z;
    q_airsim_style.w() = q.w;
    msr::airlib::VectorMath::toEulerianAngle(q_airsim_style, pitch, roll, yaw);
	
    try {
        this->set_yaw(yaw*180 / M_PI);
    } catch(...) {
		std::cerr << "set_yaw_based_on_quaternion failed" << std::endl;
		return false;
	}
	return true;
}

bool Drone::fly_velocity(double vx, double vy, double vz, double duration)
{
	try {
		getCollisionInfo();
		//client->moveByVelocity(vx, vy, vz, duration);
		client->moveByVelocity(vy,vx, -1*vz, duration);
    } catch(...) {
		std::cerr << "fly_velocity failed" << std::endl;
		return false;
	}

	return true;
}

bool Drone::land()
{
	try {
		client->land();
	} catch(...) {
		std::cerr << "land failed" << std::endl;
		return false;
	}

	return true;
}

coord Drone::gps()
{
	getCollisionInfo();
	auto pos = client->getPosition();
    /*
	return {pos.x() - initial_pos.x,
        pos.y() - initial_pos.y,
        pos.z() - initial_pos.z};
    */
	return { pos.y() - initial_pos.y, pos.x() - initial_pos.x, -1*pos.z() - initial_pos.z};
}

float Drone::get_yaw()
{
	auto q = client->getOrientation();
	float p, r, y;
    msr::airlib::VectorMath::toEulerianAngle(q, p, y, r);

	return y*180 / M_PI;
}

float Drone::get_roll()
{
	auto q = client->getOrientation();
	float p, r, y;
    msr::airlib::VectorMath::toEulerianAngle(q, p, y, r);

	return r*180 / M_PI;
}

geometry_msgs::Pose Drone::get_geometry_pose(){
    geometry_msgs::Pose pose;
	auto q = client->getOrientation();
	//auto p = client->getPosition();
    pose.position.x = client->getPosition().y();
    pose.position.y = client->getPosition().x();
    pose.position.z = -1*client->getPosition().z();
    pose.orientation.x = q.x();
    pose.orientation.y = q.y();
    pose.orientation.z = q.z();
    pose.orientation.w = q.w();
    return pose;
}
geometry_msgs::PoseWithCovariance Drone::get_geometry_pose_with_coveraiance(){
    geometry_msgs::PoseWithCovariance pose_with_covariance;
    geometry_msgs::Pose pose;
	
    auto q = client->getOrientation();
    pose.position.x = client->getPosition().y();
    pose.position.y = client->getPosition().x();
    pose.position.z = -1*client->getPosition().z();
    pose.orientation.x = q.x();
    pose.orientation.y = q.y();
    pose.orientation.z = q.z();
    pose.orientation.w = q.w();
    pose_with_covariance.pose = pose;
    for (int i = 0; i <36; i++) { 
        //https://answers.ros.org/question/181689/computing-posewithcovariances-6x6-matrix/ 
        pose_with_covariance.covariance[i] = 0;
    } 
    return pose_with_covariance;
}
float Drone::get_pitch()
{
	auto q = client->getOrientation();
	float p, r, y;
    msr::airlib::VectorMath::toEulerianAngle(q, p, y, r);

	return p*180 / M_PI;
}

msr::airlib::CollisionInfo Drone::getCollisionInfo()
{
  auto col_info = client->getCollisionInfo();
  if (col_info.has_collided) {
     collision_count ++;
  }
  if (collision_count > 10) {
    land();
    sleep(5);
    disarm();
    fprintf(stderr, "Drone Crashed!\n");
    LOG_TIME(crashed);
    ros::shutdown();
    exit(0);
  }

  if (col_info.has_collided) {
  printf("CollisionInfo %s, count %ld, collison %d, penetrate %f\n",
          col_info.has_collided ? "has collided" : "none", collision_count, col_info.collison_count,
          col_info.penetration_depth);
  }

  return col_info;
}