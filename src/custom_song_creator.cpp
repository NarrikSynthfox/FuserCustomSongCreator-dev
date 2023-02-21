#define NOMINMAX
#include <Windows.h>

#include "uasset.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "crc.h"
#include <optional>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <format>


#include <filesystem>
namespace fs = std::filesystem;

#include "custom_song_pak_template.h"
#include "ImageFile.h"
#include "DDSFile.h"
#include "stb_image_resize.h"
#include "moggcrypt/CCallbacks.h"
#include "moggcrypt/VorbisEncrypter.h"

#include "fuser_asset.h"

#include "bass/bass.h"

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

extern HWND G_hwnd;





struct AudioCtx {
	HSAMPLE currentMusic = 0;
	int currentDevice = -1;
	bool init = false;
	float volume;
};
AudioCtx gAudio;

void initAudio() {
	if (gAudio.init) {
		BASS_Free();
		gAudio.init = false;
	}

	if (!BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, 1)) {
		printf("Failed to set config: %d\n", BASS_ErrorGetCode());
		return;
	}

	if (!BASS_Init(gAudio.currentDevice, 44100, 0, G_hwnd, NULL)) {
		printf("Failed to init: %d\n", BASS_ErrorGetCode());
		return;
	}

	gAudio.init = true;
	gAudio.currentDevice = BASS_GetDevice();
	gAudio.volume = BASS_GetConfig(BASS_CONFIG_GVOL_SAMPLE) / 10000;
}

void playOgg(const std::vector<uint8_t>& ogg) {
	if (gAudio.currentMusic != 0) {
		BASS_SampleFree(gAudio.currentMusic);
	}
	gAudio.currentMusic = BASS_SampleLoad(TRUE, ogg.data(), 0, ogg.size(), 3, 0);
	if (gAudio.currentMusic == 0) {
		printf("Error while loading: %d\n", BASS_ErrorGetCode());
		return;
	}

	HCHANNEL ch = BASS_SampleGetChannel(gAudio.currentMusic, FALSE);
	if (!BASS_ChannelPlay(ch, TRUE)) {
		printf("Error while playing: %d\n", BASS_ErrorGetCode());
		return;
	}
}

void pauseMusic() {

}

void display_playable_audio(PlayableAudio& audio,std::string addString) {
	if (audio.oggData.empty()) {
		ImGui::Text("No ogg file loaded.");
		return;
	}

	auto active = BASS_ChannelIsActive(audio.channelHandle);
	if (active != BASS_ACTIVE_PLAYING) {
		std::string buttonText = "Play " + addString + " Audio";
		if (addString == "") {
			buttonText = "Play Audio";
		}
		if (ImGui::Button(buttonText.c_str())) {
			if (audio.audioHandle != 0) {
				BASS_SampleFree(audio.audioHandle);
			}
			audio.audioHandle = BASS_SampleLoad(TRUE, audio.oggData.data(), 0, audio.oggData.size(), 3, 0);
			if (audio.audioHandle == 0) {
				printf("Error while loading: %d\n", BASS_ErrorGetCode());
				return;
			}

			audio.channelHandle = BASS_SampleGetChannel(audio.audioHandle, FALSE);
			if (!BASS_ChannelPlay(audio.channelHandle, TRUE)) {
				printf("Error while playing: %d\n", BASS_ErrorGetCode());
				return;
			}
		}
	}
	else {
		std::string buttonText = "Stop Playing " + addString + " Audio";
		if (addString == "") {
			buttonText = "Stop Playing Audio";
		}
		if (ImGui::Button("Stop")) {
			BASS_ChannelStop(audio.channelHandle);
		}
	}
}

//////////////////////////////////////

struct ImGuiErrorModalManager {
	size_t error_id = 0;

	struct Error {
		size_t id;
		std::string message;
	};
	std::vector<Error> errors;

	std::string getErrorName(size_t id) {
		std::string name = "";
		name += "Error_" + std::to_string(id);
		return name;
	}

	void pushError(std::string error) {
		Error e;
		e.id = error_id++;
		e.message = error;
		ImGui::OpenPopup(getErrorName(e.id).c_str());
		errors.emplace_back(e);
	}

