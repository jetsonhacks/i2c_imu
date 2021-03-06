//  Copyright (c) 2014 Justin Eskesen
//
//  This file is part of i2c_imu
//
//  i2c_imu is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  i2c_imu is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with i2c_imu.  If not, see <http://www.gnu.org/licenses/>.
//

#include <ros/ros.h>
#include <tf/transform_broadcaster.h>
#include <sensor_msgs/Imu.h>
#include <angles/angles.h>

#include "RTIMULib.h"
#include "RTIMUSettings.h"

#define G_2_MPSS 9.80665

class I2cImu
{
public:
	I2cImu();

	void update();
	void spin();

private:
	//ROS Stuff
	ros::NodeHandle nh_;
	ros::NodeHandle private_nh_;

	tf::TransformBroadcaster tf_broadcaster_;

	ros::Publisher imu_pub_;
	ros::Publisher magnetometer_pub_;
	ros::Publisher euler_pub_;

	std::string imu_frame_id_;

	ros::Time last_update_;

	//RTUIMULib stuff
	RTIMU *imu_;

	class ImuSettings: public RTIMUSettings
	{
	public:
		ImuSettings(ros::NodeHandle* nh) : settings_nh_(nh){setDefaults();}
		virtual bool loadSettings();
		virtual bool saveSettings(){return true;}
	private:
		ros::NodeHandle* settings_nh_;
	} imu_settings_;
};

I2cImu::I2cImu() :
		nh_(), private_nh_("~"), imu_settings_(&private_nh_)
{

	// do all the ros parameter reading & pulbishing
	private_nh_.param<std::string>("frame_id", imu_frame_id_, "imu_link");

	imu_pub_ = nh_.advertise<sensor_msgs::Imu>("data",10);

	bool magnetometer;
	private_nh_.param("publish_magnetometer", magnetometer, false);
	if (magnetometer)
	{
		magnetometer_pub_ = nh_.advertise<geometry_msgs::Vector3Stamped>("mag", 10, false);
	}

	bool euler;
	private_nh_.param("publish_euler", euler, false);
	if (euler)
	{
		euler_pub_ = nh_.advertise<geometry_msgs::Vector3Stamped>("euler", 10, false);
	}
	imu_settings_.loadSettings();

	// now set up the IMU

	imu_ = RTIMU::createIMU(&imu_settings_);
	if (imu_ == NULL)
	{
		ROS_FATAL("I2cImu - %s - Failed to open the i2c device", __FUNCTION__);
		ROS_BREAK();
	}

	if (!imu_->IMUInit())
	{
		ROS_FATAL("I2cImu - %s - Failed to init the IMU", __FUNCTION__);
		ROS_BREAK();
	}
	;
}

void I2cImu::update()
{

	while (imu_->IMURead())
	{
		RTIMU_DATA imuData = imu_->getIMUData();

		ros::Time current_time = ros::Time::now();
		// sensor msg topic output
		sensor_msgs::Imu imu_msg;

		imu_msg.header.stamp = current_time;
		imu_msg.header.frame_id = imu_frame_id_;
		imu_msg.orientation.x = imuData.fusionQPose.x();
		imu_msg.orientation.y = imuData.fusionQPose.y();
		imu_msg.orientation.z = imuData.fusionQPose.z();
		imu_msg.orientation.w = imuData.fusionQPose.scalar();

		imu_msg.angular_velocity.x = imuData.gyro.x();
		imu_msg.angular_velocity.y = imuData.gyro.y();
		imu_msg.angular_velocity.z = imuData.gyro.z();

		imu_msg.linear_acceleration.x = imuData.accel.x() * G_2_MPSS;
		imu_msg.linear_acceleration.y = imuData.accel.y() * G_2_MPSS;
		imu_msg.linear_acceleration.z = imuData.accel.z() * G_2_MPSS;

		imu_pub_.publish(imu_msg);

		if (magnetometer_pub_ != NULL && imuData.compassValid)
		{
			geometry_msgs::Vector3Stamped msg;
			msg.header.stamp = current_time;
			msg.header.frame_id = imu_frame_id_;
			msg.vector.x = imuData.compass.x();
			msg.vector.y = imuData.compass.y();
			msg.vector.z = imuData.compass.z();

			magnetometer_pub_.publish(msg);
		}

		if (euler_pub_ != NULL)
		{
			geometry_msgs::Vector3Stamped msg;
			msg.header.stamp = current_time;
			msg.header.frame_id = imu_frame_id_;
			msg.vector.x = imuData.fusionPose.x();
			msg.vector.y = imuData.fusionPose.y();
			msg.vector.z = -imuData.fusionPose.z();
			euler_pub_.publish(msg);
		}
	}

}

