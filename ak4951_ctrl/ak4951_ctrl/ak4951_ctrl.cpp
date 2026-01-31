#include <iostream>
#include <string>
#include <fstream>

#include <WinSock2.h>
#include <ws2tcpip.h>

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#pragma comment(lib, "ws2_32.lib")
#define PORT 1025

#define IPSIZE          16
char    IPadr[IPSIZE] = "192.168.0.10";
#define DATA_LENGTH     60

#define ALC_MODE_OFF    0
#define ALC_MODE_RX     1
#define ALC_MODE_TX     2
#define REF_MIN         0   // dB
#define REF_MAX         30  // dB

unsigned char rxdata[DATA_LENGTH];
unsigned char txdata[DATA_LENGTH] = {
        0xef, 0xfe, 0x05, 0x7f, 0x78, 0x06, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

unsigned char reqdata[DATA_LENGTH] = {
        0xef, 0xfe, 0x05, 0x7f, 0x78, 0x07, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

struct sockaddr_in addr;
SOCKET sock;

static int alc_mode = ALC_MODE_OFF;
static int alc_ref_level = 0;
static int mic_gain = 18;
static int sp_enable = 0;

void write_ak4951(unsigned char reg_addr, unsigned char reg_data);
void read_ak4951(unsigned char reg_addr, unsigned char* reg_data);
void write_ak4951_reg02(int sp_enable, int gain);
void set_dvr_dvl(int level);
void set_ref(int level);
void alc_on_rx(int level);
void alc_on_tx(int level);
void alc_off(void);

bool save_setting_items(void);
bool load_setting_items(void);

int APIENTRY WinMain
(_In_        HINSTANCE   hInstance
    , _In_opt_    HINSTANCE   hPrevInstance
    , _In_        LPSTR       lpCmdLin
    , _In_        int         nCmdShow
)

//int main()
{
    load_setting_items();

    bool connect = false;
    bool loopback = false;
    bool loopback_prev = false;
    bool link_att = false;
    bool link_att_prev = false;
    int alc_ref_level_prev = 0;
    int mic_gain_prev = 18;
    int sp_enable_prev = 0x80;  // 0x80 is for initialize

    WSAData wsaData;
    int error = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (error != 0)
    {
        printf("Error: WSAStartup\n");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (!glfwInit()) {
        return -1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(320, 256, "AK4951 Controller (JI1UDD)", NULL, NULL);

    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    gl3wInit();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window)) {

        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Hermes Lite 2+");
        ImGui::SetNextItemWidth(190.0f);
            ImGui::InputText("##IP", IPadr, IM_ARRAYSIZE(IPadr)); 

            if (!connect) {
                ImGui::SameLine();
                if (ImGui::Button(" SET ")) {
                    connect = true;

                    inet_pton(addr.sin_family, IPadr, &addr.sin_addr.s_addr);

                    sock = socket(addr.sin_family, SOCK_DGRAM, 0);
                    if (sock == INVALID_SOCKET) {
                        printf("Error: socket\n");
                        return -1;
                    }

                    bind(sock, (SOCKADDR*)&addr, sizeof(addr));

                    write_ak4951_reg02(sp_enable, alc_mode ? mic_gain : 18);
                }
            }

            ImGui::Text("SPEAKER"); ImGui::SameLine();
            ImGui::RadioButton("OFF", &sp_enable, 0); ImGui::SameLine();
            ImGui::RadioButton("ON", &sp_enable, 0x80);
            if (connect) {
                if (loopback) {
                    sp_enable = sp_enable_prev = 0;
                }
                else if (sp_enable != sp_enable_prev) {
                    sp_enable_prev = sp_enable;
                    write_ak4951_reg02(sp_enable, alc_mode ? mic_gain : 18);
                }

            }
        ImGui::End();

        if (connect) {
            ImGui::Begin("Digital ALC");
            if (ImGui::RadioButton("OFF", &alc_mode, ALC_MODE_OFF)) {
                alc_off();
                loopback = link_att = false;
            }
            ImGui::SameLine();

            if (ImGui::RadioButton("RX", &alc_mode, ALC_MODE_RX)) {
                alc_on_rx(alc_ref_level);
                alc_ref_level_prev = alc_ref_level;
                loopback = link_att = false;
            }
            ImGui::SameLine();

            if (ImGui::RadioButton("TX", &alc_mode, ALC_MODE_TX)) {
                alc_on_tx(alc_ref_level);
                alc_ref_level_prev = alc_ref_level;
                loopback = link_att = false;
            }

            if (alc_mode != ALC_MODE_OFF) {
                ImGui::SetNextItemWidth(190.0f);
                ImGui::SliderInt("REF LEVEL (dB)", &alc_ref_level, REF_MIN, REF_MAX);
                alc_ref_level = (alc_ref_level / 3) * 3;
                if (alc_ref_level != alc_ref_level_prev) {
                    alc_ref_level_prev = alc_ref_level;
                    if ((alc_mode == ALC_MODE_RX) || link_att) {
                        set_dvr_dvl(alc_ref_level);
                        set_ref(alc_ref_level);
                    }
                    else {
                        set_ref(alc_ref_level);
                    }
                }
            }
            ImGui::End();
        }

        if (connect && (alc_mode != ALC_MODE_OFF)) {
            ImGui::Begin("Mic AMP analog gain");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderInt("GAIN (dB)", &mic_gain, 0, 30);
            mic_gain = (mic_gain / 3) * 3;
            if (ImGui::Button(" default ")) {
                mic_gain = 18;
            }
            ImGui::SameLine();

            if (alc_mode == ALC_MODE_TX) {
                ImGui::Checkbox("Loopback", &loopback);
            }
            ImGui::SameLine();

            if (loopback) {
                ImGui::Checkbox("Link-DAC-ATT", &link_att);
            }

            ImGui::End();

            if (mic_gain != mic_gain_prev) {
                mic_gain_prev = mic_gain;
                write_ak4951_reg02(sp_enable, mic_gain);
            }

            if (loopback != loopback_prev) {
                loopback_prev = loopback;
                link_att = link_att_prev = false;
                if (loopback) {
                    sp_enable = sp_enable_prev = 0x00;  // SPK OFF
                    write_ak4951_reg02(sp_enable, mic_gain);
                    write_ak4951(0x1d, 0x07); // PFDAC = PFVOL, ADCPF = ADC, PFSDO = DIGFIL
                }
                else {
                    write_ak4951(0x1d, 0x03); // PFDAC = STDI, ADCPF = ADC, PFSDO = DIGFIL
                    set_dvr_dvl(0); // DVR/DVL 0dB
                }
            }

            if (link_att != link_att_prev) {
                link_att_prev = link_att;
                if (link_att) {
                    set_dvr_dvl(alc_ref_level); // link REF
                }
                else {
                    set_dvr_dvl(0); // DVR/DVL 0dB
                }
            }

        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glClearColor(0.8f, 0.8f, 0.8f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, display_w, display_h);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    save_setting_items();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    closesocket(sock);
    WSACleanup();

    return 0;
}


void write_ak4951(unsigned char reg_addr, unsigned char reg_data) {
    memcpy(txdata + 7, &reg_addr, 1);
    memcpy(txdata + 8, &reg_data, 1);

    sendto(sock, (const char*)txdata, DATA_LENGTH, 0, (struct sockaddr*)&addr, sizeof(addr));
    printf("send data: %x, %x\n", reg_addr, reg_data);
    Sleep(10);
}


void read_ak4951(unsigned char reg_addr, unsigned char* reg_data) {
    memcpy(reqdata + 7, &reg_addr, 1);
    sendto(sock, (const char*)reqdata, DATA_LENGTH, 0, (struct sockaddr*)&addr, sizeof(addr));
    Sleep(100);

    memset(rxdata, 0, DATA_LENGTH);
    recv(sock, (char*)rxdata, DATA_LENGTH, 0);
    *reg_data = rxdata[26];
}


void write_ak4951_reg02(int sp_enable, int gain) {
    if (gain > 30) gain = 30;
    unsigned char g = gain / 3;
    unsigned char d = 0x28 | (unsigned char)sp_enable | (0x40 & (g << 3)) | (0x07 & g);
    write_ak4951(0x02, (unsigned char)d);  // Signal Select1
}


void set_dvr_dvl(int level) {
    unsigned char dvr = (unsigned int)((float)level / 0.5f) + 24;
    write_ak4951(0x13, dvr); // DVR -dB
    write_ak4951(0x14, dvr); // DVL -dB
}


void set_ref(int level) {
    unsigned char ref = (unsigned int)((float)level / 0.375f) + 145;
    write_ak4951(0x0c, ref); // REF +dB
}


void alc_on_rx(int level) {
    unsigned char dvr = (unsigned int)((float)level / 0.5f) + 24;
    unsigned char ref = (unsigned int)((float)level / 0.375f) + 145;
    write_ak4951(0x07, 0x34); // SMUTE=1
    write_ak4951(0x13, dvr);  // DVR -dB
    write_ak4951(0x14, dvr);  // DVL -dB
    write_ak4951(0x1d, 0x04); // PFDAC=PFVOL, ADCPF=STDI, PFSDO=ADC
    write_ak4951(0x0a, 0x6c); // WTM=21.3ms
    write_ak4951(0x0b, 0x0e); // RGAIN=0.00106dB(2/fs), LMTH=-4.1dBFS
    write_ak4951(0x0c, ref);  // REF +dB
    write_ak4951(0x0b, 0x2e); // ALC=Enable
    write_ak4951(0x07, 0x14); // SMUTE=0
    write_ak4951_reg02(sp_enable, mic_gain);
}


void alc_on_tx(int level) {
    unsigned char ref = (unsigned int)((float)level / 0.375f) + 145;
    write_ak4951(0x07, 0x34); // SMUTE=1
    write_ak4951(0x13, 0x18); // DVR 0dB
    write_ak4951(0x14, 0x18); // DVL 0dB
    write_ak4951(0x1d, 0x03); // PFDAC = STDI, ADCPF = ADC, PFSDO = DIGFIL
    write_ak4951(0x0a, 0x6c); // WTM=21.3ms
    write_ak4951(0x0b, 0x0e); // RGAIN=0.00106dB(2/fs), LMTH=-4.1dBFS
    write_ak4951(0x0c, ref);  // REF +dB
    write_ak4951(0x0b, 0x2e); // ALC=Enable
    write_ak4951(0x07, 0x14); // SMUTE=0
    write_ak4951_reg02(sp_enable, mic_gain);
}


void alc_off(void) {
    write_ak4951(0x0b, 0x0e); // ALC = Disable
    write_ak4951(0x1d, 0x03); // PFDAC = STDI, ADCPF = ADC, PFSDO = DIGFIL
    write_ak4951(0x13, 0x18); // DVR 0dB
    write_ak4951(0x14, 0x18); // DVL 0dB
    write_ak4951_reg02(sp_enable, 18);
}


bool save_setting_items(void) {
    FILE* fp;
    errno_t errorCode;

    errorCode = fopen_s(&fp, "setting.dat", "w");
    if (errorCode != 0) {
        printf("Error! setting.dat can not be opened!!");
        return false;
    }

    fprintf_s(fp, "%s\n", IPadr);
    fprintf_s(fp, "%d\n", alc_mode);
    fprintf_s(fp, "%d\n", alc_ref_level);
    fprintf_s(fp, "%d\n", mic_gain);
    fprintf_s(fp, "%d\n", sp_enable);

    fclose(fp);
    printf("Save setting is completed");
    return true;
}


bool load_setting_items(void) {
    FILE* fp;
    errno_t errorCode;

    errorCode = fopen_s(&fp, "setting.dat", "r");
    if (errorCode != 0) {
        printf("Error! setting.dat can not be opened!!");
        return false;
    }

    fscanf_s(fp, "%s", IPadr, IPSIZE);
    fscanf_s(fp, "%d", &alc_mode);
    fscanf_s(fp, "%d", &alc_ref_level);
    fscanf_s(fp, "%d", &mic_gain);
    fscanf_s(fp, "%d", &sp_enable);

    fclose(fp);
    printf("Load setting is completed");
    return true;
}