	void update() {

		for (auto it = errors.begin(); it != errors.end();) {
			auto&& e = *it;
			bool remove = false;

			if (ImGui::BeginPopupModal(getErrorName(e.id).c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text(e.message.c_str());
				ImGui::Separator();

				if (ImGui::Button("OK", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
					remove = true;
				}
				ImGui::SetItemDefaultFocus();
			}

			if (remove) {
				it = errors.erase(it);
			}
			else {
				++it;
			}
		}
	}
};
ImGuiErrorModalManager errorManager;

static void ErrorModal(const char* name, const char* msg) {
	if (ImGui::BeginPopupModal(name, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text(msg);
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();

		ImGui::EndPopup();
	}
}

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

template<typename T>
static void ChooseFuserEnum(const char* label, std::string& out) {
	auto values = T::GetValues();

	auto getter = [](void* data, int idx, const char** out_str) {
		auto&& d = reinterpret_cast<std::vector<std::string>*>(data);
		*out_str = (*d)[idx].c_str();
		return true;
	};

	int currentChoice = 0;
	for (size_t i = 0; i < values.size(); ++i) {
		if (values[i] == out) {
			currentChoice = i;
			break;
		}
	}

	if (ImGui::Combo(label, &currentChoice, getter, &values, values.size())) {
		out = values[currentChoice];
	}
}

template<typename T>
static void ChooseFuserEnum(const char* label, typename T::Value& out) {
	auto values = T::GetValues();

	auto getter = [](void* data, int idx, const char** out_str) {
		auto&& d = reinterpret_cast<std::vector<std::string>*>(data);
		*out_str = (*d)[idx].c_str();
		return true;
	};

	int currentChoice = static_cast<int>(out);
	if (ImGui::Combo(label, &currentChoice, getter, &values, values.size())) {
		out = static_cast<typename T::Value>(currentChoice);
	}
}

static std::optional<std::string> OpenFile(LPCSTR filter) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFileName;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (GetOpenFileNameA(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}

static std::optional<std::string> SaveFile(LPCSTR filter, LPCSTR ext, const std::string& fileName) {
	CHAR szFileName[MAX_PATH];

	// open a file name
	OPENFILENAME ofn;
	ZeroMemory(&szFileName, sizeof(szFileName));
	ZeroMemory(&ofn, sizeof(ofn));
	memcpy(szFileName, fileName.data(), std::min(fileName.size(), (size_t)MAX_PATH));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFileName;
	ofn.nMaxFile = sizeof(szFileName);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrDefExt = ext;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn)) {
		return std::string(ofn.lpstrFile);
	}

	return std::nullopt;
}

struct MainContext {
	std::string saveLocation;
	bool has_art;
	ImageFile art;
	ID3D11Device* g_pd3dDevice;
	struct CurrentPak {
		PakFile pak;
		AssetRoot root;
	};
	std::unique_ptr<CurrentPak> currentPak;

};
MainContext gCtx;

void load_file(DataBuffer&& dataBuf) {
	gCtx.has_art = false;
	gCtx.currentPak.reset();
	gCtx.currentPak = std::make_unique<MainContext::CurrentPak>();
	gCtx.saveLocation.clear();

	auto&& pak = gCtx.currentPak->pak;

	dataBuf.serialize(pak);
	int i = 0;
	for (auto&& e : pak.entries) {
		if (auto data = std::get_if<PakFile::PakEntry::PakAssetData>(&e.data)) {
			auto pos = e.name.find("DLC/Songs/");
			if (pos != std::string::npos) {
				std::string shortName;
				for (size_t i = 10; i < e.name.size(); ++i) {
					if (e.name[i] == '/') {
						break;
					}

					shortName += e.name[i];
				}

				//Double check we got the name correct
				if (e.name == ("DLC/Songs/" + shortName + "/Meta_" + shortName + ".uexp")) {
					gCtx.currentPak->root.shortName = shortName;
					//break;
				}
			}
			pos = e.name.find("UI/AlbumArt");
			if (pos != std::string::npos) {
				if (e.name.compare(e.name.length() - 5, 5, ".uexp") == 0) {
					if (auto texture = std::get_if<Texture2D>(&data->data.catagoryValues[0].value)) {
						pos = e.name.find("_small");
						if (pos == std::string::npos) {
							auto dds = DDSFile();
							dds.VInitializeFromRaw(&texture->mips[0].mipData[0], texture->mips[0].mipData.size(), texture->mips[0].width, texture->mips[0].height);
							gCtx.art = ImageFile();

							uint8_t* uncompressedImageData = new uint8_t[texture->mips[0].width * texture->mips[0].height * 4];
							auto width = texture->mips[0].width;
							auto height = texture->mips[0].height;

							uint8_t* uncompressedData_ = dds.VGetUncompressedImageData();
							for (int y = 0; y < height; y++) {
								for (int x = 0; x < width; x++) {
									uncompressedImageData[(x + width * y) * 4 + 0] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 0];
									uncompressedImageData[(x + width * y) * 4 + 1] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 1];
									uncompressedImageData[(x + width * y) * 4 + 2] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 2];
									uncompressedImageData[(x + width * y) * 4 + 3] = 255;
								}
							}
							gCtx.art.FromBytes(uncompressedImageData, texture->mips[0].width * texture->mips[0].height * 4, texture->mips[0].width, texture->mips[0].height);
							gCtx.has_art = true;
						}
					}
				}
			}
			i++;

		}
	}

	if (gCtx.currentPak->root.shortName.empty()) {
		printf("FATAL ERROR! No short name detected!");
		__debugbreak();
	}


	if (gCtx.has_art) {
		SongSerializationCtx ctx;
		ctx.loading = true;
		ctx.pak = &pak;
		gCtx.currentPak->root.serialize(ctx);
	}
	else {
		SongSerializationCtx ctx;
		ctx.loading = true;
		ctx.pak = &pak;
		gCtx.currentPak->root.serialize(ctx);
		std::string shortName = gCtx.currentPak->root.shortName;
		std::string songName = gCtx.currentPak->root.songName;
		std::string artistName = gCtx.currentPak->root.artistName;
		i32 bpm = gCtx.currentPak->root.bpm;
		std::string songKey = gCtx.currentPak->root.songKey;
		FuserEnums::KeyMode::Value keyMode = gCtx.currentPak->root.keyMode;
		FuserEnums::Genre::Value genre = gCtx.currentPak->root.genre;
		i32 year = gCtx.currentPak->root.year;
		std::vector<HmxAudio::PackageFile> celFusionPackageFile;
		std::vector<std::vector<HmxAudio::PackageFile>> celMoggFiles;
		std::vector<std::string> instrumentTypes;
		std::vector<std::string> celShortName;
		for (auto cel : gCtx.currentPak->root.celData) {
			celShortName.emplace_back(cel.data.shortName);
			auto&& fusionFile = cel.data.majorAssets[0].data.fusionFile.data;
			auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
			//auto &&mogg = asset.audio.audioFiles[0];

			HmxAudio::PackageFile fusionPackageFile;
			std::vector<HmxAudio::PackageFile> moggFiles;
			std::unordered_set<std::string> fusion_mogg_files;
			instrumentTypes.push_back(cel.data.instrument);

			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(file);
				}
			}
			celFusionPackageFile.emplace_back(fusionPackageFile);
			celMoggFiles.emplace_back(moggFiles);
		}

		DataBuffer dataBuf;
		dataBuf.buffer = (u8*)custom_song_pak_template;
		dataBuf.size = sizeof(custom_song_pak_template);
		load_file(std::move(dataBuf));

		gCtx.currentPak->root.shortName = shortName;
		gCtx.currentPak->root.songName = songName;
		gCtx.currentPak->root.artistName = artistName;
		gCtx.currentPak->root.bpm = bpm;
		gCtx.currentPak->root.songKey = songKey;
		gCtx.currentPak->root.keyMode = keyMode;
		gCtx.currentPak->root.genre = genre;
		gCtx.currentPak->root.year = year;

		int idx = 0;
		for (auto& cel : gCtx.currentPak->root.celData) {
			cel.data.shortName = celShortName[idx];
			auto&& fusionFile = cel.data.majorAssets[0].data.fusionFile.data;
			auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
			//auto &&mogg = asset.audio.audioFiles[0];

			cel.data.instrument = instrumentTypes[idx];
			HmxAudio::PackageFile* fusionPackageFile = nullptr;
			std::vector<HmxAudio::PackageFile*> moggFiles;
			std::unordered_set<std::string> fusion_mogg_files;
			if (celMoggFiles[idx].size() == 1) {
				asset.audio.audioFiles.erase(asset.audio.audioFiles.begin() + 1);
			}
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					file.resourceHeader = celFusionPackageFile[idx].resourceHeader;
					file.fileData = celFusionPackageFile[idx].fileData;
					file.fileName = celFusionPackageFile[idx].fileName;
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}
			
			int moggidx = 0;
			
			for (auto& mogg : moggFiles) {
				mogg->resourceHeader = celMoggFiles[idx][moggidx].resourceHeader;
				mogg->fileData = celMoggFiles[idx][moggidx].fileData;
				mogg->fileName = celMoggFiles[idx][moggidx].fileName;
				moggidx++;
			}
			auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
			auto& map = fusion.nodes.getNode("keymap");
			if (map.children.size() == 1) {
				map.children.emplace_back(map.children[0]);
				std::string str = hmx_fusion_parser::outputData(map);
				std::vector<std::uint8_t> vec(str.begin(), str.end());
				map = hmx_fusion_parser::parseData(vec);
				auto nodes1 = std::get<hmx_fusion_nodes*>(map.children[0].value);
				auto nodes2 = std::get<hmx_fusion_nodes*>(map.children[1].value);
				nodes1->getInt("max_note") = 71;
				nodes2->getInt("root_note") = 84;
				nodes2->getInt("min_note") = 72;
			}
			idx++;
		}

	}
	
}

void load_template() {
	gCtx.currentPak.reset();
	DataBuffer dataBuf;
	dataBuf.buffer = (u8*)custom_song_pak_template;
	dataBuf.size = sizeof(custom_song_pak_template);
	load_file(std::move(dataBuf));
}

void save_file() {
	SongSerializationCtx ctx;
	ctx.loading = false;
	ctx.pak = &gCtx.currentPak->pak;
	gCtx.currentPak->root.serialize(ctx);

	std::vector<u8> outData;
	DataBuffer outBuf;
	outBuf.setupVector(outData);
	outBuf.loading = false;
	gCtx.currentPak->pak.serialize(outBuf);
	outBuf.finalize();

	std::string basePath = fs::path(gCtx.saveLocation).parent_path().string() + "/";
	std::ofstream outPak(basePath + gCtx.currentPak->root.shortName + "_P.pak", std::ios_base::binary);
	outPak.write((char*)outBuf.buffer, outBuf.size);

	{
		PakSigFile sigFile;
		sigFile.encrypted_total_hash.resize(512);

		const u32 chunkSize = 64 * 1024;
		for (size_t start = 0; start < outBuf.size; start += chunkSize) {
			size_t end = start + chunkSize;
			if (end > outBuf.size) {
				end = outBuf.size;
			}

			sigFile.chunks.emplace_back(CRC::MemCrc32(outBuf.buffer + start, end - start));
		}

		std::vector<u8> sigOutData;
		DataBuffer sigOutBuf;
		sigOutBuf.setupVector(sigOutData);
		sigOutBuf.loading = false;
		sigFile.serialize(sigOutBuf);
		sigOutBuf.finalize();

		std::ofstream outPak(basePath + gCtx.currentPak->root.shortName + "_P.sig", std::ios_base::binary);
		outPak.write((char*)sigOutBuf.buffer, sigOutBuf.size);
	}
}

