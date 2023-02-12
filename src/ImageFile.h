#pragma once

//Sampo Siltanen 2016
//
//Class to hold BMP 24 bits per pixel filetype data

#include <string>
#include <stdint.h>
#include <d3d11.h>
#include <imgui/imgui.h>
#include "stb_image.h"
#include "stb_image_resize.h"


class ImageFile {
public:
	ImageFile();
	virtual ~ImageFile();

	void imgui_Display(ID3D11Device* g_pd3dDevice);
	bool FromFile(std::string filename);
	int getWidth();
	int getHeight();
	uint8_t* getData();
	uint8_t* resizeAndGetData(int desired_width, int desired_height);
	uint8_t* resizeAndGetDataWithFilter(int desired_width, int desired_height, stbir_filter filter);
	void FromBytes(uint8_t* bytes, int data_len, int width, int height);
private:
	bool LoadTexture(ID3D11Device* g_pd3dDevice, ID3D11ShaderResourceView** out_srv);
	bool texture_loaded = false;
	ID3D11ShaderResourceView* image_texture = NULL;

	uint8_t* data = NULL;
	int width;
	int height;
	int channels;
};