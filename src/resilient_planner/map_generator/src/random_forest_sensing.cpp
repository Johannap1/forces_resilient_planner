#include <iostream>
#include <pcl/io/pcd_io.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/search/kdtree.h>
#include <pcl/search/impl/kdtree.hpp>

#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/Bool.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/PoseStamped.h>
#include <nav_msgs/Odometry.h>
#include <Eigen/Eigen>
#include <math.h>
#include <random>

using namespace std;
using namespace Eigen;

ros::Publisher _all_map_pub;

ros:: Subscriber _map_trigger;

int _obs_num;
double _x_size, _y_size, _z_size, _init_x, _init_y, _resolution, _sense_rate;
double _x_l, _x_h, _y_l, _y_h, _w_l, _w_h, _h_l, _h_h, _w_c_l, _w_c_h;

bool _has_map  = false;

sensor_msgs::PointCloud2 globalMap_pcd;
pcl::PointCloud<pcl::PointXYZ> cloudMap;

pcl::search::KdTree<pcl::PointXYZ> kdtreeMap;
vector<int>     pointIdxSearch;
vector<float>   pointSquaredDistance;      

void RandomMapGenerate()
{  
   // random_device rd;
   // default_random_engine eng(rd());
   
   // uniform_real_distribution<double> rand_x = uniform_real_distribution<double>(_x_l, _x_h );
   // uniform_real_distribution<double> rand_y = uniform_real_distribution<double>(_y_l, _y_h );
   // uniform_real_distribution<double> rand_w = uniform_real_distribution<double>(_w_l, _w_h);
   // uniform_real_distribution<double> rand_h = uniform_real_distribution<double>(_h_l, _h_h);

   // uniform_real_distribution<double> rand_x_circle = uniform_real_distribution<double>(_x_l + 1.0, _x_h - 1.0);
   // uniform_real_distribution<double> rand_y_circle = uniform_real_distribution<double>(_y_l + 1.0, _y_h - 1.0);
   // uniform_real_distribution<double> rand_r_circle = uniform_real_distribution<double>(_w_c_l    , _w_c_h    );

   // uniform_real_distribution<double> rand_roll      = uniform_real_distribution<double>(- M_PI,     + M_PI);
   // uniform_real_distribution<double> rand_pitch     = uniform_real_distribution<double>(+ M_PI/4.0, + M_PI/2.0);
   // uniform_real_distribution<double> rand_yaw       = uniform_real_distribution<double>(+ M_PI/4.0, + M_PI/2.0);
   // uniform_real_distribution<double> rand_ellipse_c = uniform_real_distribution<double>(0.5, 2.0);
   // uniform_real_distribution<double> rand_num       = uniform_real_distribution<double>(0.0, 1.0);

   pcl::PointXYZ pt_random;

   vector<double> obstacle_x = {-0.5, 0.7, -1};
   vector<double> obstacle_y = {-2, 0, 2};
   vector<double> obstacle_w = {0.5, 0.7, 0.5};

   _obs_num = 3;

   bool is_kdtree_empty = false;
   if(cloudMap.points.size() > 0)
      kdtreeMap.setInputCloud( cloudMap.makeShared() ); 
   else
      is_kdtree_empty = true;

   // then, we put some pilar
   for(int i = 0; i < _obs_num; i ++)
   {
      double x, y, w, h; 
      x    = obstacle_x[i];
      y    = obstacle_y[i];
      w    = obstacle_w[i];
      
      pcl::PointXYZ searchPoint(x, y, (_h_l + _h_h)/2.0);
      pointIdxSearch.clear();
      pointSquaredDistance.clear();
      
      if(is_kdtree_empty == false)
      {
         if ( kdtreeMap.nearestKSearch (searchPoint, 1, pointIdxSearch, pointSquaredDistance) > 0 )
         {
            if(sqrt(pointSquaredDistance[0]) < 1.0 )
               continue;
         }
      }

      x = floor(x/_resolution) * _resolution + _resolution / 2.0;
      y = floor(y/_resolution) * _resolution + _resolution / 2.0;

      int widNum = ceil(w/_resolution);
      for(int r = -widNum/2.0; r < widNum/2.0; r ++ )
      {
         for(int s = -widNum/2.0; s < widNum/2.0; s ++ )
         {
            h    = 5.0;  
            int heiNum = 2.0 * ceil(h/_resolution);
            for(int t = 0; t < heiNum; t ++ ){
               pt_random.x = x + (r+0.0) * _resolution + 0.001;
               pt_random.y = y + (s+0.0) * _resolution + 0.001;
               pt_random.z =     (t+0.0) * _resolution * 0.5 -1.0 + 0.001;
               double dist = sqrt(pow(pt_random.x-x,2) + pow(pt_random.y-y,2));
               if(dist <= w/2)
                  cloudMap.points.push_back(pt_random);
            }
         }
      }
   }

   cloudMap.width = cloudMap.points.size();
   cloudMap.height = 1;
   cloudMap.is_dense = true;

   _has_map = true;
   
   pcl::toROSMsg(cloudMap, globalMap_pcd);
   globalMap_pcd.header.frame_id = "map";
}

void pubSensedPoints()
{
   if( !_has_map ) return;
   _all_map_pub.publish(globalMap_pcd);
}

void triggerpubSensedPoints(const std_msgs::Bool &msg)
{
   if( !_has_map ) return;
   _all_map_pub.publish(globalMap_pcd);
}

int main (int argc, char** argv) 
{        
   ros::init (argc, argv, "random_map_sensing");
   ros::NodeHandle n( "~" );

   _all_map_pub = n.advertise<sensor_msgs::PointCloud2>("all_map", 1); 
   _map_trigger = n.subscribe("trigger_map", 10, triggerpubSensedPoints);

   n.param("init_state_x", _init_x,       0.0);
   n.param("init_state_y", _init_y,       0.0);

   n.param("map/x_size",  _x_size, 10.0);
   n.param("map/y_size",  _y_size, 10.0);
   n.param("map/z_size",  _z_size, 10.0 );

   n.param("map/obs_num",    _obs_num,  30);
   n.param("map/resolution", _resolution, 0.05);

   n.param("ObstacleShape/lower_hei", _h_l,   -1.0);
   n.param("ObstacleShape/upper_hei", _h_h,   3.0);

   RandomMapGenerate();
   //only pub map pointcloud on request
   //ros::ServiceServer pub_glb_obs_service = n.advertiseService("/pub_glb_obs", pubGlbObs);
   ros::spin();
   pubSensedPoints();
}