bool Error_InvalidFileName = false;
void select_save_location() {
	auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
	auto location = SaveFile("Fuser Custom Song (*.pak)\0*.pak\0", "pak", fileName);
	if (location) {
		auto path = fs::path(*location);
		auto savedFileName = path.stem().string() + path.extension().string();
		if (savedFileName != fileName) {
			Error_InvalidFileName = true;
		}
		else {
			gCtx.saveLocation = *location;
			save_file();
		}
	}
}

static int ValidateShortName(ImGuiInputTextCallbackData* data) {
	if (!isalnum(data->EventChar) && data->EventChar != '_') {
		return 1;
	}

	return 0;
}

void display_main_properties() {
	auto&& root = gCtx.currentPak->root;

	if (ImGui::InputText("Short Name", &root.shortName, ImGuiInputTextFlags_CallbackCharFilter, ValidateShortName)) {
		gCtx.saveLocation.clear(); //We clear the save location, since it needs to resolve to another file path.
	}

	ImGui::SameLine();
	HelpMarker("Short name can only contain alphanumeric characters and '_'. This name is used to uniquely identify your custom song.");

	ImGui::InputText("Song Name", &root.songName);
	ImGui::InputText("Artist Name", &root.artistName);

	ImGui::InputScalar("BPM", ImGuiDataType_S32, &root.bpm);
	ChooseFuserEnum<FuserEnums::Key>("Key", root.songKey);
	ChooseFuserEnum<FuserEnums::KeyMode>("Mode", root.keyMode);
	ChooseFuserEnum<FuserEnums::Genre>("Genre", root.genre);
	ImGui::InputScalar("Year", ImGuiDataType_S32, &root.year);
}
//#include "stb_image_write.h"

void update_texture(std::string filepath, AssetLink<IconFileAsset> icon) {
	rgbcx::init();
	auto texture = &std::get<Texture2D>(icon.data.file.e->getData().data.catagoryValues[0].value);

	for (int mip_index = 0; mip_index < texture->mips.size(); mip_index++) {
		texture->mips[mip_index].width = (texture->mips[mip_index].width + 3) & ~3;
		texture->mips[mip_index].height = (texture->mips[mip_index].height + 3) & ~3;



		uint8_t* raw_data = gCtx.art.resizeAndGetData(texture->mips[mip_index].width, texture->mips[mip_index].height);

		auto width = texture->mips[mip_index].width;
		auto height = texture->mips[mip_index].height;
		
		uint8_t* compressedData = new uint8_t[width * height / 2];

		
		for (int y = 0; y < height; y += 4) {
			for (int x = 0; x < width; x += 4) {
				uint8_t* inData = new uint8_t[64]{ 0 };
				for (int ypx = 0; ypx < 4; ypx++)
				{
					for (int xpx = 0; xpx < 4; xpx++)
					{
						for (int i = 0; i < 4; i++)
						{
							inData[(((ypx*4)+xpx)*4) + i] = raw_data[((((y+ypx) * width) + (x+xpx)) * 4) + i];
						}
					}
				}
				uint8_t* block = &compressedData[(y / 4 * width / 4 + x / 4) * 8];
				rgbcx::encode_bc1(10,block, inData,true,false);
				delete[] inData;
			}
		}

		texture->mips[mip_index].mipData.clear();

		auto len = width*height/2;
		for (int i = 0; i < len; i++) {
			texture->mips[mip_index].mipData.push_back(compressedData[i]);
		}
		texture->mips[mip_index].len_1 = len;
		texture->mips[mip_index].len_2 = len;
		delete[] compressedData;
		delete[] raw_data;
	}
}

void display_album_art() {
	auto&& root = gCtx.currentPak->root;
	
	ImGui::Text("Album art resizes to 512x512px for small and 1080x1080px for large. Accepted formats: bmp,png,jpg,jpeg");
	if (ImGui::Button("Import Album Art")) {
		auto file = OpenFile("Image File\0*.bmp;*.png;*.jpg;*.jpeg\0");
		if (file) {
			gCtx.art = ImageFile();
			gCtx.art.FromFile(file.value());

			update_texture(file.value(), root.large_icon_link);
			update_texture(file.value(), root.small_icon_link);


			gCtx.has_art = true;
		}
	}
	if (gCtx.has_art) {
		gCtx.art.imgui_Display(gCtx.g_pd3dDevice);
	}

}

