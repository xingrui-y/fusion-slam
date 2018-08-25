#include <iostream>
#include <vector>

#include "device_mapping.cuh"
#include "Tracking.hpp"
#include "Solver.hpp"
#include "Timer.hpp"

using namespace cv;

Tracking::Tracking():
mpMap(nullptr),
mpViewer(nullptr),
mnMapPoints(0),
mNextState(NOT_INITIALISED) {
	mORBMatcher = cuda::DescriptorMatcher::createBFMatcher(NORM_HAMMING);
}
int n = 0;
bool Tracking::Track(cv::Mat& imRGB, cv::Mat& imD) {

	Timer::Start("Tracking", "Create Frame");
	mNextFrame = Frame(imRGB, imD);
	Timer::Stop("Tracking", "Create Frame");

	mNextFrame.SetPose(Eigen::Matrix4d::Identity());

	bool bOK;
	switch (mNextState) {
	case NOT_INITIALISED:
		bOK = InitTracking();
		break;

	case OK:
		bOK = TrackLastFrame();
		break;

	case LOST:
		bOK = TrackMap(false);
		break;
	}

	if (!bOK) {
		bOK = TrackMap(false);
		if(!bOK)
			mNextState = LOST;
	} else {
		mLastFrame = Frame(mNextFrame);
		if(mNextState == OK) {
			mpMap->IntegrateKeys(mNextFrame);
			mpMap->CheckKeys(mNextFrame);
		}

		mNextState = OK;
	}

	return bOK;
}

bool Tracking::InitTracking() {

	mNextFrame.mOutliers.resize(mNextFrame.mNkp);
	fill(mNextFrame.mOutliers.begin(), mNextFrame.mOutliers.end(), false);
	return true;
}

bool Tracking::TrackMap(bool bUseGraphMatching) {

	mpMap->GetORBKeys(mMapPoints, mnMapPoints);
	cv::Mat desc(mnMapPoints, 32, CV_8UC1);
	if(mnMapPoints == 0)
		return false;

	vector<Eigen::Vector3d> Points;
	ORBKey* MapKeys = (ORBKey*)malloc(sizeof(ORBKey)*mnMapPoints);
	mMapPoints.download((void*)MapKeys, mnMapPoints);
	for(int i = 0; i < mnMapPoints; ++i) {
		ORBKey& key = MapKeys[i];
		for(int j = 0; j < 32; ++j) {
			desc.at<char>(i, j) = key.descriptor[j];
		}
		Eigen::Vector3d p;
		p << key.pos.x, key.pos.y, key.pos.z;
		Points.push_back(p);
	}

	cv::cuda::GpuMat mMapDesc(desc);
	std::vector<cv::DMatch> matches;
	std::vector<std::vector<cv::DMatch>> rawMatches;
	mORBMatcher->knnMatch(mNextFrame.mDescriptors, mMapDesc, rawMatches, 2);

	for (int i = 0; i < rawMatches.size(); ++i) {
		cv::DMatch& firstMatch = rawMatches[i][0];
		cv::DMatch& secondMatch = rawMatches[i][1];
		if (firstMatch.distance < 0.85 * secondMatch.distance) {
			matches.push_back(firstMatch);
		}
		else if(bUseGraphMatching) {
			matches.push_back(firstMatch);
			matches.push_back(secondMatch);
		}
	}
	cout << "1st : " << matches.size() << endl;
	if(matches.size() < 50)
		return false;

	std::vector<Eigen::Vector3d> plist;
	std::vector<Eigen::Vector3d> qlist;

	if(bUseGraphMatching) {
		std::vector<ORBKey> vFrameKey;
		std::vector<ORBKey> vMapKey;
		std::vector<float> vDistance;
		std::vector<int> vQueryIdx;
		cv::Mat cpuFrameDesc;
		mNextFrame.mDescriptors.download(cpuFrameDesc);
		cv::Mat cpuMatching(2, matches.size(), CV_32SC1);
		for(int i = 0; i < matches.size(); ++i) {
			int trainIdx = matches[i].trainIdx;
			int queryIdx = matches[i].queryIdx;
			ORBKey trainKey = MapKeys[trainIdx];
			ORBKey queryKey;
			if(trainKey.valid && queryKey.valid) {
				cv::Vec3f normal = mNextFrame.mNormals[queryIdx];
				Eigen::Vector3d& p = mNextFrame.mPoints[queryIdx];
				queryKey.pos = make_float3(p(0), p(1), p(2));
				queryKey.normal = make_float3(normal(0), normal(1), normal(2));
				vFrameKey.push_back(queryKey);
				vMapKey.push_back(trainKey);
				vDistance.push_back(matches[i].distance);
				vQueryIdx.push_back(queryIdx);
			}
		}

		DeviceArray<ORBKey> trainKeys(vMapKey.size());
		DeviceArray<ORBKey> queryKeys(vFrameKey.size());
		DeviceArray<float> MatchDist(vDistance.size());
		DeviceArray<int> QueryIdx(vQueryIdx.size());
		MatchDist.upload((void*)vDistance.data(), vDistance.size());
		trainKeys.upload((void*)vMapKey.data(), vMapKey.size());
		queryKeys.upload((void*)vFrameKey.data(), vFrameKey.size());
		QueryIdx.upload((void*)vQueryIdx.data(), vQueryIdx.size());
		cuda::GpuMat AdjecencyMatrix(matches.size(), matches.size(), CV_32FC1);
		DeviceArray<ORBKey> query_select, train_select;
		DeviceArray<int> SelectedIdx;
		BuildAdjecencyMatrix(AdjecencyMatrix, trainKeys, queryKeys, MatchDist,
				train_select, query_select, QueryIdx, SelectedIdx);

		std::vector<int> vSelectedIdx;
		std::vector<ORBKey> vORB_train, vORB_query;
		vSelectedIdx.resize(SelectedIdx.size());
		vORB_train.resize(train_select.size());
		vORB_query.resize(query_select.size());
		train_select.download((void*)vORB_train.data(), vORB_train.size());
		query_select.download((void*)vORB_query.data(), vORB_query.size());
		SelectedIdx.download((void*)vSelectedIdx.data(), vSelectedIdx.size());
		cout << "2nd : " << vSelectedIdx.size() << endl;
		for (int i = 0; i < query_select.size(); ++i) {
			Eigen::Vector3d p, q;
			if(vORB_query[i].valid &&
					vORB_train[i].valid) {
				bool redundant = false;
				for(int j = 0; j < i; j++) {
					if(vSelectedIdx[j] == vSelectedIdx[i]) {
						redundant = true;
						break;
					}
				}
				if(!redundant) {
					p << vORB_query[i].pos.x,
						 vORB_query[i].pos.y,
						 vORB_query[i].pos.z;
					q << vORB_train[i].pos.x,
						 vORB_train[i].pos.y,
						 vORB_train[i].pos.z;
					plist.push_back(p);
					qlist.push_back(q);
				}
			}
		}
	}
	else {
		for (int i = 0; i < matches.size(); ++i) {
			plist.push_back(mNextFrame.mPoints[matches[i].queryIdx]);
			qlist.push_back(Points[matches[i].trainIdx]);
		}
	}
	cout << "3rd : " << plist.size() << endl;

	Eigen::Matrix4d Td = Eigen::Matrix4d::Identity();
	bool bOK = Solver::SolveAbsoluteOrientation(plist, qlist, mNextFrame.mOutliers, Td, 100);
	cout << "last : " << count(mNextFrame.mOutliers.begin(), mNextFrame.mOutliers.end(), false) << endl;

	if(!bOK)
		return false;

	mNextFrame.SetPose(Td.inverse());
	return true;
}

