#include "ros/ros.h"
#include "microstrain_3dm_gx3_45/driver.h"
#include "microstrain_3dm_gx3_45/node.h"

using namespace microstrain_3dm_gx3_45;
using namespace std;
using namespace boost;
using namespace ros;

/*
 * TODOs
 * prepare object instead of this ugly code
 * add some services etc.
 */


imuNode::imuNode() : nh_priv_("~") {

	param::param<string>("~port",port_,"/dev/ttyACM0");
	param::param<int>("~baud_rate",baud_rate_,115200);
	param::param<int>("~declination",declination_,0);
	param::param<string>("~frame_id",frame_id_,"/imu");
	param::param<string>("~child_frame_id",child_frame_id_,"/base_footprint");
	param::param<float>("~rate",rate_,10.0);

	param::param<bool>("~publish_pose",publish_pose_,true);
	param::param<bool>("~publish_imu",publish_imu_,true);
	param::param<bool>("~publish_gps",publish_gps_,true);
	param::param<bool>("~publish_gps_as_odom",publish_gps_as_odom_,true);

	param::param("linear_acceleration_stdev", linear_acceleration_stdev_, 0.098);
	param::param("orientation_stdev", orientation_stdev_, 0.035);
	param::param("angular_velocity_stdev", angular_velocity_stdev_, 0.012);

	started_ = false;
	inited_ = false;

	gps_fix_available_ = false;

	if (!imu_.openPort(port_,(unsigned int)baud_rate_)) {

		ROS_ERROR("Can't open port.");

	}

	if (publish_imu_) imu_data_pub_ = nh_priv_.advertise<sensor_msgs::Imu>("imu/data", 100);
	if (publish_pose_) imu_pose_pub_ = nh_priv_.advertise<geometry_msgs::PoseStamped>("imu/pose", 100);

	service_reset_ = service_reset_ = nh_priv_.advertiseService("reset_kf", &imuNode::srvResetKF,this);

	if (publish_gps_) gps_pub_ = nh_priv_.advertise<sensor_msgs::NavSatFix>("gps/fix", 100);
	if (publish_gps_as_odom_) gps_odom_pub_ = nh_priv_.advertise<nav_msgs::Odometry>("gps/odom", 100);

}

bool imuNode::srvResetKF(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res) {


	ROS_INFO("Resetting KF.");

	if (!imu_.setToIdle()) ROS_ERROR("%s",imu_.getLastError().c_str());
	if (!imu_.initKalmanFilter(declination_)) ROS_ERROR("%s",imu_.getLastError().c_str());
	if (!imu_.resume()) ROS_ERROR("%s",imu_.getLastError().c_str());

	return true;

}

bool imuNode::init() {

	started_ = false;

	ROS_INFO("Pinging device");
	imu_.setTimeout(posix_time::seconds(0.5));
	if (!imu_.ping()) {

		printErrMsgs("Pinging device");
		return false;

	}

	ROS_INFO("Setting to idle");
	if (!imu_.setToIdle()) {

		printErrMsgs("Setting to idle");
		return false;

	}

	ROS_INFO("Checking status");
	if (!imu_.devStatus()) {

		printErrMsgs("Checking status");
		return false;

	}

	ROS_INFO("Disabling all streams");
	if (!imu_.disAllStreams()) {

		printErrMsgs("Disabling all streams");
		return false;

	}

	ROS_INFO("Device self test");
	if (!imu_.selfTest()) {

		printErrMsgs("Device self test");
		return false;
	}

	ROS_INFO("Setting AHRS msg format");
	if (!imu_.setAHRSMsgFormat()) {

		printErrMsgs("Setting AHRS msg format");
		return false;

	}

	ROS_INFO("Setting GPS msg format");
	if (!imu_.setGPSMsgFormat()) {

			printErrMsgs("Setting GPS msg format");
			return false;

		}

	ROS_INFO("Setting NAV msg format");
	if (!imu_.setNAVMsgFormat()) {

		printErrMsgs("Setting NAV msg format");
		return false;

	}

	start();

	ROS_INFO("KF initialization");
	if (!imu_.initKalmanFilter((uint32_t)declination_)) {

		printErrMsgs("KF initialization");
		return false;

	}

	inited_ = true;
	return true;

}