std::string lastMoggError;
void display_mogg_settings(FusionFileAsset& fusionFile, size_t idx, HmxAudio::PackageFile& mogg, std::string addString) {
	
	auto&& header = std::get<HmxAudio::PackageFile::MoggSampleResourceHeader>(mogg.resourceHeader);
	std::string buttonText = "Replace "+addString+" Audio";
	if (addString == "") {
		buttonText = "Replace Audio";
	}
	if (fusionFile.playableMoggs.size() <= idx) {
		fusionFile.playableMoggs.resize(idx + 1);
	}
	if (ImGui::Button(buttonText.c_str())) {
		auto moggFile = OpenFile("Ogg file (*.ogg)\0*.ogg\0");
		if (moggFile) {
			std::ifstream infile(*moggFile, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
			std::vector<u8> outData;

			try {
				VorbisEncrypter ve(&infile, 0x10, cppCallbacks);
				char buf[8192];
				size_t read = 0;
				size_t offset = 0;
				do {
					outData.resize(outData.size() + sizeof(buf));
					read = ve.ReadRaw(outData.data() + offset, 1, 8192);
					offset += read;
				} while (read != 0);

				header.sample_rate = ve.sample_rate;
			}
			catch (std::exception& e) {
				lastMoggError = e.what();
			}

			if (outData.size() > 0 && outData[0] == 0x0B) {
				mogg.fileData = std::move(outData);
				fusionFile.playableMoggs[idx].oggData = std::move(fileData);
			}
			else {
				ImGui::OpenPopup("Ogg loading error");
			}
		}
	}



	display_playable_audio(fusionFile.playableMoggs[idx],addString);

	ImGui::InputScalar("Sample Rate", ImGuiDataType_U32, &header.sample_rate);

	ErrorModal("Ogg loading error", ("Failed to load ogg file:" + lastMoggError).c_str());
}
void display_keyzone_settings(hmx_fusion_nodes* keyzone, std::vector<HmxAudio::PackageFile*> moggFiles) {
	int itemWidth = 300;
	ImGui::PushItemWidth(itemWidth);
	ImGui::InputText("Keymap Label", &keyzone->getString("zone_label"));
	bool unp = keyzone->getInt("unpitched") == 1;
	bool unp_changed = ImGui::Checkbox("Unpitched", &unp);
	if (unp_changed) {
		if (unp)
			keyzone->getInt("unpitched") = 1;
		else
			keyzone->getInt("unpitched") = 0;
	}
	auto&& ts = keyzone->getNode("timestretch_settings");

	bool natp = ts.getInt("maintain_formant") == 1;
	bool natp_changed = ImGui::Checkbox("Natural Pitching", &natp);
	if (natp_changed) {
		if (natp)
			ts.getInt("maintain_formant") = 1;
		else
			ts.getInt("maintain_formant") = 0;
	}

	static int selectedAudioFile = 0;


	std::vector<std::string> fileNames;
	for (auto mogg : moggFiles) {
		fileNames.emplace_back(mogg->fileName);
	}
	auto it = std::find(fileNames.begin(), fileNames.end(), keyzone->getString("sample_path"));
	if (it != fileNames.end())
		selectedAudioFile = std::distance(fileNames.begin(), it);
	else
		selectedAudioFile = 0;


	if (ImGui::BeginCombo("Sample Path", fileNames[selectedAudioFile].c_str())) {
		for (int i = 0; i < fileNames.size(); ++i)
		{
			bool is_selected = (selectedAudioFile == i);
			if (ImGui::Selectable(fileNames[i].c_str(), is_selected))
			{
				selectedAudioFile = i;
				keyzone->getString("sample_path") = fileNames[i];
			}
			if (is_selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::PopItemWidth();
	if (ImGui::CollapsingHeader("Advanced Keymap Settings")) {
		ImGui::PushItemWidth(itemWidth);
		ImGui::InputScalar("Map - Min Note", ImGuiDataType_U32, &keyzone->getInt("min_note"));
		ImGui::SameLine();
		HelpMarker("The lowest midi note that the selected sample will play.");

		ImGui::InputScalar("Map - Highest Note", ImGuiDataType_U32, &keyzone->getInt("max_note"));
		ImGui::SameLine();
		HelpMarker("The highest midi note that the selected sample will play.");

		ImGui::InputScalar("Map - Root Note", ImGuiDataType_U32, &keyzone->getInt("root_note"));
		ImGui::SameLine();
		HelpMarker("The note at which that the selected sample will play at its original pitch.");

		ImGui::InputScalar("Map - Min Velocity", ImGuiDataType_U32, &keyzone->getInt("min_velocity"));
		ImGui::SameLine();
		HelpMarker("The lowest midi note velocity at which the selected sample will play.");

		ImGui::InputScalar("Map - Max Velocity", ImGuiDataType_U32, &keyzone->getInt("max_velocity"));
		ImGui::SameLine();
		HelpMarker("The highest midi note velocity at which the selected sample will play.");

		ImGui::InputScalar("Audio - Start Offset", ImGuiDataType_S32, &keyzone->getInt("start_offset_frame"));
		ImGui::SameLine();
		HelpMarker("The offset, in samples, that the audio will start playing from.");

		ImGui::InputScalar("Audio - End Offset", ImGuiDataType_S32, &keyzone->getInt("end_offset_frame"));
		ImGui::SameLine();
		HelpMarker("The offset, in samples, that the audio will stop playing at.");
		ImGui::PopItemWidth();
	}

}

void display_cel_audio_options(CelData& celData, HmxAssetFile& asset, std::vector<HmxAudio::PackageFile*>& moggFiles, FusionFileAsset& fusionFile, HmxAudio::PackageFile* fusionPackageFile, bool duplicate_moggs,bool isRiser = false)
{
	static int currentAudioFile = 0;
	static int currentKeyzone = 0;

	std::string celShortName = celData.shortName + (isRiser ? "_trans" : "");
	auto aRegion = ImGui::GetContentRegionAvail();

	auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
	auto& map = fusion.nodes.getNode("keymap");

	bool advanced = fusion.nodes.getInt("edit_advanced") == 1;
	std::string advBtn = "Switch to Advanced Mode";
	if (advanced)
		advBtn = "Switch to Simple Mode";

	if (ImGui::Button(advBtn.c_str()))
		ImGui::OpenPopup("Switch Modes?");

	if (ImGui::BeginPopupModal("Switch Modes?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::BeginChild("PopupHolder", ImVec2(420, 100));
		if (advanced) {
			
			ImGui::BeginChild("Text", ImVec2(420, 69));
			ImGui::Text(celShortName.c_str());
			ImGui::Text("Are you sure you want to switch to Simple Mode?");
			ImGui::TextWrapped("WARNING: This will reset audio file and keymap count, as well as the keymap midi, velocity, and offset settings.");
			ImGui::EndChild();
			ImGui::BeginChild("Buttons", ImVec2(420, 25));
			if (ImGui::Button("Yes", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				
				currentAudioFile = 0;
				currentKeyzone = 0;
				int audioFileCount = 0;
				for (auto it = asset.audio.audioFiles.begin(); it != asset.audio.audioFiles.end();) {
					if (it->fileType == "MoggSampleResource") {
						if (audioFileCount == 2) {
							it = asset.audio.audioFiles.erase(it);
						}
						else {
							it->fileName = "C:/" + celShortName + "_" + std::to_string(audioFileCount) + ".mogg";
							audioFileCount++;
							++it;
						}
					}
					else {
						++it;
					}
				}
				moggFiles.clear();
				for (auto&& file : asset.audio.audioFiles) {
					if (file.fileType == "FusionPatchResource") {
						fusionPackageFile = &file;
					}
					else if (file.fileType == "MoggSampleResource") {
						moggFiles.emplace_back(&file);
					}
				}
				if (map.children.size() == 1) {
					map.children.emplace_back(map.children[0]);
					std::string str = hmx_fusion_parser::outputData(map);
					std::vector<std::uint8_t> vec(str.begin(), str.end());
					map = hmx_fusion_parser::parseData(vec);
				}
				else if(map.children.size()>2)
					map.children.resize(2);

				std::vector<hmx_fusion_nodes*>nodes;
				nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[0].value));
				nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[1].value));

				nodes[0]->getString("zone_label") = "Major";
				nodes[0]->getInt("min_note") = 0;
				nodes[0]->getInt("root_note") = 60;
				nodes[0]->getInt("max_note") = 71;
				nodes[1]->getString("zone_label") = "Minor";
				nodes[1]->getInt("min_note") = 72;
				nodes[1]->getInt("root_note") = 84;
				nodes[1]->getInt("max_note") = 127;
				int idx = 0;
				for (auto c : nodes) {
					c->getInt("min_velocity") = 0;
					c->getInt("max_velocity") = 127;
					c->getInt("start_offset_frame") = -1;
					c->getInt("end_offset_frame") = -1;
					c->getString("sample_path") = moggFiles[idx]->fileName;
					if (moggFiles.size() != 1) {
						idx++;
					}
				}

				fusion.nodes.getInt("edit_advanced") = 0;
				advanced = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndChild();
		}
		else {
			ImGui::BeginChild("Text", ImVec2(420, 69));
			ImGui::Text("Switch to Advanced Mode?");
			ImGui::EndChild();
			ImGui::BeginChild("Buttons", ImVec2(420, 25));
			if (ImGui::Button("Yes", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				fusion.nodes.getInt("edit_advanced") = 1;
				advanced = true;
				currentAudioFile = 0;
				currentKeyzone = 0;
			}
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}

	if (advanced) {
		ImGui::BeginChild("Audio", ImVec2(aRegion.x, aRegion.y / 3));
		if (currentAudioFile >= moggFiles.size())
			currentAudioFile = 0;
		ImGui::BeginChild("AudioTableHolder", ImVec2(aRegion.x / 3, (aRegion.y / 3)));
		if (ImGui::BeginTable("AudioTable", 2, 0, ImVec2(aRegion.x / 3, (aRegion.y / 3) - 50))) {
			ImGui::TableSetupColumn("Index", 0, 0.2);
			ImGui::TableSetupColumn("Audio File");
			ImGui::TableHeadersRow();
			for (int i = 0; i < moggFiles.size(); i++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable((std::to_string(i)).c_str(), currentAudioFile == i, ImGuiSelectableFlags_SpanAllColumns)) {
					currentAudioFile = i;
				}
				ImGui::TableNextColumn();
				ImGui::Text((moggFiles[i]->fileName).c_str());
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("Add Audio File")) {
			HmxAudio::PackageFile newFile = *moggFiles[0];
			int i = 0;
			bool unique = false;
			while (!unique) {
				std::unordered_set<std::string> fileNames;
				for (auto mogg : moggFiles) {
					fileNames.insert(mogg->fileName);
				}
				if (fileNames.count("C:/" + celShortName + "_" + std::to_string(i) + ".mogg") > 0)
					i++;
				else
					unique = true;
			}

			newFile.fileName = "C:/" + celShortName + "_" + std::to_string(i) + ".mogg";

			asset.audio.audioFiles.insert(asset.audio.audioFiles.begin() + i, newFile);
			moggFiles.clear();
			int idx = 0;
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}

		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Audio File") && moggFiles.size() != 1) {
			auto selectedRemove = moggFiles[currentAudioFile];
			if (currentAudioFile == moggFiles.size() - 1)
				currentAudioFile--;
			auto it = std::find_if(
				asset.audio.audioFiles.begin(), asset.audio.audioFiles.end(),
				[&selectedRemove](const HmxAudio::PackageFile& p) {
					return &p == selectedRemove;
				}
			);
			asset.audio.audioFiles.erase(it);
			moggFiles.clear();
			for (auto&& file : asset.audio.audioFiles) {
				if (file.fileType == "FusionPatchResource") {
					fusionPackageFile = &file;
				}
				else if (file.fileType == "MoggSampleResource") {
					moggFiles.emplace_back(&file);
				}
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("AudioSettings", ImVec2((aRegion.x / 3) * 2, (aRegion.y / 3)));
		ImGui::Text((moggFiles[currentAudioFile]->fileName).c_str());
		display_mogg_settings(fusionFile, currentAudioFile, *moggFiles[currentAudioFile],"");
		ImGui::EndChild();

		ImGui::EndChild();

		ImGui::BeginChild("Keymap", ImVec2(aRegion.x, (aRegion.y / 2)));


		
		if (currentKeyzone >= map.children.size())
			currentKeyzone = 0;
		ImGui::BeginChild("KeyzoneTableHolder", ImVec2(aRegion.x / 3, (aRegion.y / 2)));
		if (ImGui::BeginTable("KeyzoneTable", 2, 0, ImVec2(aRegion.x / 3, (aRegion.y / 2) - 50))) {
			ImGui::TableSetupColumn("Index", 0, 0.2);
			ImGui::TableSetupColumn("Keyzone Label");
			ImGui::TableHeadersRow();
			for (int i = 0; i < map.children.size(); i++)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable((std::to_string(i)).c_str(), currentKeyzone == i, ImGuiSelectableFlags_SpanAllColumns)) {
					currentKeyzone = i;
				}
				ImGui::TableNextColumn();
				ImGui::Text(std::get<hmx_fusion_nodes*>(map.children[i].value)->getString("zone_label").c_str());
			}
			ImGui::EndTable();
		}

		if (ImGui::Button("Add Keyzone")) {
			map.children.emplace_back(map.children[0]);
			std::string str = hmx_fusion_parser::outputData(map);
			std::vector<std::uint8_t> vec(str.begin(), str.end());
			map = hmx_fusion_parser::parseData(vec);
			std::get<hmx_fusion_nodes*>(map.children[map.children.size() - 1].value)->getString("zone_label") = "New Zone";
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Keyzone") && map.children.size() != 1) {
			int mapToErase = currentKeyzone;
			if (currentKeyzone == map.children.size() - 1)
				currentKeyzone--;
			map.children.erase(map.children.begin() + mapToErase);
		}

		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("KeymapSettings", ImVec2((aRegion.x / 3) * 2, (aRegion.y / 2)));
		display_keyzone_settings(std::get<hmx_fusion_nodes*>(map.children[currentKeyzone].value), moggFiles);
		ImGui::EndChild();

		ImGui::EndChild();
	}
	else {
		bool duplicate_changed = false;
		if(duplicate_moggs){
			ImGui::Text("Duplicated");
			display_mogg_settings(fusionFile, 0, *moggFiles[0],"Duplicated");
		}
		else {
			ImGui::Text("Major");
			display_mogg_settings(fusionFile, 0, *moggFiles[0], "Major");
		}
		
		
		duplicate_changed = ImGui::Checkbox("Duplicate Audio?", &duplicate_moggs);

		if (duplicate_changed) {
			if (duplicate_moggs) {
				if (moggFiles.size() == 2) {
					asset.audio.audioFiles.erase(asset.audio.audioFiles.begin() + 1);

					moggFiles.clear();
					for (auto&& file : asset.audio.audioFiles) {
						if (file.fileType == "FusionPatchResource") {
							fusionPackageFile = &file;
						}
						else if (file.fileType == "MoggSampleResource") {
							moggFiles.emplace_back(&file);
						}
					}
				}

				auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
				auto map = fusion.nodes.getNode("keymap");

				if (map.children.size() == 2) {
					std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path") = std::get<hmx_fusion_nodes*>(map.children[0].value)->getString("sample_path");
				}
			}
			else {
				if (moggFiles.size() == 1) {
					auto newFile = *moggFiles[0];
					newFile.fileName = "C:/" + celShortName + "_1.mogg";
					asset.audio.audioFiles.insert(asset.audio.audioFiles.begin() + 1, newFile);

					moggFiles.clear();
					for (auto&& file : asset.audio.audioFiles) {
						if (file.fileType == "FusionPatchResource") {
							fusionPackageFile = &file;
						}
						else if (file.fileType == "MoggSampleResource") {
							moggFiles.emplace_back(&file);
						}
					}
				}

				auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
				auto map = fusion.nodes.getNode("keymap");

				if (map.children.size() == 2) {
					std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path") = "C:/" + celShortName + "_1.mogg";
				}
			}
		}
		ImGui::Spacing();

		if(!duplicate_moggs){
			ImGui::Text("Minor");
			display_mogg_settings(fusionFile, 1, *moggFiles[1],"Minor");
		}
		ImGui::Spacing();

		std::vector<hmx_fusion_nodes*>nodes;
		nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[0].value));
		nodes.emplace_back(std::get<hmx_fusion_nodes*>(map.children[1].value));
		bool unp = nodes[0]->getInt("unpitched") == 1;
		bool unp_changed = ImGui::Checkbox("Unpitched", &unp);
		if (unp_changed) {
			if (unp) {
				nodes[0]->getInt("unpitched") = 1;
				nodes[1]->getInt("unpitched") = 1;
			}
			else {
				nodes[0]->getInt("unpitched") = 0;
				nodes[1]->getInt("unpitched") = 0;
			}
		}
		auto&& ts = nodes[0]->getNode("timestretch_settings");
		auto&& ts2 = nodes[1]->getNode("timestretch_settings");

		bool natp = ts.getInt("maintain_formant") == 1;
		bool natp_changed = ImGui::Checkbox("Natural Pitching", &natp);
		if (natp_changed) {
			if (natp) {
				ts.getInt("maintain_formant") = 1;
				ts2.getInt("maintain_formant") = 1;
			}
			else {
				ts.getInt("maintain_formant") = 0;
				ts2.getInt("maintain_formant") = 0;
			}
		}
	}
}
void display_cell_data(CelData& celData, FuserEnums::KeyMode::Value currentKeyMode, bool advancedMode = false) {
	ChooseFuserEnum<FuserEnums::Instrument>("Instrument", celData.instrument);
	
	

	auto&& fusionFile = celData.majorAssets[0].data.fusionFile.data;
	
	auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
	//auto &&mogg = asset.audio.audioFiles[0];

	HmxAudio::PackageFile* fusionPackageFile = nullptr;
	std::vector<HmxAudio::PackageFile*> moggFiles;
	std::unordered_set<std::string> fusion_mogg_files;


	bool disc_advanced;
	{
		for (auto&& file : asset.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFile = &file;
			}
			else if (file.fileType == "MoggSampleResource") {
				moggFiles.emplace_back(&file);
			}
		}

		auto&& fusion = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFile->resourceHeader);
		auto map = fusion.nodes.getNode("keymap");


		if (fusion.nodes.getChild("edit_advanced") == nullptr) {
			hmx_fusion_node label;
			label.key = "edit_advanced";
			label.value = 0;
			fusion.nodes.children.insert(fusion.nodes.children.begin(), label);
			disc_advanced = false;
		}
		else {
			disc_advanced = fusion.nodes.getInt("edit_advanced") == 1;
		}
		int mapidx = 0;
		for (auto c : map.children) {
			auto nodes = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_files.emplace(nodes->getString("sample_path"));
			if (nodes->getChild("zone_label") == nullptr) {
				hmx_fusion_node label;
				label.key = "zone_label";
				if (mapidx == 0) {
					label.value = "Major";
				}
				else if (mapidx == 1) {
					label.value = "Minor";
				}
				else {
					label.value = "UNKNOWN";
				}
				nodes->children.insert(nodes->children.begin(), label);
			}
			mapidx++;
		}
	}

	bool duplicate_moggs = fusion_mogg_files.size() == 1;

	// Riser Moggs

	auto&& fusionFileRiser = celData.songTransitionFile.data.majorAssets[0].data.fusionFile.data;
	auto&& assetRiser = std::get<HmxAssetFile>(fusionFileRiser.file.e->getData().data.catagoryValues[0].value);
	//auto &&mogg = asset.audio.audioFiles[0];

	HmxAudio::PackageFile* fusionPackageFileRiser = nullptr;
	std::vector<HmxAudio::PackageFile*> moggFilesRiser;
	std::unordered_set<std::string> fusion_mogg_filesRiser;

	bool rise_advanced;
	{
		for (auto&& file : assetRiser.audio.audioFiles) {
			if (file.fileType == "FusionPatchResource") {
				fusionPackageFileRiser = &file;
			}
			else if (file.fileType == "MoggSampleResource") {
				moggFilesRiser.emplace_back(&file);
			}
		}

		auto&& fusionRiser = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFileRiser->resourceHeader);
		auto mapRiser = fusionRiser.nodes.getNode("keymap");


		if (fusionRiser.nodes.getChild("edit_advanced") == nullptr) {
			hmx_fusion_node label;
			label.key = "edit_advanced";
			label.value = 0;
			fusionRiser.nodes.children.insert(fusionRiser.nodes.children.begin(), label);
			rise_advanced = false;
		}
		else {
			rise_advanced = fusionRiser.nodes.getInt("edit_advanced") == 1;
		}
		int mapidx = 0;
		for (auto c : mapRiser.children) {
			auto nodesRiser = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_filesRiser.emplace(nodesRiser->getString("sample_path"));
			if (nodesRiser->getChild("zone_label") == nullptr) {
				hmx_fusion_node label;
				label.key = "zone_label";
				if (mapidx == 0) {
					label.value = "Major";
				}
				else if (mapidx == 1) {
					label.value = "Minor";
				}
				else {
					label.value = "UNKNOWN";
				}
				nodesRiser->children.insert(nodesRiser->children.begin(), label);
			}
			mapidx++;
		}
	}
	
	bool duplicate_moggsRiser = fusion_mogg_filesRiser.size() == 1;

	ImGui::NewLine();
	
	
	auto windowSize = ImGui::GetWindowSize();

	auto oggWindowSize = ImGui::GetContentRegionAvail().y - 25;
	if (ImGui::BeginTabBar("CelDataEditTabs")) {
		if (ImGui::BeginTabItem("Disc Audio")) {

			ImGui::BeginChild("AudioSettingsDisc", ImVec2((windowSize.x / 3) * 2, oggWindowSize));
			display_cel_audio_options(celData, asset, moggFiles, fusionFile, fusionPackageFile, duplicate_moggs,false);
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("Advanced - Disc", ImVec2(windowSize.x / 3, oggWindowSize));
			if (ImGui::Button("Export Disc Fusion File")) {
				auto file = SaveFile("Fusion Text File (.fusion)\0*.fusion\0", "fusion", "");
				if (file) {
					for (auto&& f : asset.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {

							std::ofstream outFile(*file);
							std::string outStr = hmx_fusion_parser::outputData(std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes);
							outFile << outStr;

							break;
						}
					}
				}
			}

			if (ImGui::Button("Import Disc Fusion File")) {
				auto file = OpenFile("Fusion Text File (.fusion)\0*.fusion\0");
				if (file) {
					for (auto&& f : asset.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {

							std::ifstream infile(*file, std::ios_base::binary);
							std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
							std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes = hmx_fusion_parser::parseData(fileData);

							break;
						}
					}
				}
			}

			ImGui::Spacing();

			bool overwrite_midi = false;
			bool maj = true;

			if (ImGui::Button("Overwrite Disc Major Midi File")) {
				overwrite_midi = true;
			}
			else if (ImGui::Button("Overwrite Disc Minor Midi File")) {
				overwrite_midi = true;
				maj = false;
			}

			if (overwrite_midi) {
				auto file = OpenFile("Harmonix Midi Resource File (.mid_pc)\0*.mid_pc\0");
				if (file) {
					AssetLink<MidiSongAsset>* midiSong = nullptr;
					if (maj) {
						midiSong = &celData.majorAssets[0];
					}
					else {
						midiSong = &celData.minorAssets[0];
					}

					auto&& midi_file = midiSong->data.midiFile.data;
					auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);

					std::ifstream infile(*file, std::ios_base::binary);
					std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
					midiAsset.audio.audioFiles[0].fileData = std::move(fileData);
				}
			}

			ImGui::Spacing();

			bool export_midi = false;

			if (ImGui::Button("Export Disc Major Midi File")) {
				export_midi = true;
				maj = true;
			}
			else if (ImGui::Button("Export Disc Minor Midi File")) {
				export_midi = true;
				maj = false;
			}

			if (export_midi) {
				auto file = SaveFile("Harmonix Midi Resource File (.mid_pc)\0*.mid_pc\0", "mid_pc", "");
				if (file) {
					AssetLink<MidiSongAsset>* midiSong = nullptr;
					if (maj) {
						midiSong = &celData.majorAssets[0];
					}
					else {
						midiSong = &celData.minorAssets[0];
					}

					auto&& midi_file = midiSong->data.midiFile.data;
					auto&& midiAsset = std::get<HmxAssetFile>(midi_file.file.e->getData().data.catagoryValues[0].value);
					auto&& fileData = midiAsset.audio.audioFiles[0].fileData;

					std::ofstream outfile(*file, std::ios_base::binary);
					outfile.write((const char*)fileData.data(), fileData.size());
				}
			}

			static int curPickup = -1;
			static float pickupInput;
			ImGui::BeginChild("PickupTableHolder", ImVec2(windowSize.x/3, ImGui::GetContentRegionAvail().y-50));
			if (ImGui::BeginTable("PickupTable", 2, 0, ImVec2(windowSize.x / 3, oggWindowSize / 3))) {
				ImGui::TableSetupColumn("Index", 0, 0.2);
				ImGui::TableSetupColumn("Pickup Beat");
				ImGui::TableHeadersRow();
				if (celData.pickupArray->values.size() > 0) {
					for (int i = 0; i < celData.pickupArray->values.size(); i++)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						if (ImGui::Selectable((std::to_string(i)).c_str(), curPickup == i, ImGuiSelectableFlags_SpanAllColumns)) {
							curPickup = i;
							pickupInput = std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data;
						}
						ImGui::TableNextColumn();
						std::string pickupPos = std::to_string(std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data);
						pickupPos = pickupPos.substr(0, pickupPos.find(".") + 3);
						ImGui::Text(pickupPos.c_str());
					}
				}
				
				ImGui::EndTable();
			}
			
			if (ImGui::InputFloat("Pickup Beat", &pickupInput, 0.0F, 0.0F, "%.2f")) {
				pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F)*100)/100;
			}
			
			if (ImGui::Button("Add Pickup")) {
				pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F) * 100) / 100;

				if (celData.pickupArray->values.size() > 0) {
					std::vector<float> pickups;

					for (auto puv : celData.pickupArray->values) {
						pickups.emplace_back(std::get<PrimitiveProperty<float>>(puv->v).data);
					}

					auto it = std::find(pickups.begin(), pickups.end(), pickupInput);

					if (it == pickups.end()) {
						pickups.emplace_back(pickupInput);
						std::sort(pickups.begin(), pickups.end());
						auto last = std::unique(pickups.begin(), pickups.end());
						pickups.erase(last, pickups.end());
						celData.pickupArray->values.clear();
						for (int i = 0; i < pickups.size(); i++) {
							celData.pickupArray->values.emplace_back(new IPropertyValue);
							celData.pickupArray->values[i]->v = asset_helper::createPropertyValue("FloatProperty");
							std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data = pickups[i];
						}
						auto it2 = std::find(pickups.begin(), pickups.end(), pickupInput);
						if (it2 != pickups.end()) {
							curPickup = std::distance(pickups.begin(), it2);
						}
					}
				}
				else {
					celData.pickupArray->values.emplace_back(new IPropertyValue);
					celData.pickupArray->values[0]->v = asset_helper::createPropertyValue("FloatProperty");
					std::get<PrimitiveProperty<float>>(celData.pickupArray->values[0]->v).data = pickupInput;
					curPickup = 0;
				}
				

			}
			ImGui::SameLine();
			if (ImGui::Button("Update Pickup") && celData.pickupArray->values.size()>0) {
				pickupInput = std::round(std::clamp(pickupInput, 0.0F, 128.0F) * 100) / 100;
				if (celData.pickupArray->values.size() == 1) {
					std::get<PrimitiveProperty<float>>(celData.pickupArray->values[curPickup]->v).data = pickupInput;
				}
				else {
					std::vector<float> pickups;
					for (auto puv : celData.pickupArray->values) {
						pickups.emplace_back(std::get<PrimitiveProperty<float>>(puv->v).data);
					}

					auto it = std::find(pickups.begin(), pickups.end(), pickupInput);
					if (it == pickups.end()) {
						pickups[curPickup] = pickupInput;
						std::sort(pickups.begin(), pickups.end());
						auto last = std::unique(pickups.begin(), pickups.end());
						pickups.erase(last, pickups.end());
						celData.pickupArray->values.clear();
						for (int i = 0; i < pickups.size(); i++) {
							celData.pickupArray->values.emplace_back(new IPropertyValue);
							celData.pickupArray->values[i]->v = asset_helper::createPropertyValue("FloatProperty");
							std::get<PrimitiveProperty<float>>(celData.pickupArray->values[i]->v).data = pickups[i];
						}
						auto it2 = std::find(pickups.begin(), pickups.end(), pickupInput);
						if (it2 != pickups.end()) {
							curPickup = std::distance(pickups.begin(), it2);
						}
					}
				}
				
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove Pickup") && celData.pickupArray->values.size()>0) {
				int pickupToErase = curPickup;
				if (curPickup == celData.pickupArray->values.size() - 1) {
					curPickup--;
				}
				celData.pickupArray->values.erase(celData.pickupArray->values.begin() + pickupToErase);
				if (celData.pickupArray->values.size() > 0) {
					pickupInput = std::get<PrimitiveProperty<float>>(celData.pickupArray->values[curPickup]->v).data;
				}
				
			}
			ImGui::NewLine();
			ImGui::NewLine();
			if (ImGui::Button("Clear Pickups")) {
				ImGui::OpenPopup("Clear Pickups?");
			}

			if (ImGui::BeginPopupModal("Clear Pickups?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::BeginChild("Text", ImVec2(420, 69));
				ImGui::Text("Are you sure you would like to clear pickups?");
				ImGui::TextWrapped("WARNING: Will erase all pickups");
				ImGui::EndChild();
				ImGui::BeginChild("Buttons", ImVec2(420, 25));
				if (ImGui::Button("Yes", ImVec2(120, 0)))
				{
					celData.pickupArray->values.clear();
					curPickup = -1;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("No", ImVec2(120, 0)))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndChild();


				ImGui::EndPopup();
			}

			ImGui::EndChild();

			
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Riser Audio")) {
			ImGui::BeginChild("AudioSettingsRiser", ImVec2((windowSize.x / 3) * 2, oggWindowSize));
			display_cel_audio_options(celData, assetRiser, moggFilesRiser, fusionFileRiser, fusionPackageFileRiser, duplicate_moggsRiser, true);
			ImGui::EndChild();
			ImGui::SameLine();
			ImGui::BeginChild("Advanced - Riser", ImVec2(windowSize.x / 3, oggWindowSize));
			if (ImGui::Button("Export Riser Fusion File")) {
				auto file = SaveFile("Fusion Text File (.fusion)\0*.fusion\0", "fusion", "");
				if (file) {
					for (auto&& f : assetRiser.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {

							std::ofstream outFile(*file);
							std::string outStr = hmx_fusion_parser::outputData(std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes);
							outFile << outStr;

							break;
						}
					}
				}
			}

			if (ImGui::Button("Import Riser Fusion File")) {
				auto file = OpenFile("Fusion Text File (.fusion)\0*.fusion\0");
				if (file) {
					for (auto&& f : assetRiser.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {

							std::ifstream infile(*file, std::ios_base::binary);
							std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
							std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes = hmx_fusion_parser::parseData(fileData);

							break;
						}
					}
				}
			}

			ImGui::Spacing();

			bool overwrite_midiRiser = false;
			bool majRiser = true;

			if (ImGui::Button("Overwrite Riser Major Midi File")) {
				overwrite_midiRiser = true;
			}
			else if (ImGui::Button("Overwrite Riser Minor Midi File")) {
				overwrite_midiRiser = true;
				majRiser = false;
			}

			if (overwrite_midiRiser) {
				auto file = OpenFile("Harmonix Midi Resource File (.mid_pc)\0*.mid_pc\0");
				if (file) {
					AssetLink<MidiSongAsset>* midiSongRiser = nullptr;
					if (majRiser) {
						midiSongRiser = &celData.songTransitionFile.data.majorAssets[0];
					}
					else {
						midiSongRiser = &celData.songTransitionFile.data.minorAssets[0];
					}

					auto&& midi_fileRiser = midiSongRiser->data.midiFile.data;
					auto&& midiAssetRiser = std::get<HmxAssetFile>(midi_fileRiser.file.e->getData().data.catagoryValues[0].value);

					std::ifstream infile(*file, std::ios_base::binary);
					std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
					midiAssetRiser.audio.audioFiles[0].fileData = std::move(fileData);
				}
			}

			ImGui::Spacing();


			bool export_midiRiser = false;

			if (ImGui::Button("Export Riser Major Midi File")) {
				export_midiRiser = true;
				majRiser = true;
			}
			else if (ImGui::Button("Export Riser Minor Midi File")) {
				export_midiRiser = true;
				majRiser = false;
			}

			if (export_midiRiser) {
				auto file = SaveFile("Harmonix Midi Resource File (.mid_pc)\0*.mid_pc\0", "mid_pc", "");
				if (file) {
					AssetLink<MidiSongAsset>* midiSongRiser = nullptr;
					if (majRiser) {
						midiSongRiser = &celData.songTransitionFile.data.majorAssets[0];
					}
					else {
						midiSongRiser = &celData.songTransitionFile.data.minorAssets[0];
					}

					auto&& midi_fileRiser = midiSongRiser->data.midiFile.data;
					auto&& midiAssetRiser = std::get<HmxAssetFile>(midi_fileRiser.file.e->getData().data.catagoryValues[0].value);
					auto&& fileDataRiser = midiAssetRiser.audio.audioFiles[0].fileData;

					std::ofstream outfile(*file, std::ios_base::binary);
					outfile.write((const char*)fileDataRiser.data(), fileDataRiser.size());
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}





}
void set_g_pd3dDevice(ID3D11Device* g_pd3dDevice) {
	gCtx.g_pd3dDevice = g_pd3dDevice;

}
void custom_song_creator_update(size_t width, size_t height) {
	bool do_open = false;
	bool do_save = false;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New")) {
				load_template();
			}
			if (ImGui::MenuItem("Open", "Ctrl+O")) {
				do_open = true;
			}

			if (ImGui::MenuItem("Save", "Ctrl+S")) {
				do_save = true;
			}

			if (ImGui::MenuItem("Save As..")) {
				select_save_location();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Audio"))
		{
			std::vector<std::string> devices;

			int a, count = 0;
			BASS_DEVICEINFO info;

			for (a = 0; BASS_GetDeviceInfo(a, &info); a++)
			{
				if (info.flags & BASS_DEVICE_ENABLED) {
					devices.emplace_back(info.name);
				}
			}

			auto get_item = [](void* data, int idx, const char** out) -> bool {
				std::vector<std::string>& devices = *(std::vector<std::string>*)data;
				*out = devices[idx].c_str();

				return true;
			};


			if (ImGui::Combo("Current Device", &gAudio.currentDevice, get_item, &devices, devices.size())) {
				BASS_Stop();
				BASS_SetDevice(gAudio.currentDevice);
				BASS_Start();
			}

			if (ImGui::SliderFloat("Volume", &gAudio.volume, 0, 1)) {
				BASS_SetConfig(
					BASS_CONFIG_GVOL_SAMPLE,
					gAudio.volume * 10000
				);
			}

			ImGui::EndMenu();
		}

#if _DEBUG
		if (ImGui::BeginMenu("Debug Menu"))
		{
			bool extract_uexp = false;
			std::string save_file;
			std::string ext;
			std::function<std::vector<u8>(const Asset&)> getData;

			if (ImGui::MenuItem("Load Pak and pop in album art")) {
				__debugbreak();
			}

			if (ImGui::MenuItem("Extract Midi From uexp")) {
				extract_uexp = true;
				save_file = "Fuser Midi File (*.midi_pc)\0.midi_pc\0";
				ext = "midi_pc";
				getData = [](const Asset& asset) {
					auto&& midiAsset = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);
					auto&& fileData = midiAsset.audio.audioFiles[0].fileData;
					return fileData;
				};
			}

			if (ImGui::MenuItem("Extract Fusion From uexp")) {
				extract_uexp = true;
				save_file = "Fuser Fusion File (*.fusion)\0.fusion\0";
				ext = "fusion";
				getData = [](const Asset& asset) {
					auto&& assetFile = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);

					for (auto&& f : assetFile.audio.audioFiles) {
						if (f.fileType == "FusionPatchResource") {
							std::string outStr = hmx_fusion_parser::outputData(std::get<HmxAudio::PackageFile::FusionFileResource>(f.resourceHeader).nodes);
							std::vector<u8> out;
							out.resize(outStr.size());
							memcpy(out.data(), outStr.data(), outStr.size());
							return out;
						}
					}

					return std::vector<u8>();
				};
			}

			if (extract_uexp) {
				auto file = OpenFile("Unreal Asset File (*.uasset)\0*.uasset\0");
				if (file) {
					auto assetFile = fs::path(*file);
					auto uexpFile = assetFile.parent_path() / (assetFile.stem().string() + ".uexp");

					std::ifstream infile(assetFile, std::ios_base::binary);
					std::ifstream uexpfile(uexpFile, std::ios_base::binary);

					std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
					fileData.insert(fileData.end(), std::istreambuf_iterator<char>(uexpfile), std::istreambuf_iterator<char>());


					DataBuffer dataBuf;
					dataBuf.loading = true;
					dataBuf.setupVector(fileData);

					Asset a;
					a.serialize(dataBuf);

					auto out_file = SaveFile(save_file.c_str(), ext.c_str(), "");
					if (out_file) {
						auto fileData = getData(a);
						std::ofstream outfile(*out_file, std::ios_base::binary);
						outfile.write((const char*)fileData.data(), fileData.size());
					}
				}
			}

			ImGui::EndMenu();
		}
