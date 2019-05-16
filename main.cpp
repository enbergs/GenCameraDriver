// include std
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <thread>
#include <memory>

// opencv
#include <opencv2/opencv.hpp>

// cuda
#ifdef _WIN32
#include <windows.h>
#endif
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include "GenCameraDriver.h"
#include "LinuxSocket.hpp"
#include "skversion.h"

inline std::string getTimeString()
{
	time_t timep;
	time(&timep);
	char tmp[64];
	strftime(tmp, sizeof(tmp), "__%Y_%m_%d_%H_%M_%S__", localtime(&timep));
	return tmp;
}

int record(int argc, char* argv[]) {
	std::vector<std::vector<cam::GenCamInfo>> A_camInfos;
	std::vector<std::shared_ptr<cam::GenCamera>> A_cameraPtr;
	bool wait = false, video = false, hard = false;
	int port = 0;
	int frameNum = 500;
	int brightness = 40;
	LxSoc lxsoc;
	std::string save_dir = "";
	double exposure = 0;
	std::vector<std::pair<std::string, double>> snExp;

	for (int i = 1; i < argc; i++)
	{
		std::string t = cam::SysUtil::toLower(std::string(argv[i]));
		if (t == "help")
		{
			std::cout <<
				"Help:\n" <<
				"Usage: ./GenCameraDriver [CameraType]([XIMEA],[PTGREY],[STEREO],[FILE [DIR]]) [frame [FrameCount]]\n" << 
				"[bright [BrightnessLevel]] [wait [WaitPort]] [video] [hard] [folder [folderName]] [exposure [expo_time_ms]]\n" <<
				"[expsn [sn] [expo_time_ms]]\n" <<
				"Sample1: \n{use ximea & file(video dir = \"./mp4s/\") camera type, save 200 frames, wait on sync signal on port 12344, save jpeg format, set brightness level at 40(default), use hardware sync}\n" <<
				"./GenCameraDriver XIMEA FILE ./mp4s/ frame 200 wait 12344 hard\n" <<
				"Sample2: \n{(use ptgrey camera type only, (save 500 frames(default)), save video format, set brightness level at 25)}\n" <<
				"./GenCameraDriver PTGREY video bright 25\n" <<
				std::endl;
			return 0;
		}
		else if (t == "ximea" || t == "x")
			A_cameraPtr.push_back(cam::createCamera(cam::CameraModel::XIMEA_xiC));
		else if (t == "ptgrey" || t == "pointgrey" || t == "p")
			A_cameraPtr.push_back(cam::createCamera(cam::CameraModel::PointGrey_u3));
		else if (t == "stereo" || t == "s")
			A_cameraPtr.push_back(cam::createCamera(cam::CameraModel::Stereo));
		else if (t == "file" || t == "f")
		{
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when use file camera, please specify dir\nSample: ./GenCameraDriver File ./mp4s/");
				return -1;
			}
			A_cameraPtr.push_back(cam::createCamera(cam::CameraModel::File, argv[i + 1]));
			i += 1;
		}
		else if (t == "video" || t == "v")
		{
			video = true;
		}
		else if (t == "exposure")//format : exposure [exposure = %f]
		{
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when specify exposure, please specify exposure time\nSample: ./GenCameraDriver XIMEA exposure 50");
				return -1;
			}
			exposure = atof(argv[i + 1]);
			i += 1;
		}
		else if (t == "bright")//format : bright [brightLevel = %d]
		{
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when specify brightness, please specify level\nSample: ./GenCameraDriver XIMEA bright 50");
				return -1;
			}
			brightness = atoi(argv[i + 1]);
			i += 1;
		}
		else if (t == "frame")//format : frame [frameNum = %d]
		{
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when specify frame count, please specify num\nSample: ./GenCameraDriver XIMEA frame 200");
				return -1;
			}
			frameNum = atoi(argv[i + 1]);
			i += 1;
		}
		else if (t == "wait")//format : wait [port = %d] 
		{
			
			wait = true;
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when use wait mode, please specify port\nSample: ./GenCameraDriver XIMEA wait 22336");
				return -1;
			}
			port = atoi(argv[i + 1]);
			i += 1;
			lxsoc.init(port);
		}
		else if (t == "hard")
		{
			hard = true;
		}
		else if (t == "folder")//format : folder [folderName]
		{
			if (i + 1 >= argc)
			{
				cam::SysUtil::errorOutput("when specify folder, please specify Name\nSample: ./GenCameraDriver XIMEA folder hello");
				return -1;
			}
			save_dir = argv[i + 1];
			i += 1;
			cam::SysUtil::infoOutput(cv::format("Save Folder = %s", save_dir.c_str()));
		}
		else if (t == "expsn")
		{
			if (i + 2 >= argc)
			{
				cam::SysUtil::errorOutput("expsn need sn and exposure time\nSample: ./GenCameraDriver XIMEA expsn CACU123 10.0");
				return -1;
			}
			snExp.push_back(std::pair<std::string, double>(argv[i + 1], atof(argv[i + 2])));
			i += 2;
		}
		else
		{
			cam::SysUtil::warningOutput("can't recognize argv = " + t);
		}
	}

	if(A_cameraPtr.size() == 0)
		A_cameraPtr.push_back(cam::createCamera(cam::CameraModel::XIMEA_xiC));

	//output
	{
		cam::SysUtil::infoOutput(video ? ("Video Save Mode ON") : ("Images Save Mode ON"));
		cam::SysUtil::infoOutput(hard ? ("Hardware Sync Mode ON") : ("Hardware Sync Mode OFF"));
		cam::SysUtil::infoOutput(cv::format("Record Frame Count = %d", frameNum));
		if(exposure > 1e-2)
		{
			cam::SysUtil::infoOutput(cv::format("Exposure Mode ON, Time = %lf ms", exposure));
		}
		else
		{
			cam::SysUtil::infoOutput(cv::format("Brightness Autolevel = %d", brightness));
		}
#ifndef WIN32
		if(wait)
			cam::SysUtil::infoOutput(cv::format("Wait Mode ON, will wait on port %d", port));
#endif
		if (snExp.size() > 0)
		{
			cam::SysUtil::infoOutput("Exposure Special Setting SN List = ");
			for (int i = 0; i < snExp.size(); i++)
			{
				cam::SysUtil::infoOutput(cv::format("SN = %s, exp = %lf ms", snExp[i].first.c_str(), snExp[i].second));
			}
		}

		for (int i = 0; i < A_cameraPtr.size(); i++)
		{
			cam::SysUtil::infoOutput("Will add camera type = " + A_cameraPtr[i]->getCamModelString());
		}
		cam::SysUtil::sleep(1000);
	}

	for (int i = 0; i < A_cameraPtr.size(); i++)
	{
		std::vector<cam::GenCamImgRatio> imgRatios;
		std::vector<cam::GenCamInfo> camInfos;
		std::shared_ptr<cam::GenCamera> cameraPtr = A_cameraPtr[i];
		cameraPtr->init();
		// set camera setting
		if(hard == true)
			cameraPtr->setSyncType(cam::GenCamSyncType::Hardware);
		cameraPtr->setFPS(-1, 10);
		cameraPtr->startCapture();
		if(exposure > 1e-2)
		{
			cameraPtr->setExposure(-1, exposure * 1000);
		}
		else
		{
			cameraPtr->setAutoExposure(-1, cam::Status::on);
			cameraPtr->setAutoExposureLevel(-1, brightness);
			cameraPtr->setAutoExposureCompensation(-1, cam::Status::on, -0.5);
		}
		//special SN setting
		cameraPtr->getCamInfos(camInfos);
		for (int j = 0; j < snExp.size(); j++)
			for (int k = 0; k < camInfos.size(); k++)
				if (camInfos[k].sn.find(snExp[j].first) != std::string::npos)
				{
					cameraPtr->setExposure(k, snExp[j].second * 1000);
					cam::SysUtil::infoOutput(cv::format(
						"Found SN = %s (with matching = %s), will set exposure to %lf ms",
						camInfos[k].sn.c_str(), snExp[j].first.c_str(), snExp[j].second));
				}
		//cameraPtr->setAutoWhiteBalance(-1);
		cameraPtr->setWhiteBalance(-1, 1.8, 1.0, 2.1); //only valid for ptgrey (ximea only work in rgb mode, but we use raw)
		cameraPtr->makeSetEffective();
		// set capturing setting
		cameraPtr->setCamBufferType(cam::GenCamBufferType::JPEG);
		cameraPtr->setJPEGQuality(90, 0.75);
		cameraPtr->setCaptureMode(cam::GenCamCaptureMode::Continous, frameNum);
		cameraPtr->setCapturePurpose(cam::GenCamCapturePurpose::Recording);
		cameraPtr->setVerbose(false);
		cameraPtr->makeSetEffective();
		cameraPtr->getCamInfos(camInfos);

		if (cameraPtr->getCamModelString() == "   XIMEA_xiC" && cam::SysUtil::existFile("./mul_mat.tiff"))
		{
			cv::Mat mul = cv::imread("./mul_mat.tiff", cv::IMREAD_UNCHANGED);
			cv::cuda::GpuMat mul_cuda_(mul);
			std::vector<cv::cuda::GpuMat> muls(camInfos.size(), mul_cuda_);
			cameraPtr->setBrightnessAdjustment(muls);
			cam::SysUtil::infoOutput("\nXIMEA camera & ./mul_mat.tiff has been found, will use this mat to deal with BrightnessAdjustment\nCheck main.cpp line "+ std::to_string(__LINE__) +std::string("\n"));
		}
		A_camInfos.push_back(camInfos);
		cam::SysUtil::sleep(1000);
		// set image ratios
		imgRatios.resize(camInfos.size());
		for (size_t i = 0; i < camInfos.size(); i++) {
			imgRatios[i] = static_cast<cam::GenCamImgRatio>(0);
			//imgRatios[i] = cam::GenCamImgRatio::Octopus;
		}
		cameraPtr->setImageRatios(imgRatios);
	}

	for (int i = 0; i < A_cameraPtr.size(); i++)
	{
		std::vector<cam::GenCamInfo> camInfos = A_camInfos[i];
		std::shared_ptr<cam::GenCamera> cameraPtr = A_cameraPtr[i];
		if (wait)
			cameraPtr->isStartRecord = false;
		cameraPtr->startCaptureThreads();
	}
	if (wait)
	{
		while (lxsoc.waitFor("Action") != 1);
		for (int i = 0; i < A_cameraPtr.size(); i++)
		{
			A_cameraPtr[i]->isStartRecord = true;
		}
	}

	if(save_dir == "")
		save_dir = getTimeString();

	for (int i = 0; i < A_cameraPtr.size(); i++)
	{
		std::vector<cam::GenCamInfo> camInfos = A_camInfos[i];
		std::shared_ptr<cam::GenCamera> cameraPtr = A_cameraPtr[i];
		cameraPtr->waitForRecordFinish();
		if(!video)
			cameraPtr->saveImages(save_dir);
		else
			cameraPtr->saveVideosGpu(save_dir);
		for (int i = 0; i < camInfos.size(); i++) {
			printf("%d:%s\n", i, camInfos[i].sn.c_str());
			printf("%d: width:%d height:%d\n", i, camInfos[i].width, camInfos[i].height);

		}
		cameraPtr->stopCaptureThreads();
		cameraPtr->stopCapture();
		cameraPtr->release();
	}
	return 0;
}



int main(int argc, char* argv[]) {
	cam::SysUtil::infoOutput(cv::format("Version = %d.%d.%s", __SK_MAJOR_VERSION__, __SK_MINOR_VERSION__, __GIT_VERSION__));
	record(argc, argv);
	return 0;
}

