#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <Eigen/Core>
#include <opencv.hpp>
#include "DeviceArray.h"

class Frame;
class GlViewer;
class Tracker;
class VoxelMap;
class PointCloud;
class ICPTracker;
class KeyFrameGraph;

// Msg is used for communicating
// with the system, mainly from
// the visualisation thread.
struct Msg
{
	Msg(int msg) : data(msg) {}

	enum
	{
		EMPTY_MSG,
		SYSTEM_RESET,
		EXPORT_MESH_TO_FILE,
		WRITE_BINARY_MAP_TO_DISK,
		READ_BINARY_MAP_FROM_DISK,
		SYSTEM_SHUTDOWN,
		TOGGLE_MESH_ON,
		TOGGLE_MESH_OFF
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
	bool shouldQuit() const;
	void queueMessage(Msg newmsg);
	void trackFrame(cv::Mat& image, cv::Mat& depth, int id, double timeStamp);

protected:

	void rebootSystem();
	void exportMeshAsFile();
	void processMessages();
	void systemReInitialise();
	void writeBinaryMapToDisk();
	void readBinaryMapFromDisk();
	void updateVisualisation();
	void findConstraintsForNewKeyFrames(Frame* newKF);

	VoxelMap* map;
	GlViewer* viewer;

	// General control variables
	bool keepRunning;
	bool systemRunning;
	bool dumpMapToDisk;

	// Camera intrinsics
	Eigen::Matrix3f K;

	// Image parameters
	int width, height;
	int nImgsProcessed;

	// Multi-threading loop
	void loopVisualisation();
	void loopOptimization();
	void loopConstraintSearch();

	// Multi-threading variables
	std::thread threadVisualisation;
	std::thread threadOptimization;
	std::thread threadConstraintSearch;

	Frame* currentKeyFrame;
	Frame* latestTrackedFrame;

	KeyFrameGraph* keyFrameGraph;

	// Used for frame-to-model tracking
	PointCloud* trackingReference;
	PointCloud* trackingTarget;
	ICPTracker* tracker;

	// Used for constraint searching
	std::deque<Frame*> newKeyFrames;
	std::mutex newKeyFrameMutex;

	std::mutex messageQueueMutex;
	std::queue<Msg> messageQueue;

	bool toggleShowMesh;
	bool toggleShowImage;
};

inline bool SlamSystem::shouldQuit() const
{
	return !systemRunning;
}