#endif

		ImGui::EndMainMenuBar();
	}

	auto&& input = ImGui::GetIO();

	if (input.KeyCtrl) {
		if (input.KeysDown['O'] && input.KeysDownDuration['O'] == 0.0f) {
			do_open = true;
		}
		if (input.KeysDown['S'] && input.KeysDownDuration['S'] == 0.0f) {
			do_save = true;
		}
	}


	if (do_open) {
		auto file = OpenFile("Fuser Custom Song (*.pak)\0*.pak\0");
		if (file) {
			std::ifstream infile(*file, std::ios_base::binary);
			std::vector<u8> fileData = std::vector<u8>(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());

			DataBuffer dataBuf;
			dataBuf.setupVector(fileData);
			load_file(std::move(dataBuf));

			gCtx.saveLocation = *file;
		}
	}

	if (do_save) {
		if (!gCtx.saveLocation.empty()) {
			save_file();
		}
		else {
			select_save_location();
		}
	}

	ImGui::SetNextWindowPos(ImVec2{ 0, ImGui::GetFrameHeight() });
	ImGui::SetNextWindowSize(ImVec2{ (float)width, (float)height - ImGui::GetFrameHeight() });

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoTitleBar;
	window_flags |= ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoResize;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Fuser Custom Song Creator", nullptr, window_flags);

	if (gCtx.currentPak != nullptr) {
		if (ImGui::BeginTabBar("Tabs")) {
			if (ImGui::BeginTabItem("Main Properties")) {
				display_main_properties();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Album Art")) {
				display_album_art();
				ImGui::EndTabItem();
			}

			for (auto&& cel : gCtx.currentPak->root.celData) {
				std::string tabName = "Song Cell - ";
				tabName += cel.data.type.getString();
				if (ImGui::BeginTabItem(tabName.c_str())) {
					display_cell_data(cel.data, gCtx.currentPak->root.keyMode);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		if (Error_InvalidFileName) {
			ImGui::OpenPopup("Invalid File Name");
			Error_InvalidFileName = false;
		}
		auto fileName = gCtx.currentPak->root.shortName + "_P.pak";
		auto error = "Your file must be named as " + fileName + ", otherwise the song loader won't unlock it!";
		ErrorModal("Invalid File Name", error.c_str());
	}
	else {
		ImGui::Text("Welcome to the Fuser Custom Song Creator!");
		ImGui::Text("To get started with the default template, choose File -> New from the menu, or use the button:"); ImGui::SameLine();
		if (ImGui::Button("Create New Custom Song")) {
			load_template();
		}

		ImGui::Text("To open an existing custom song, use File -> Open.");
	}

	ImGui::End();
	ImGui::PopStyleVar();
}