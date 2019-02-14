#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <Eigen/Core>
#include <opencv.hpp>
#include "DeviceArray.h"
#include "SophusUtil.h"

class Frame;
class GlViewer;
class Tracker;
class VoxelMap;
class AOTracker;
class PointCloud;
class ICPTracker;
class KeyFrameGraph;

// Msg is used for communicating
// with the system, mainly from
// the visualisation thread.
struct Msg
{
	Msg(int msg = 0) : data(msg) {}

	enum
	{
		EMPTY_MSG = 0,
		SYSTEM_RESET,
		EXPORT_MESH_TO_FILE,
		WRITE_BINARY_MAP_TO_DISK,
		READ_BINARY_MAP_FROM_DISK,
		SYSTEM_SHUTDOWN,
		TOGGLE_MESH_ON,
		TOGGLE_MESH_OFF,
		TOGGLE_IMAGE_ON,
		TOGGLE_IMAGE_OFF,
	};

	int data;
};

class SlamSystem
{
public:

	SlamSystem(int w, int h, Eigen::Matrix3f K);
	SlamSystem(const SlamSystem&) = delete;
	SlamSystem& operator=(const SlamSystem&) = delete;

	~SlamSystem();

	// Public APIs
	void trackFrame(cv::Mat& image, cv::Mat& depth, int id, double timeStamp);

	bool shouldQuit() const;
	void queueMessage(Msg newmsg);


public:

	// Message loop
	void processMessages();

	// Utils
	void rebootSystem();
	void exportMeshAsFile();
	void systemReInitialise();
	void writeBinaryMapToDisk();
	void readBinaryMapFromDisk();
	void updatePoseGraph(int nKeyFrame);
	void relocalise();

	// Try build pose graph
	void updateVisualisation();
	void findConstraintsForNewKFs(Frame* newKF);
	void checkConstraints();
	void tryTrackConstraint();
	void validateKeyPoints();
	bool needNewKeyFrame(SE3& poseUpdate);

	// Sub-routines
	VoxelMap* map;
	GlViewer* viewer;

	// General control variables
	bool keepRunning;
	bool systemRunning;

	// Camera intrinsics
	Eigen::Matrix3f K;

	// Image parameters
	int width, height;

	// Multi-threading loop
	void loopVisualisation();
	void loopOptimization();
	void loopMapGeneration();
	void loopConstraintSearch();
	bool Optimization(int it, float minDelta);
	bool newConstraintAdded;

	// Multi-Threading
	std::thread threadVisualisation;
	std::thread threadOptimization;
	std::thread threadMapGeneration;
	std::thread threadConstraintSearch;

	Frame* currentFrame;
	Frame* currentKeyFrame;
	Frame* latestTrackedFrame;
	KeyFrameGraph* keyFrameGraph;

	// Used for frame-to-model tracking
	ICPTracker* tracker;
	PointCloud* trackingReference;
	PointCloud* trackingTarget;

	// Used for constraint searching
	ICPTracker* constraintTracker;
	PointCloud* firstFrame;
	PointCloud* secondFrame;

	// Used for constraint searching
	std::deque<Frame*> newKeyFrames;
	std::mutex newKeyFrameMutex;

	std::mutex messageQueueMutex;
	std::queue<Msg> messageQueue;

	// used for debugging
	cv::Mat imageReference;
	cv::Mat imageTarget;
	cv::Mat depthReference;
	cv::Mat depthTarget;
	cv::Mat nmapReference;
	cv::Mat nmapTarget;
	void displayDebugImages(int ms);

	std::deque<Frame*> keyFramesToBeMapped;
	std::mutex keyFramesToBeMappedMutex;
	bool havePoseUpdate;

	// Key Frame Selection
	float entropyReference;
	float entropyRatio;
	bool isFirstFrame;

	// refactoring

public:

	struct TexturedPoint
	{
		TexturedPoint(Eigen::Vector3f& pos, cv::Vec3b& color) : position(pos), color(color) {}
		Eigen::Vector3f position;
		cv::Vec3b color;
	};

	void build_full_trajectory();
	void build_point_cloud();
	void save_point_cloud(std::string path);
	void load_groundtruth(std::vector<Sophus::SE3d> gt);
	void set_initial_pose(Sophus::SE3d initial_pose);

	Sophus::SE3d first_frame_pose;
	Sophus::SE3d motion_model;
	std::vector<Sophus::SE3d> full_trajectory;
	std::vector<TexturedPoint> point_cloud;
	std::vector<Sophus::SE3d> ground_truth_trajectory;
};

inline bool SlamSystem::shouldQuit() const
{
	return !systemRunning;
}
