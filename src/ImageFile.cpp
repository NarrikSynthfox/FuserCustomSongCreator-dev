#include "ImageFile.h"

#include <iostream>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

ImageFile::ImageFile()
{
	data = NULL;
}


ImageFile::~ImageFile()
{
	stbi_image_free(data);
}
//
//void BMPFile::VConversionInitialize(uint8_t* uncompressedImageData, unsigned int width, unsigned int height)
//{
//	m_pBmpHeader = new BITMAPFILEHEADER_BMP();
//	m_pBmpInfoHeader = new BITMAPINFOHEADER_BMP();
//	unsigned int imageSize = width * height * 3;
//	m_pBmpHeader->bfType = BF_TYPE_MB;
//	m_pBmpHeader->bfSize = imageSize + sizeof(BITMAPFILEHEADER_BMP) + sizeof(BITMAPINFOHEADER_BMP);
//	m_pBmpHeader->bfReserved1 = 0;
//	m_pBmpHeader->bfReserved2 = 0;
//	m_pBmpHeader->bfOffBits = sizeof(BITMAPFILEHEADER_BMP) + sizeof(BITMAPINFOHEADER_BMP);
//
//	m_pBmpInfoHeader->biSize = sizeof(BITMAPINFOHEADER_BMP);
//	m_pBmpInfoHeader->biWidth = width;
//	m_pBmpInfoHeader->biHeight = height;
//	m_pBmpInfoHeader->biPlanes = NUM_OF_PLANES;
//	m_pBmpInfoHeader->biBitCount = BIT_COUNT_24;
//	m_pBmpInfoHeader->biCompression = BI_RGB;
//	m_pBmpInfoHeader->biSizeImage = imageSize;
//	m_pBmpInfoHeader->biXPelsPerMeter = PIXELS_PER_METER;
//	m_pBmpInfoHeader->biYPelsPerMeter = PIXELS_PER_METER;
//	m_pBmpInfoHeader->biClrUsed = 0;
//	m_pBmpInfoHeader->biClrImportant = 0;
//
//	m_pixels = uncompressedImageData;
//}

int ImageFile::getWidth(){
	return width;
}

int ImageFile::getHeight(){
	return height;
}

uint8_t* ImageFile::getData() {
	uint8_t* pCopy = new uint8_t[width*height*channels];
	memcpy(pCopy, data, width * height * channels);

	return pCopy;
}

uint8_t* ImageFile::resizeAndGetData(int desired_width, int desired_height) {
	uint8_t* pCopy = new uint8_t[desired_width * desired_height * channels];
	uint8_t* iCopy = new uint8_t[width * height * channels];
	memcpy(iCopy, data, width * height * channels);
	bool ret = stbir_resize_uint8(iCopy, width, height, 0, pCopy, desired_width, desired_height, 0, 4);
	return pCopy;
}

bool ImageFile::FromFile(std::string filename) {
	// Load from disk into a raw RGBA buffer
	data = stbi_load(filename.c_str(), &width, &height, &channels, 4);
	channels = 4;
	if (data == NULL)
		return false;
	return true;
}

void ImageFile::FromBytes(uint8_t* bytes, int data_len, int width, int height) {
	// Load from disk into a raw RGBA buffer
	/*data = stbi_load(filename.c_str(), &width, &height, &channels, 4);
	channels = 4;
	if (data == NULL)
		return false;
	return true;*/
	data = new uint8_t[data_len];
	memcpy(data, bytes, data_len);
	this->width = width;
	this->height = height;
}


// Simple helper function to load an image into a DX11 texture with common settings
bool ImageFile::LoadTexture(ID3D11Device* g_pd3dDevice, ID3D11ShaderResourceView** out_srv)
{
	if (data == NULL)
		return false;
	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	return true;
}

void ImageFile::imgui_Display(ID3D11Device* g_pd3dDevice) {
	if (!texture_loaded) {
		bool ret = LoadTexture(g_pd3dDevice, &image_texture);
		IM_ASSERT(ret);
		texture_loaded = true;
	}
	ImGui::Image((void*)image_texture, ImVec2(128, 128));
}
