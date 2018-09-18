#ifdef _WIN32
#include <io.h> 
#define access    _access_s
#else
#include <unistd.h>
#endif

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

#include "NPPJpegCoder.h"

inline bool isFileExists(const std::string &Filename) {
	return access(Filename.c_str(), 0) == 0;
}

int main(int argc, char* argv[]) {
	std::string input_dir(argv[1]);
	std::string serialnum(argv[2]);
	std::string output(argv[3]);
	std::string videoname;

	// get exist filenames
	cv::String path = input_dir;
	std::vector<cv::String> dirFiles;
	cv::glob(path, dirFiles);
	for (size_t k = 0; k < dirFiles.size(); k++) {
		std::size_t found = dirFiles[k].find(serialnum);
		if (found != std::string::npos) {
			videoname = dirFiles[k];
			break;
		}
	}

	std::cout << cv::format("Find bin file: %s", videoname.c_str()) << std::endl;

	int frameNum;
	int quality;
	int width;
	int height;

	cv::VideoWriter writer;
	npp::NPPJpegCoder coder;

	FILE* fp = fopen(videoname.c_str(), "rb");

	fread(&frameNum, sizeof(unsigned int), 1, fp);
	fread(&width, sizeof(int), 1, fp);
	fread(&height, sizeof(int), 1, fp);
	fread(&quality, sizeof(int), 1, fp);

	unsigned int length;
	char* data = new char[width * height];
	cv::cuda::GpuMat img(height, width, CV_8UC3);
	cv::Mat img_h;
	for (size_t i = 0; i < frameNum; i++) {
		printf("Decode frame %d, total %d frames.\n", i, frameNum);
		fread(&length, sizeof(unsigned int), 1, fp);
		fread(data, length, 1, fp);
		if (i == 0) {
			coder.init(width, height, quality);
			writer.open(output.c_str(), cv::VideoWriter::fourcc('D', 'I', 'V', 'X'), 10, cv::Size(width, height));
		}
		coder.decode(reinterpret_cast<unsigned char*>(data), length, img, 0);
		img.download(img_h);
		writer << img_h;
	}

	fclose(fp);
	writer.release();
	coder.release();

	return 0;
}