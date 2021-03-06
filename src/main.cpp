#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"

#include "map.h"
#include "behavior.h"
#include "trajectory.h"
#include "cost.h"
#include "predictions.h"

#include "params.h"

#include <map>

using namespace std;

// for convenience
using json = nlohmann::json;


// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}


int main() {
  uWS::Hub h;

  //////////////////////////////////////////////////////////////////////
  Map map;
  map.read(map_file_);

  //previous lane points, at the start point we assume that the ego car stay in lane 1.
  queue<int> lanes_line;
  for(int i=0;i<30;i++)
       lanes_line.push(1);

  CarData car = CarData(0., 0., 0., 0., 0.,  0., 1.0, 0.);
  //////////////////////////////////////////////////////////////////////


  h.onMessage([&map, &car, &lanes_line](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length, uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;


    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);

        string event = j[0].get<string>();

        if (event == "telemetry") {
          // j[1] is the data JSON object

            TrajectoryXY previous_path_xy;

        	// Main car's localization Data
          	car.x = j[1]["x"];
          	car.y = j[1]["y"];
          	car.s = j[1]["s"];
          	car.d = j[1]["d"];
          	car.yaw = j[1]["yaw"];
          	car.speed = j[1]["speed"]; //MPH

            // cout << "SPEEDOMETER: car.speed=" << car.speed << " car.speed_target=" << car.speed_target << '\n';
          	// Previous path data given to the Planner
          	vector<double> previous_path_x = j[1]["previous_path_x"];
          	vector<double> previous_path_y = j[1]["previous_path_y"];
          	previous_path_xy.x_vals = previous_path_x;
          	previous_path_xy.y_vals = previous_path_y;

          	// Previous path's end s and d values
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
            vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            //////////////////////////////////////////////////////////////////////

            int prev_size = previous_path_xy.x_vals.size();

            vector<double> frenet_car = map.getFrenet(car.x, car.y, deg2rad(car.yaw));
            car.s = frenet_car[0];
            car.d = frenet_car[1];
            car.lane = get_lane(car.d);
            // cout << "car.s=" << car.s << " car.d=" << car.d << endl;
            lanes_line.push(car.lane);
            lanes_line.pop();
            car.passed_path = lanes_line;
            // -- prev_size: close to 100 msec when possible -not lower bcz of simulator latency- for trajectory (re)generation ---
            // points _before_ prev_size are kept from previous generated trajectory
            // points _after_  prev_size will be re-generated
            PreviousPath previous_path = PreviousPath(previous_path_xy,  min(prev_size, PARAM_PREV_PATH_XY_REUSED));

            // --- 6 car predictions x 50 points x 2 coord (x,y): 6 objects predicted over 1 second horizon ---
            Predictions predictions = Predictions(sensor_fusion, car, PARAM_NB_POINTS /* 50 */);

            Behavior behavior = Behavior(sensor_fusion, car, predictions);
            vector<Target> targets = behavior.get_targets();

            Trajectory trajectory = Trajectory(targets, map, car, previous_path, predictions);
            // --------------------------------------------------------------------------

            double min_cost = trajectory.getMinCost();
            int min_cost_index = trajectory.getMinCostIndex();
            vector<double> next_x_vals = trajectory.getMinCostTrajectoryXY().x_vals;
            vector<double> next_y_vals = trajectory.getMinCostTrajectoryXY().y_vals;

            int target_lane = targets[min_cost_index].lane;
            car.speed_target = targets[min_cost_index].velocity;


          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals; //trajectories[min_cost_index].x_vals; //next_x_vals;
          	msgJson["next_y"] = next_y_vals; //trajectories[min_cost_index].y_vals; //next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