bool I2cImu::ImuSettings::loadSettings()
{
	ROS_INFO("%s: reading IMU parameters from param server", __FUNCTION__);
	int temp_int;

	// General
	settings_nh_->getParam("imu_type", m_imuType);
	settings_nh_->getParam("fusion_type", m_fusionType);

	if(settings_nh_->getParam("i2c_bus", temp_int))
		m_I2CBus = (unsigned char) temp_int;

	if(settings_nh_->getParam("i2c_slave_address", temp_int))
			m_I2CSlaveAddress = (unsigned char) temp_int;


	double declination_radians;
	settings_nh_->param("magnetic_declination", declination_radians, 0.0);
	m_compassAdjDeclination = angles::to_degrees(declination_radians);

	//MPU9150
	settings_nh_->getParam("mpu9150/gyro_accel_sample_rate", m_MPU9150GyroAccelSampleRate);
	settings_nh_->getParam("mpu9150/compass_sample_rate", m_MPU9150CompassSampleRate);
	settings_nh_->getParam("mpu9150/accel_full_scale_range", m_MPU9150AccelFsr);
	settings_nh_->getParam("mpu9150/gyro_accel_low_pass_filter", m_MPU9150GyroAccelLpf);
	settings_nh_->getParam("mpu9150/gyro_full_scale_range", m_MPU9150GyroFsr);

	//MPU9250
	settings_nh_->getParam("mpu9250/gyro_accel_sample_rate", m_MPU9250GyroAccelSampleRate);
	settings_nh_->getParam("mpu9250/compass_sample_rate", m_MPU9250CompassSampleRate);
	settings_nh_->getParam("mpu9250/accel_full_scale_range", m_MPU9250AccelFsr);
	settings_nh_->getParam("mpu9250/accel_low_pass_filter", m_MPU9250AccelLpf);
	settings_nh_->getParam("mpu9250/gyro_full_scale_range", m_MPU9250GyroFsr);
	settings_nh_->getParam("mpu9250/gyro_low_pass_filter", m_MPU9250GyroLpf);

	//GD20HM303D
	settings_nh_->getParam("GD20HM303D/gyro_sample_rate", m_GD20HM303DGyroSampleRate);
	settings_nh_->getParam("GD20HM303D/accel_sample_rate", m_GD20HM303DAccelSampleRate);
	settings_nh_->getParam("GD20HM303D/compass_sample_rate", m_GD20HM303DCompassSampleRate);
	settings_nh_->getParam("GD20HM303D/accel_full_scale_range", m_GD20HM303DAccelFsr);
	settings_nh_->getParam("GD20HM303D/gyro_full_scale_range", m_GD20HM303DGyroFsr);
	settings_nh_->getParam("GD20HM303D/compass_full_scale_range", m_GD20HM303DCompassFsr);
	settings_nh_->getParam("GD20HM303D/accel_low_pass_filter", m_GD20HM303DAccelLpf);
	settings_nh_->getParam("GD20HM303D/gyro_high_pass_filter", m_GD20HM303DGyroHpf);
	settings_nh_->getParam("GD20HM303D/gyro_bandwidth", m_GD20HM303DGyroBW);

	//GD20M303DLHC
	settings_nh_->getParam("GD20M303DLHC/gyro_sample_rate",m_GD20M303DLHCGyroSampleRate);
	settings_nh_->getParam("GD20M303DLHC/accel_sample_rate",m_GD20M303DLHCAccelSampleRate);
	settings_nh_->getParam("GD20M303DLHC/compass_sample_rate",m_GD20M303DLHCCompassSampleRate);
	settings_nh_->getParam("GD20M303DLHC/accel_full_scale_range",m_GD20M303DLHCAccelFsr);
	settings_nh_->getParam("GD20M303DLHC/gyro_full_scale_range",m_GD20M303DLHCGyroFsr);
	settings_nh_->getParam("GD20M303DLHC/compass_full_scale_range",m_GD20M303DLHCCompassFsr);
	settings_nh_->getParam("GD20M303DLHC/gyro_high_pass_filter",m_GD20M303DLHCGyroHpf);
	settings_nh_->getParam("GD20M303DLHC/gyro_bandwidth",m_GD20M303DLHCGyroBW);

	//GD20HM303DLHC
	settings_nh_->getParam("GD20HM303DLHC/gyro_sample_rate", m_GD20HM303DLHCGyroSampleRate);
	settings_nh_->getParam("GD20HM303DLHC/accel_sample_rate",m_GD20HM303DLHCAccelSampleRate);
	settings_nh_->getParam("GD20HM303DLHC/compass_sample_rate",m_GD20HM303DLHCCompassSampleRate);
	settings_nh_->getParam("GD20HM303DLHC/accel_full_scale_range",m_GD20HM303DLHCAccelFsr);
	settings_nh_->getParam("GD20HM303DLHC/gyro_full_scale_range",m_GD20HM303DLHCGyroFsr);
	settings_nh_->getParam("GD20HM303DLHC/compass_full_scale_range",m_GD20HM303DLHCCompassFsr);
	settings_nh_->getParam("GD20HM303DLHC/gyro_high_pass_filter",m_GD20HM303DLHCGyroHpf);
	settings_nh_->getParam("GD20HM303DLHC/gyro_bandwidth",m_GD20HM303DLHCGyroBW);

	//LSM9DS0
	settings_nh_->getParam("LSM9DS0/gyro_sample_rate",m_LSM9DS0GyroSampleRate);
	settings_nh_->getParam("LSM9DS0/accel_sample_rate",m_LSM9DS0AccelSampleRate);
	settings_nh_->getParam("LSM9DS0/compass_sample_rate",m_LSM9DS0CompassSampleRate);
	settings_nh_->getParam("LSM9DS0/accel_full_scale_range",m_LSM9DS0AccelFsr);
	settings_nh_->getParam("LSM9DS0/gyro_full_scale_range",m_LSM9DS0GyroFsr);
	settings_nh_->getParam("LSM9DS0/compass_full_scale_range",m_LSM9DS0CompassFsr);
	settings_nh_->getParam("LSM9DS0/accel_low_pass_filter",m_LSM9DS0AccelLpf);
	settings_nh_->getParam("LSM9DS0/gyro_high_pass_filter",m_LSM9DS0GyroHpf);
	settings_nh_->getParam("LSM9DS0/gyro_bandwidth",m_LSM9DS0GyroBW);

	std::vector<int> compass_max, compass_min;
	if (settings_nh_->getParam("calib/compass_min", compass_min)
			&& settings_nh_->getParam("calib/compass_max", compass_max)
			&& compass_min.size() == 3 && compass_max.size() == 3)
	{
		m_compassCalMin = RTVector3(compass_min[0], compass_min[1], compass_min[2]);
		m_compassCalMax = RTVector3(compass_max[0],compass_max[1], compass_max[2]);
		m_compassCalValid = true;
	}

	return true;
}

void I2cImu::spin()
{
	ros::Rate r(1.0 / (imu_->IMUGetPollInterval() / 1000.0));
	while (nh_.ok())
	{
		update();

		ros::spinOnce();
		r.sleep();
	}
}


int main(int argc, char** argv)
{
	ros::init(argc, argv, "i2c_imu_node");

	ROS_INFO("RTIMU Node for ROS");

	I2cImu i2c_imu;
	i2c_imu.spin();

	return (0);
}
// EOF