void Tracking::UpdateMap() {
	mpMap->FuseFrame(mNextFrame);
}

bool Tracking::TrackLastFrame() {

	mNextFrame.SetPose(mLastFrame);

	Timer::Start("Tracking", "Track Frame");
	bool bOK = TrackFrame();
	Timer::Stop("Tracking", "Track Frame");

	if (!bOK)
		return false;

	Timer::Start("Tracking", "ICP");
	bOK = TrackICP();
	Timer::Stop("Tracking", "ICP");

	if (!bOK)
		return false;

	return true;
}

bool Tracking::TrackFrame() {

	std::vector<cv::DMatch> Matches;
	std::vector<std::vector<cv::DMatch>> matches;
	mORBMatcher->knnMatch(mNextFrame.mDescriptors, mLastFrame.mDescriptors,
			matches, 2);

	for (int i = 0; i < matches.size(); ++i) {
		cv::DMatch& firstMatch = matches[i][0];
		cv::DMatch& secondMatch = matches[i][1];
		if (firstMatch.distance < 0.85 * secondMatch.distance) {
			Matches.push_back(firstMatch);
		}
	}

	std::vector<Eigen::Vector3d> p;
	std::vector<Eigen::Vector3d> q;
	for (int i = 0; i < Matches.size(); ++i) {
		p.push_back(mNextFrame.mPoints[Matches[i].queryIdx]);
		q.push_back(mLastFrame.mPoints[Matches[i].trainIdx]);
	}

	Eigen::Matrix4d Td = Eigen::Matrix4d::Identity();
	bool bOK = Solver::SolveAbsoluteOrientation(p, q, mNextFrame.mOutliers, Td, 100);

	if(!bOK) {
		Eigen::Matrix3d rot = Td.inverse().topLeftCorner(3,3);
		Eigen::Vector3d ea = rot.eulerAngles(0, 1, 2).array().sin();
		Eigen::Vector3d trans = Td.inverse().topRightCorner(3, 1);
		if(fabs(ea(0)) > mRotThresh ||
		   fabs(ea(1)) > mRotThresh ||
		   fabs(ea(2)) > mRotThresh ||
		   fabs(trans(0)) > mTransThresh ||
		   fabs(trans(1)) > mTransThresh ||
		   fabs(trans(2)) > mTransThresh)
			return false;
	}

	Eigen::Matrix4d Tp = mLastFrame.mPose;
	Eigen::Matrix4d Tc = Eigen::Matrix4d::Identity();
	Tc = Td.inverse() * Tp;

	mNextFrame.SetPose(Tc);

	return true;
}

bool Tracking::TrackICP() {
	float cost = Solver::SolveICP(mNextFrame, mLastFrame);

	if(std::isnan(cost) || cost > 1e-3)
		return false;

	return true;
}

void Tracking::AddObservation(const Rendering& render) {
	mLastFrame = Frame(mLastFrame, render);
}

void Tracking::SetMap(Mapping* pMap) {
	mpMap = pMap;
}

void Tracking::SetViewer(Viewer* pViewer) {
	mpViewer = pViewer;
}

void Tracking::ShowResiduals() {

	DeviceArray2D<uchar> warpImg(640, 480);
	DeviceArray2D<uchar> residual(640, 480);
	warpImg.zero();
	residual.zero();
	WarpGrayScaleImage(mNextFrame, mLastFrame, residual);
	ComputeResidualImage(residual, warpImg, mNextFrame);
	cv::Mat cvresidual(480, 640, CV_8UC1);
	warpImg.download((void*) cvresidual.data, cvresidual.step);
	cv::imshow("residual", cvresidual);
}

void Tracking::ResetTracking() {
	mNextState = NOT_INITIALISED;
}