bool imuNode::start() {

	if (!imu_.resume()) {

			printErrMsgs("Resuming");
			return false;

		}

	started_ = true;
	return true;

}

bool imuNode::stop() {

	if (!imu_.setToIdle()) {

		printErrMsgs("To idle");
		return false;

	}

	started_ = false;
	return true;

}

void imuNode::spin() {

	Rate r(rate_);

	geometry_msgs::PoseStamped ps;
	ps.header.frame_id = frame_id_;
	ps.pose.position.x = ps.pose.position.y = ps.pose.position.z = 0.0;

	sensor_msgs::Imu imu;
	imu.header.frame_id = frame_id_;

	double angular_velocity_covariance = angular_velocity_stdev_ * angular_velocity_stdev_;
	double orientation_covariance = orientation_stdev_ * orientation_stdev_;
	double linear_acceleration_covariance = linear_acceleration_stdev_ * linear_acceleration_stdev_;

	imu.linear_acceleration_covariance[0] = linear_acceleration_covariance;
	imu.linear_acceleration_covariance[4] = linear_acceleration_covariance;
	imu.linear_acceleration_covariance[8] = linear_acceleration_covariance;

	imu.angular_velocity_covariance[0] = angular_velocity_covariance;
	imu.angular_velocity_covariance[4] = angular_velocity_covariance;
	imu.angular_velocity_covariance[8] = angular_velocity_covariance;

	imu.orientation_covariance[0] = orientation_covariance;
	imu.orientation_covariance[4] = orientation_covariance;
	imu.orientation_covariance[8] = orientation_covariance;

	sensor_msgs::NavSatFix gps;

	gps.header.frame_id = frame_id_;
	gps.status.service = sensor_msgs::NavSatStatus::SERVICE_GPS;

	nav_msgs::Odometry gps_odom;

	gps_odom.header.frame_id = frame_id_;
	gps_odom.child_frame_id = child_frame_id_;
	gps_odom.pose.pose.orientation.x = 1; // identity quaternion
	gps_odom.pose.pose.orientation.y = 0;
	gps_odom.pose.pose.orientation.z = 0;
	gps_odom.pose.pose.orientation.w = 0;
	gps_odom.pose.covariance[21] = 99999; // rot x
	gps_odom.pose.covariance[28] = 99999; // rot y
	gps_odom.pose.covariance[35] = 99999; // rot z


	ROS_INFO("Start polling device.");

	int gps_msg_cnt = 0;

	while(ok()) {

		// just for testing
		if (!imu_.pollNAV()) {

			printErrMsgs("NAV");

		}

		if (publish_imu_ || publish_pose_) {

			if (!imu_.pollAHRS()) {

				printErrMsgs("AHRS");

			}

		}


		if (publish_imu_ && imu_data_pub_.getNumSubscribers() > 0) {

			tahrs q = imu_.getAHRS();

			imu.header.stamp.fromNSec(q.time);

			imu.linear_acceleration.x = -q.ax;
			imu.linear_acceleration.y = q.ay;
			imu.linear_acceleration.z = -q.az;

			imu.angular_velocity.x = -q.gx;
			imu.angular_velocity.y = q.gy;
			imu.angular_velocity.z = -q.gz;

			float yaw = q.y;

			yaw+=M_PIl;
			if (yaw > M_PIl) yaw-=2*M_PIl;

			tf::quaternionTFToMsg(tf::createQuaternionFromRPY(-q.p, q.p, -yaw), imu.orientation);

			imu_data_pub_.publish(imu);

		}

		if (publish_pose_ && imu_pose_pub_.getNumSubscribers() > 0) {

			tahrs q = imu_.getAHRS();

			//ps.header.stamp.fromBoost(q.time);
			ps.header.stamp.fromNSec(q.time);

			float yaw = q.y;
			yaw+=M_PIl;
			if (yaw > M_PIl) yaw-=2*M_PIl;

			tf::quaternionTFToMsg(tf::createQuaternionFromRPY(-q.p, q.p, -yaw),ps.pose.orientation);

			imu_pose_pub_.publish(ps);

		}

		if (publish_gps_ || publish_gps_as_odom_) {

			if (!imu_.pollGPS()) {

				printErrMsgs("GPS");

			}

			tgps g;
			g = imu_.getGPS();

			if (g.lat_lon_valid && !gps_fix_available_) {

				ROS_INFO("GPS fix available.");
				gps_fix_available_ = true;

			}

			if (!g.lat_lon_valid && gps_fix_available_) {

				ROS_WARN("GPS fix lost.");
				gps_fix_available_ = false;

			}

			if (!g.lat_lon_valid) ROS_WARN_ONCE("GPS fix not available.");

		}

		if (publish_gps_ && gps_pub_.getNumSubscribers() > 0) {

			tgps g;
			g = imu_.getGPS();

			gps.header.stamp.fromNSec(g.time);

			gps.latitude = g.latitude;
			gps.longitude = g.longtitude;

			if (g.lat_lon_valid) gps.status.status = sensor_msgs::NavSatStatus::STATUS_FIX;
			else gps.status.status = sensor_msgs::NavSatStatus::STATUS_NO_FIX;

			if (!g.hor_acc_valid) gps.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_UNKNOWN;
			else {

				//gps.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN;
				gps.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_APPROXIMATED;

				gps.position_covariance[0] = g.horizontal_accuracy * g.horizontal_accuracy;
				gps.position_covariance[4] = g.horizontal_accuracy * g.horizontal_accuracy;
				gps.position_covariance[8] = 100.0; // TODO add this to driver

			}

			gps_pub_.publish(gps);

			if (gps_msg_cnt++==2*rate_) {

				gps_msg_cnt = 0;

				if (!g.lat_lon_valid) ROS_WARN("LAT/LON not valid.");
				if (!g.hor_acc_valid) ROS_WARN("Horizontal accuracy not valid.");
				else ROS_INFO("GPS horizontal accuracy: %f",g.horizontal_accuracy);

			}

		}

		if (publish_gps_as_odom_ && gps_odom_pub_.getNumSubscribers() > 0) {

			tgps g;
			g = imu_.getGPS();

			gps_odom.header.stamp.fromNSec(g.time);

			if (g.lat_lon_valid) {

				gps_odom.pose.covariance[0] = g.horizontal_accuracy * g.horizontal_accuracy;
				gps_odom.pose.covariance[7] = g.horizontal_accuracy * g.horizontal_accuracy;
				gps_odom.pose.covariance[14] = 99999; // TODO check vertical acc.

			} else {

				gps_odom.pose.covariance[0] = 99999;
				gps_odom.pose.covariance[7] = 99999;
				gps_odom.pose.covariance[14] = 99999;

			}

			double northing, easting;
			string zone;

			gps_common::LLtoUTM(g.latitude, g.longtitude, northing, easting, zone);

			gps_odom.pose.pose.position.x = easting;
			gps_odom.pose.pose.position.y = northing;
			gps_odom.pose.pose.position.z = 0.0; // TODO fill this

			gps_odom_pub_.publish(gps_odom);

		}

		r.sleep();
		spinOnce();

	};

	stop();

}

void imuNode::printErrMsgs(string prefix) {

	string msg = "...";

	while(msg!="") {

	  msg = imu_.getLastError();
	  if (msg!="") ROS_ERROR("%s: %s",prefix.c_str(),msg.c_str());

  }

}

imuNode::~imuNode() {

	imu_.closePort();

}

int main(int argc, char **argv)
{

  ros::init(argc, argv, "imu3dmgx3");

  imuNode node;

  ROS_INFO("Initializing.");
  if (!node.init()) {

	  ROS_ERROR("Initialization failed. Please check logs.");
	  return 0;

  }

  ROS_INFO("Initialization completed.");

  node.spin();


  ROS_INFO("Finished");


  return 0;

}
