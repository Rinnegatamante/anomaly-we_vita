#include <vitasdk.h>
#include <vitaGL.h>
#include <imgui_vita.h>
#include <stdio.h>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern "C" {
void *__wrap_memcpy(void *dest, const void *src, size_t n) {
	return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
	return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
	return sceClibMemset(s, c, n);
}
}

#define CONFIG_FILE_PATH "ux0:data/anomaly/settings.cfg"

int antialiasing = 2; // Anti-Aliasing setting
bool framecap = false; // Framecap setting

void loadConfig(void) {
	char buffer[30];
	int value;

	FILE *config = fopen(CONFIG_FILE_PATH, "r");

	if (config) {
		while (EOF != fscanf(config, "%[^ ] %d\n", buffer, &value)) {
			if (strcmp("antialiasing", buffer) == 0) antialiasing = value;
			else if (strcmp("framecap", buffer) == 0) framecap = (bool)value;
		}
		fclose(config);
	}
}

void saveConfig(void) {
	FILE *config = fopen(CONFIG_FILE_PATH, "w+");

	if (config) {
		fprintf(config, "%s %d\n", "antialiasing", antialiasing);
		fprintf(config, "%s %d\n", "framecap", (int)framecap);
		fclose(config);
	}
}

char *antialiasing_settings[] = {
	"Disabled",
	"MSAA 2x",
	"MSAA 4x"
};

GLuint antialiasing_presets[3];

char *options_descs[] = {
	"Technique used to reduce aliasing surrounding 3D models. Greatly improves graphics quality at the cost of some GPU power.\nThe default value is: MSAA 4x.", // antialiasing
	"Enable a framecap to 30 fps.\nThe default value is: Off.", // framecap
};

enum {
	OPT_ANTIALIASING,
	OPT_FRAMECAP,
};

char *desc = nullptr;

void SetDescription(int i) {
	if (ImGui::IsItemHovered())
		desc = options_descs[i];
}

void uploadTexture(GLuint tex, uint8_t *buf, int w, int h) {
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

int main(int argc, char *argv[]) {
	loadConfig();
	int exit_code = 0xDEAD;

	vglInitExtended(0, 960, 544, 0x1800000, SCE_GXM_MULTISAMPLE_4X);
	ImGui::CreateContext();
	ImGui_ImplVitaGL_Init();
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	ImGui::StyleColorsDark();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	
	glGenTextures(3, antialiasing_presets);
	int w, h;
	uint8_t *buf = stbi_load("app0:antialiasing/0x.png", &w, &h, NULL, 4);
	uploadTexture(antialiasing_presets[0], buf, w, h);
	free(buf);
	buf = stbi_load("app0:antialiasing/2x.png", &w, &h, NULL, 4);
	uploadTexture(antialiasing_presets[1], buf, w, h);
	free(buf);
	buf = stbi_load("app0:antialiasing/4x.png", &w, &h, NULL, 4);
	uploadTexture(antialiasing_presets[2], buf, w, h);
	free(buf);
	
	ImGui::GetIO().MouseDrawCursor = false;
	
	while (exit_code == 0xDEAD) {
		bool show_anti = false;
		int anti = antialiasing;
		desc = nullptr;
		ImGui_ImplVitaGL_NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(960, 544), ImGuiSetCond_Always);
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		float y = ImGui::GetCursorPosY();
		ImGui::SetCursorPosX(892.0f);
		ImGui::TextColored(ImVec4(255, 255, 0, 255), "Graphics");
		ImGui::SetCursorPosY(y);
		
		ImGui::Text("Anti-Aliasing:"); ImGui::SameLine();
		if (ImGui::BeginCombo("##combo2", antialiasing_settings[antialiasing])) {
			for (int n = 0; n < sizeof(antialiasing_settings)/sizeof(*antialiasing_settings); n++) {
				bool is_selected = antialiasing == n;
				if (ImGui::Selectable(antialiasing_settings[n], is_selected))
					antialiasing = n;
				if (ImGui::IsItemHovered()) {
					show_anti = true;
					anti = n;
					desc = options_descs[OPT_ANTIALIASING];
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered()) {
			show_anti = true;
			desc = options_descs[OPT_ANTIALIASING];
		}
		
		ImGui::Separator();
		y = ImGui::GetCursorPosY();
		ImGui::SetCursorPosX(920.0f);
		ImGui::TextColored(ImVec4(255, 255, 0, 255), "Misc");
		ImGui::SetCursorPosY(y);
		
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::Text("Framecap:"); ImGui::SameLine();
		ImGui::Checkbox("##check1", &framecap);
		SetDescription(OPT_FRAMECAP);
		ImGui::PopStyleVar();
		
		ImGui::Separator();
		if (ImGui::Button("Save and Exit"))
			exit_code = 0;
		ImGui::SameLine();
		if (ImGui::Button("Save and Launch the game"))
			exit_code = 1;
		ImGui::SameLine();
		if (ImGui::Button("Discard and Exit"))
			exit_code = 2;
		ImGui::SameLine();
		if (ImGui::Button("Discard and Launch the game"))
			exit_code = 3;
		ImGui::Separator();

		if (desc) {
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::TextWrapped(desc);
		}
		if (show_anti) {
			float w = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX((w - 545.0f) / 2.0f);
			ImGui::Image((void*)antialiasing_presets[anti], ImVec2(545.0f, 324.0f));
		}

		ImGui::End();
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
	}

	if (exit_code < 2) // Save
		saveConfig();

	if (exit_code % 2 == 1) // Launch
		sceAppMgrLoadExec("app0:/eboot.bin", NULL, NULL);

	return 0;
}
