#include "ros/ros.h"
#include <std_msgs/String.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
//#include "template_library.hpp"
#include <sstream>
//#include "rpc/RpcLibClient.hpp"
#include <iostream>
#include <chrono>
#include <math.h>
#include <iterator>
#include <chrono>
#include <thread>
#include <tuple>
//#include "controllers/DroneControllerBase.hpp"
#include "control_drone.h"
#include "common/Common.hpp"
#include <fstream>
#include "Drone.h"
#include "package_delivery/get_trajectory.h"
#include <cstdlib>
#include <geometry_msgs/Point.h>
#include <trajectory_msgs/MultiDOFJointTrajectoryPoint.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>
#include <stdio.h>
#include <time.h>
#include "std_msgs/Bool.h"
#include <signal.h>
#include "common.h"
#include <profile_manager/profiling_data_srv.h>

using namespace std;
string ip_addr__global;
string localization_method;
string g_stats_file_addr;
string ns;
std::string g_supervisor_mailbox; //file to write to when completed

//data to be logged in stats manager
std::string g_mission_status = "failed";

enum State { setup, waiting, flying, completed, invalid };


double dist(coord t, geometry_msgs::Point m)
{
    // We must convert between the two coordinate systems
    return std::sqrt((t.x-m.y)*(t.x-m.y) + (t.y-m.x)*(t.y-m.x) + (t.z+m.z)*(t.z+m.z));
}


void log_data_before_shutting_down(){
    profile_manager::profiling_data_srv profiling_data_srv_inst;
    profiling_data_srv_inst.request.key = "mission_status";
    profiling_data_srv_inst.request.value = (g_mission_status == "completed" ? 1.0: 0.0);
    
    if (ros::service::waitForService("/record_profiling_data", 10)){ 
        if(!ros::service::call("/record_profiling_data",profiling_data_srv_inst)){
            ROS_ERROR_STREAM("could not probe data using stats manager");
            ros::shutdown();
        }
    }
}


void package_delivery_initialize_params() {
    if(!ros::param::get("/scanning/ip_addr",ip_addr__global)){
        ROS_FATAL("Could not start exploration. Parameter missing! Looking for %s", 
                (ns + "/ip_addr").c_str());
    }
    if(!ros::param::get("/scanning/localization_method",localization_method)){
        ROS_FATAL("Could not start exploration. Parameter missing! Looking for %s", 
                (ns + "/localization_method").c_str());
    }

    if(!ros::param::get("/stats_file_addr",g_stats_file_addr)){
        ROS_FATAL("Could not start scanning . Parameter missing! Looking for %s", 
                (ns + "/stats_file_addr").c_str());
    }

    if(!ros::param::get("/supervisor_mailbox",g_supervisor_mailbox))  {
        ROS_FATAL_STREAM("Could not start scanning supervisor_mailbox not provided");
        //return -1;
    }
}


geometry_msgs::Point get_start(Drone& drone) {
    geometry_msgs::Point start;

    // Get current position from drone
    auto drone_pos = drone.position();
    start.x = drone_pos.x; start.y = drone_pos.y; start.z = drone_pos.z;
    std::cout << "Current position is " << drone_pos.x << " " << drone_pos.y << " " << drone_pos.z << std::endl;

    return start;
}


void get_goal(int& width, int& length, int& lanes) {
    std::cout << "Enter width ,length and number of lanes"<<std::endl
        << "associated with the area you like to sweep "<<endl;

    std::cin >> width >> length >> lanes;
}


trajectory_t request_trajectory(ros::ServiceClient& client, geometry_msgs::Point start, int width, int length, int lanes) {
    // Request the actual trajectory from the motion_planner node
    package_delivery::get_trajectory srv;
    srv.request.start = start;
    srv.request.width = width;
    srv.request.length = length;
    srv.request.n_pts_per_dir = lanes;

    if (client.call(srv)) {
        ROS_INFO("Received trajectory.");
    } else {
        ROS_ERROR("Failed to call service.");
        return trajectory_t();
    }

    trajectory_t result;
    for (multiDOFpoint p : srv.response.multiDOFtrajectory.points) {
        result.push_back(p);
    }

    return result;
}


bool trajectory_done(trajectory_t trajectory) {
    return trajectory.size() <= 1;
}


void sigIntHandlerPrivate(int signo){
    if (signo == SIGINT) {
        log_data_before_shutting_down(); 
        ros::shutdown();
    }
    exit(0);
}


int main(int argc, char **argv)
{
    ros::init(argc, argv, "scanning", ros::init_options::NoSigintHandler);
    ros::NodeHandle n;
    ros::NodeHandle panic_nh;
    string app_name;
    package_delivery_initialize_params();
    int width, length, lanes; // size of area to scan
    geometry_msgs::Point start, goal, original_start;
	package_delivery::get_trajectory get_trajectory_srv;
    trajectory_t trajectory, reverse_trajectory;
    uint16_t port = 41451;
    Drone drone(ip_addr__global.c_str(), port, localization_method);
    signal(SIGINT, sigIntHandlerPrivate);
	ros::ServiceClient get_trajectory_client = 
        n.serviceClient<package_delivery::get_trajectory>("get_trajectory_srv");
    ros::ServiceClient record_profiling_data_client = 
      n.serviceClient<profile_manager::profiling_data_srv>("/record_profiling_data");

    //----------------------------------------------------------------- 
	// *** F:DN knobs(params)
	//----------------------------------------------------------------- 
    //const int step__total_number = 1;
    int package_delivery_loop_rate = 100;
    float goal_s_error_margin = 5.0; //ok distance to be away from the goal.
                                                      //this is b/c it's very hard 
                                                      //given the issues associated with
                                                      //flight controler to land exactly
                                                      //on the goal

    
    ros::Rate loop_rate(package_delivery_loop_rate);
    for (State state = setup; ros::ok(); ) {
        ros::spinOnce();
        State next_state = invalid;

        if (state == setup){
            control_drone(drone);
            get_goal(width, length, lanes);
            original_start = get_start(drone);
            next_state = waiting;
        } else if (state == waiting) {
            ROS_INFO("Waiting to receive trajectory...");
            start = get_start(drone);
            trajectory = request_trajectory(get_trajectory_client, start, width, length, lanes);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            next_state = flying;
        } else if (state == flying){
            follow_trajectory(drone, trajectory, reverse_trajectory);
            next_state = trajectory_done(trajectory) ? completed : flying;
        } else if (state == completed){
            drone.fly_velocity(0, 0, 0);
            ROS_INFO("scanned the entire space and returned successfully");
            //update_stats_file(stats_file_addr,"mission_status completed");
            g_mission_status = "completed";
            log_data_before_shutting_down();
            signal_supervisor(g_supervisor_mailbox, "kill"); 
            ros::shutdown(); 
            //next_state = setup;
        }else{
            ROS_ERROR("Invalid FSM state!");
            break;
        }

        state = next_state;
    }

    return 0;
}
