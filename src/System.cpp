#include "System.hpp"
#include "Timer.hpp"

using namespace cv;
using namespace std;

System::System(const char* str):
mpMap(nullptr), mpViewer(nullptr),
mpTracker(nullptr), mpParam(nullptr),
mptViewer(nullptr), mbStop(false),
nFrames(0) {
	if(!str)
		System(static_cast<SysDesc*>(nullptr));
}

System::System(SysDesc* pParam):
mpMap(nullptr),
mpViewer(nullptr),
mpTracker(nullptr),
mbStop(false),
nFrames(0){

	mpMap = new Mapping();
	mpMap->AllocateDeviceMemory();

	mpViewer = new Viewer();
	mpTracker = new Tracking();

	mpViewer->SetMap(mpMap);
	mpViewer->SetSystem(this);
	mpViewer->SetTracker(mpTracker);

	mpTracker->SetMap(mpMap);

	if(pParam) {
		mpParam = new SysDesc();
		memcpy((void*)mpParam, (void*)pParam, sizeof(SysDesc));
	}
	else {
		mpParam = new SysDesc();
		mpParam->DepthScale = 1000.0f;
		mpParam->DepthCutoff = 8.0f;
		mpParam->fx = 525.0f;
		mpParam->fy = 525.0f;
		mpParam->cx = 320.0f;
		mpParam->cy = 240.0f;
		mpParam->cols = 640;
		mpParam->rows = 480;
		mpParam->TrackModel = true;
	}

	mK = Mat::eye(3, 3, CV_32FC1);
	mK.at<float>(0, 0) = mpParam->fx;
	mK.at<float>(1, 1) = mpParam->fy;
	mK.at<float>(0, 2) = mpParam->cx;
	mK.at<float>(1, 2) = mpParam->cy;
	Frame::SetK(mK);

	mptViewer = new thread(&Viewer::Spin, mpViewer);

	Frame::mDepthScale = mpParam->DepthScale;
	Frame::mDepthCutoff = mpParam->DepthCutoff;
}

void System::GrabImageRGBD(Mat& imRGB, Mat& imD) {

	bool bOK = mpTracker->Track(imRGB, imD);

	if (bOK) {
		int no = mpMap->FuseFrame(mpTracker->mNextFrame);
		Rendering rd;
		rd.cols = 640;
		rd.rows = 480;
		rd.fx = mK.at<float>(0, 0);
		rd.fy = mK.at<float>(1, 1);
		rd.cx = mK.at<float>(0, 2);
		rd.cy = mK.at<float>(1, 2);
		rd.Rview = mpTracker->mLastFrame.Rot_gpu();
		rd.invRview = mpTracker->mLastFrame.RotInv_gpu();
		rd.maxD = 5.0f;
		rd.minD = 0.1f;
		rd.tview = mpTracker->mLastFrame.Trans_gpu();

		mpMap->RenderMap(rd, no);
		mpTracker->AddObservation(rd);
//		Mat tmp(rd.rows, rd.cols, CV_8UC4);
//		rd.Render.download((void*) tmp.data, tmp.step);
//		resize(tmp, tmp, cv::Size(tmp.cols * 2, tmp.rows * 2));
//		imshow("img", tmp);
//		mpMap->MeshScene();
		if(nFrames > 30) {
			nFrames = 0;
			mpMap->MeshScene();
		}
		nFrames++;
	}

	if(mbStop)
		exit(0);
}

void System::SaveMesh() {

	uint n = mpMap->MeshScene();
//	float3 * host_tri = mpMap->mHostMesh;
//	FILE *f = fopen("scene.stl", "wb+");
//	if (f != NULL) {
//		for (int i = 0; i < 80; i++)
//			fwrite(" ", sizeof(char), 1, f);
//		fwrite(&n, sizeof(int), 1, f);
//		float zero = 0.0f;
//		short attribute = 0;
//		for (uint i = 0; i < n; i++) {
//			fwrite(&zero, sizeof(float), 1, f);
//			fwrite(&zero, sizeof(float), 1, f);
//			fwrite(&zero, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 0].x, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 0].y, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 0].z, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 1].x, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 1].y, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 1].z, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 2].x, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 2].y, sizeof(float), 1, f);
//			fwrite(&host_tri[i * 3 + 2].z, sizeof(float), 1, f);
//			fwrite(&attribute, sizeof(short), 1, f);
//		}
//		fclose(f);
//	}
//	delete host_tri;
}

void System::Reboot() {
	mpMap->ResetDeviceMemory();
	mpTracker->ResetTracking();
}

void System::PrintTimings() {
	Timer::Print();
}

void System::Stop() {
	mbStop = true;
}

void System::JoinViewer() {

	while(!mbStop) {

	}
}
