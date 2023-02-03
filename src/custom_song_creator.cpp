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

#include <filesystem>
namespace fs = std::filesystem;

#include "custom_song_pak_template.h"
#include "ImageFile.h"
#include "DDSFile.h"
#include "moggcrypt/CCallbacks.h"
#include "moggcrypt/VorbisEncrypter.h"

#include "fuser_asset.h"

#include "bass/bass.h"

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

void playOgg(const std::vector<uint8_t> &ogg) {
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

void display_playable_audio(PlayableAudio &audio,bool isRiser) {
	if (audio.oggData.empty()) {
		ImGui::Text("No ogg file loaded.");
		return;
	}

	auto active = BASS_ChannelIsActive(audio.channelHandle);
	if (active != BASS_ACTIVE_PLAYING) {
		std::string buttonText = "Play Disc Audio";
		if (isRiser) {
			buttonText = "Play Riser Audio";
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

		for(auto it = errors.begin(); it != errors.end();) {
			auto &&e = *it;
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

static void ErrorModal(const char *name, const char *msg) {
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
static void ChooseFuserEnum(const char *label, std::string &out) {
	auto values = T::GetValues();

	auto getter = [](void *data, int idx, const char **out_str) {
		auto &&d = reinterpret_cast<std::vector<std::string>*>(data);
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
static void ChooseFuserEnum(const char *label, typename T::Value &out) {
	auto values = T::GetValues();

	auto getter = [](void *data, int idx, const char **out_str) {
		auto &&d = reinterpret_cast<std::vector<std::string>*>(data);
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

static std::optional<std::string> SaveFile(LPCSTR filter, LPCSTR ext, const std::string &fileName) {
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

void load_file(DataBuffer &&dataBuf) {
	gCtx.currentPak = std::make_unique<MainContext::CurrentPak>();
	gCtx.saveLocation.clear();

	auto &&pak = gCtx.currentPak->pak;

	dataBuf.serialize(pak);
	int i = 0;
	for (auto &&e : pak.entries) {
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

							uint8_t * uncompressedData_ = dds.VGetUncompressedImageData();
							for (int y = 0; y < height; y++) {
								for (int x = 0; x < width; x++) {
									uncompressedImageData[(x + width * y) * 4 + 0] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 0];
									uncompressedImageData[(x + width * y) * 4 + 1] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 1];
									uncompressedImageData[(x + width * y) * 4 + 2] = uncompressedData_[(x + width * (height - 1 - y)) * 3 + 2];
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

	SongSerializationCtx ctx;
	ctx.loading = true;
	ctx.pak = &pak;
	gCtx.currentPak->root.serialize(ctx);
}

void load_template() {
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
	auto &&root = gCtx.currentPak->root;

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

	auto texture = &std::get<Texture2D>(icon.data.file.e->getData().data.catagoryValues[0].value);

	for (int mip_index = 0; mip_index < texture->mips.size(); mip_index++) {
		texture->mips[mip_index].width = (texture->mips[mip_index].width + 3) & ~3;
		texture->mips[mip_index].height = (texture->mips[mip_index].height + 3) & ~3;



		auto raw_data = gCtx.art.resizeAndGetData(texture->mips[mip_index].width, texture->mips[mip_index].height);

		uint8_t* uncompressedImageData = new uint8_t[texture->mips[mip_index].width * texture->mips[mip_index].height * 3];
		auto width = texture->mips[mip_index].width;
		auto height = texture->mips[mip_index].height;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uncompressedImageData[(x + width * y) * 3 + 0] = raw_data[(x + width * (height - 1 - y)) * 4 + 0];
				uncompressedImageData[(x + width * y) * 3 + 1] = raw_data[(x + width * (height - 1 - y)) * 4 + 1];
				uncompressedImageData[(x + width * y) * 3 + 2] = raw_data[(x + width * (height - 1 - y)) * 4 + 2];

			}
		}

		DDSFile ddsFile = DDSFile();
		ddsFile.VConversionInitialize(uncompressedImageData, texture->mips[mip_index].width * texture->mips[mip_index].height * 3, texture->mips[mip_index].width, texture->mips[mip_index].height);
		texture->mips[mip_index].mipData.clear();
		auto len = ddsFile.GetLinearSize();
		for (int i = 0; i < len; i++) {
			texture->mips[mip_index].mipData.push_back(ddsFile.m_mainData[i]);
		}
		texture->mips[mip_index].len_1 = len;
		texture->mips[mip_index].len_2 = len;
	}
}

void display_album_art() {
	auto&& root = gCtx.currentPak->root;

	ImGui::Text("Album art resizes to 540x540px, choose source image appropriately for good quality");
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
void display_mogg_settings(FusionFileAsset &fusionFile, size_t idx, HmxAudio::PackageFile &mogg,bool isRiser) {
	auto &&header = std::get<HmxAudio::PackageFile::MoggSampleResourceHeader>(mogg.resourceHeader);
	std::string buttonText = "Replace Disc Audio";
	if(isRiser){
		buttonText = "Replace Riser Audio";
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

	

	display_playable_audio(fusionFile.playableMoggs[idx],isRiser);

	ImGui::InputScalar("Sample Rate", ImGuiDataType_U32, &header.sample_rate);

	ErrorModal("Ogg loading error", ("Failed to load ogg file:" + lastMoggError).c_str());
}

void display_cell_data(CelData& celData, FuserEnums::KeyMode::Value currentKeyMode, bool advancedMode = false) {
	ChooseFuserEnum<FuserEnums::Instrument>("Instrument", celData.instrument);

	// Disc Moggs

	auto&& fusionFile = celData.majorAssets[0].data.fusionFile.data;
	auto&& asset = std::get<HmxAssetFile>(fusionFile.file.e->getData().data.catagoryValues[0].value);
	//auto &&mogg = asset.audio.audioFiles[0];

	HmxAudio::PackageFile* fusionPackageFile = nullptr;
	std::vector<HmxAudio::PackageFile*> moggFiles;
	std::unordered_set<std::string> fusion_mogg_files;

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

		for (auto c : map.children) {
			auto nodes = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_files.emplace(nodes->getString("sample_path"));
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

		for (auto c : mapRiser.children) {
			auto nodesRiser = std::get<hmx_fusion_nodes*>(c.value);
			fusion_mogg_filesRiser.emplace(nodesRiser->getString("sample_path"));
		}
	}

	bool duplicate_moggsRiser = fusion_mogg_filesRiser.size() == 1;
	ImGui::NewLine();

	std::string primaryKey = "";
	std::string secondaryKey = "";

	if (currentKeyMode == FuserEnums::KeyMode::Value::Minor) {
		std::swap(primaryKey, secondaryKey);
	}

	std::string duplicateString = "Same Disc audio for both key modes?";
	std::string duplicateStringRiser = "Same Riser audio for both key modes?";
	auto windowSize = ImGui::GetWindowSize();

	auto oggWindowSize = ImGui::GetContentRegionAvail().y / 2;
	ImGui::BeginChild("Primary", ImVec2(windowSize.x / 2, oggWindowSize));
	ImGui::Text("Major Key");
	bool duplicate_changed = false;

	if (ImGui::CollapsingHeader("Disc Audio")) {
		display_mogg_settings(fusionFile, 0, *moggFiles[0], false);

		duplicate_changed = ImGui::Checkbox(duplicateString.c_str(), &duplicate_moggs);
	}

	bool duplicate_changedRiser = false;

	if (ImGui::CollapsingHeader("Riser Audio")) {
		display_mogg_settings(fusionFileRiser, 0, *moggFilesRiser[0], true);

		duplicate_changedRiser = ImGui::Checkbox(duplicateStringRiser.c_str(), &duplicate_moggsRiser);
	}

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
				asset.audio.audioFiles.insert(asset.audio.audioFiles.begin() + 1, *moggFiles[0]);

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
				std::get<hmx_fusion_nodes*>(map.children[1].value)->getString("sample_path") = "second_key";
			}
		}
	}

	if (duplicate_changedRiser) {
		if (duplicate_moggsRiser) {
			if (moggFilesRiser.size() == 2) {
				assetRiser.audio.audioFiles.erase(assetRiser.audio.audioFiles.begin() + 1);

				moggFilesRiser.clear();
				for (auto&& file : assetRiser.audio.audioFiles) {
					if (file.fileType == "FusionPatchResource") {
						fusionPackageFileRiser = &file;
					}
					else if (file.fileType == "MoggSampleResource") {
						moggFilesRiser.emplace_back(&file);
					}
				}
			}

			auto&& fusionRiser = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFileRiser->resourceHeader);
			auto mapRiser = fusionRiser.nodes.getNode("keymap");

			if (mapRiser.children.size() == 2) {
				std::get<hmx_fusion_nodes*>(mapRiser.children[1].value)->getString("sample_path") = std::get<hmx_fusion_nodes*>(mapRiser.children[0].value)->getString("sample_path");
			}
		}
		else {
			if (moggFilesRiser.size() == 1) {
				assetRiser.audio.audioFiles.insert(assetRiser.audio.audioFiles.begin() + 1, *moggFilesRiser[0]);

				moggFilesRiser.clear();
				for (auto&& file : assetRiser.audio.audioFiles) {
					if (file.fileType == "FusionPatchResource") {
						fusionPackageFileRiser = &file;
					}
					else if (file.fileType == "MoggSampleResource") {
						moggFilesRiser.emplace_back(&file);
					}
				}
			}

			auto&& fusionRiser = std::get<HmxAudio::PackageFile::FusionFileResource>(fusionPackageFileRiser->resourceHeader);
			auto mapRiser = fusionRiser.nodes.getNode("keymap");

			if (mapRiser.children.size() == 2) {
				std::get<hmx_fusion_nodes*>(mapRiser.children[1].value)->getString("sample_path") = "second_key";
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("Secondary", ImVec2(windowSize.x / 2, oggWindowSize));
	ImGui::Text("Minor Key");
	if (ImGui::CollapsingHeader("Disc Audio")) {
		if (duplicate_moggs) {
			ImGui::Text("Disc Audio is duplicated for both key modes.");
		}
		else {
			display_mogg_settings(fusionFile, 1, *moggFiles[1], false);
		}
	}
	if (ImGui::CollapsingHeader("Riser Audio")) {
		if (duplicate_moggsRiser) {
			ImGui::Text("Riser Audio is duplicated for both key modes.");
		}
		else {
			display_mogg_settings(fusionFileRiser, 1, *moggFilesRiser[1], true);
		}
	}
	ImGui::EndChild();


	ImGui::BeginChild("Advanced - Disc", ImVec2(windowSize.x / 2, oggWindowSize));
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
	ImGui::EndChild();


	ImGui::SameLine();
	ImGui::BeginChild("Advanced - Riser", ImVec2(windowSize.x / 2, oggWindowSize));
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
				std::vector<std::string> &devices = *(std::vector<std::string>*)data;
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
				getData = [](const Asset &asset) {
					auto &&midiAsset = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);
					auto &&fileData = midiAsset.audio.audioFiles[0].fileData;
					return fileData;
				};
			}

			if (ImGui::MenuItem("Extract Fusion From uexp")) {
				extract_uexp = true;
				save_file = "Fuser Fusion File (*.fusion)\0.fusion\0";
				ext = "fusion";
				getData = [](const Asset &asset) {
					auto &&assetFile = std::get<HmxAssetFile>(asset.data.catagoryValues[0].value);
					
					for (auto &&f : assetFile.audio.audioFiles) {
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
						outfile.write((const char *)fileData.data(), fileData.size());
					}
				}
			}

			ImGui::EndMenu();
		}
#endif

		ImGui::EndMainMenuBar();
	}

	auto &&input = ImGui::GetIO();

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

			for (auto &&cel : gCtx.currentPak->root.celData) {
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