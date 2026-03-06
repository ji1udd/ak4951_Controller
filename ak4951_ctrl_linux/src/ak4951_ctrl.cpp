#include <iostream>
#include <string>
#include <fstream>
#include <math.h>

#define __STDC_WANT_LIB_EXT1__ 1
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define PORT 1025
#define IPSIZE          16
char    IPadr[IPSIZE] = "192.168.0.10";
#define DATA_LENGTH     60

#define ALC_MODE_OFF    0
#define ALC_MODE_RX     1
#define ALC_MODE_TX     2
#define REF_MIN         0   // dB
#define REF_MAX         30  // dB

#define EQ_NUM          5

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
int sock;

typedef struct {
    bool enable, enable_prev;
    int fo, fo_prev;
    int fb, fb_prev;
    float k, k_prev;
    const unsigned char EA_addr;
    const unsigned char EB_addr;
    const unsigned char EC_addr;
} eq_item;

static eq_item eq[EQ_NUM] = {
    { false, false,  200,  200,  100,  100, 0.0f, 0.0f, 0x32, 0x34, 0x36},
    { false, false,  400,  400,  150,  150, 0.0f, 0.0f, 0x38, 0x3a, 0x3c},
    { false, false,  800,  800,  300,  300, 0.0f, 0.0f, 0x3e, 0x40, 0x42},
    { false, false, 1600, 1600,  600,  600, 0.0f, 0.0f, 0x44, 0x46, 0x48},
    { false, false, 3200, 3200, 1200, 1200, 0.0f, 0.0f, 0x4a, 0x4c, 0x4e}
};

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
void set_eq(unsigned int ch);

bool save_setting_items(void);
bool load_setting_items(void);

int main()
{
    load_setting_items();

    bool connect = false;
    bool loopback = false;
    bool loopback_prev = false;
    bool link_att = false;
    bool link_att_prev = false;
    int alc_mode_prev = 0;
    int alc_ref_level_prev = 0;
    int mic_gain_prev = 0;
    int sp_enable_prev = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (!glfwInit()) {
        return -1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(320, 424, "AK4951 Controller (JI1UDD)", NULL, NULL);

    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    std::string iniPath = std::string(std::getenv("HOME")) + "/.config/ak4951_ctrl/imgui.ini";
//   static std::string persistentPath = iniPath;
//   io.IniFilename = persistentPath.c_str();
    io.IniFilename = iniPath.c_str();

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

                    addr.sin_addr.s_addr = inet_addr(IPadr);
                    sock = socket(addr.sin_family, SOCK_DGRAM, 0);
                    if (sock < 0) {
                        printf("Error: socket #%d, %d\n", sock, errno);
                        return -1;
                    }

                    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

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
            ImGui::RadioButton("OFF", &alc_mode, ALC_MODE_OFF); ImGui::SameLine();
            ImGui::RadioButton("RX", &alc_mode, ALC_MODE_RX); ImGui::SameLine();
            ImGui::RadioButton("TX", &alc_mode, ALC_MODE_TX);

            if (alc_mode != alc_mode_prev) {
                alc_mode_prev = alc_mode;
                switch (alc_mode) {
                case ALC_MODE_RX:
                    alc_on_rx(alc_ref_level);
                    alc_ref_level_prev = alc_ref_level;
                    break;
                case ALC_MODE_TX:
                    alc_on_tx(alc_ref_level);
                    alc_ref_level_prev = alc_ref_level;
                    break;
                default:
                    alc_off();
                    break;
                }
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
            ImGui::Begin("Mic AMP Analog Gain");
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

            ImGui::End();
        }

        if (connect) {

            char name[48];
            switch (alc_mode) {
                case ALC_MODE_RX:
                    strcpy(name, "5-Band Parametric Equalizer [ RX ]");
                    break;
                default:
                    strcpy(name, "5-Band Parametric Equalizer [ TX ]");
            }

            ImGui::Begin(name);
            ImGui::Text("    ENB  Center(Hz)  Width(Hz)   K(Gain)");
            ImGui::PushItemWidth(72.0f);

            ImGui::Text("EQ1"); ImGui::SameLine();
            ImGui::Checkbox("##EQ1 ENB", &eq[0].enable); ImGui::SameLine();
            ImGui::SliderInt("##EQ1 fo", &eq[0].fo, 150, 10000); ImGui::SameLine();
            ImGui::SliderInt("##EQ1 fb", &eq[0].fb, 50, 5000); ImGui::SameLine();
            ImGui::SliderFloat("##EQ1 K", &eq[0].k, -1.0f, 2.999f, "%.1f");

            ImGui::Text("EQ2"); ImGui::SameLine();
            ImGui::Checkbox("##EQ2 ENB", &eq[1].enable); ImGui::SameLine();
            ImGui::SliderInt("##EQ2 fo", &eq[1].fo, 150, 10000); ImGui::SameLine();
            ImGui::SliderInt("##EQ2 fb", &eq[1].fb, 50, 5000); ImGui::SameLine();
            ImGui::SliderFloat("##EQ2 K", &eq[1].k, -1.0f, 2.999f, "%.1f");

            ImGui::Text("EQ3"); ImGui::SameLine();
            ImGui::Checkbox("##EQ3 ENB", &eq[2].enable); ImGui::SameLine();
            ImGui::SliderInt("##EQ3 fo", &eq[2].fo, 150, 10000); ImGui::SameLine();
            ImGui::SliderInt("##EQ3 fb", &eq[2].fb, 50, 5000); ImGui::SameLine();
            ImGui::SliderFloat("##EQ3 K", &eq[2].k, -1.0f, 2.999f, "%.1f");

            ImGui::Text("EQ4"); ImGui::SameLine();
            ImGui::Checkbox("##EQ4 ENB", &eq[3].enable); ImGui::SameLine();
            ImGui::SliderInt("##EQ4 fo", &eq[3].fo, 150, 10000); ImGui::SameLine();
            ImGui::SliderInt("##EQ4 fb", &eq[3].fb, 50, 5000); ImGui::SameLine();
            ImGui::SliderFloat("##EQ4 K", &eq[3].k, -1.0f, 2.999f, "%.1f");

            ImGui::Text("EQ5"); ImGui::SameLine();
            ImGui::Checkbox("##EQ5 ENB", &eq[4].enable); ImGui::SameLine();
            ImGui::SliderInt("##EQ5 fo", &eq[4].fo, 150, 10000); ImGui::SameLine();
            ImGui::SliderInt("##EQ5 fb", &eq[4].fb, 50, 5000); ImGui::SameLine();
            ImGui::SliderFloat("##EQ5 K", &eq[4].k, -1.0f, 2.999f, "%.1f");
            ImGui::Text("                               -1 < K < 3");
            ImGui::PopItemWidth();

            for (unsigned int ch = 0; ch < EQ_NUM; ch++) {
                set_eq(ch);
            }

            ImGui::End();
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

    close(sock);

    return 0;
}


void write_ak4951(unsigned char reg_addr, unsigned char reg_data) {
    memcpy(txdata + 7, &reg_addr, 1);
    memcpy(txdata + 8, &reg_data, 1);

    sendto(sock, (const char*)txdata, DATA_LENGTH, 0, (struct sockaddr*)&addr, sizeof(addr));
    printf("send data: %x, %x\n", reg_addr, reg_data);
    usleep(10000);
}


void read_ak4951(unsigned char reg_addr, unsigned char* reg_data) {
    memcpy(reqdata + 7, &reg_addr, 1);
    sendto(sock, (const char*)reqdata, DATA_LENGTH, 0, (struct sockaddr*)&addr, sizeof(addr));
    usleep(100000);

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


void set_eq(unsigned int ch) {
    if ((ch >= 0) && (ch < EQ_NUM)) {

        int fo = eq[ch].fo;
        int fb = eq[ch].fb;
        float k = eq[ch].k;

        if ((eq[ch].enable != eq[ch].enable_prev) || (fo != eq[ch].fo_prev) || (fb != eq[ch].fb_prev) || (k != eq[ch].k_prev)) {

            eq[ch].enable_prev = eq[ch].enable;
            eq[ch].fo_prev = fo;
            eq[ch].fb_prev = fb;
            eq[ch].k_prev = k;

            unsigned char dis = 0;
            unsigned char enb = 0;
            for (int i = EQ_NUM - 1; i >= 0; i--) {
                dis <<= 1;
                enb <<= 1;
                if (eq[i].enable) {
                    enb |= 0x01;
                    if (i != (int)ch) dis |= 0x01;
                }
            }

            // disable EQ
            write_ak4951(0x30, dis);   // disable EQ(ch)

            // calcurate
            const float pi = 3.1415926f;
            const float fs = 48000.0f;
            float x = cosf(2.0f * pi * (float)fo / fs);
            float y = tanf(pi * (float)fb / fs);
            float z = 1.0f + y;

            int16_t a = (int16_t)(8192.0f * k * y / z + 0.5f);
            int16_t b = (int16_t)(8192.0f * x * 2.0f / z + 0.5f);
            int16_t c = (int16_t)(8192.0f * (y - 1.0f) / z + 0.5f);

            // set EQ coefficients
            write_ak4951(eq[ch].EA_addr, a & 0xFF);            // Coeff0 ; ExA (LSB)
            write_ak4951(eq[ch].EA_addr + 1, (a >> 8) & 0xFF); // Coeff1 ; ExA (MSB)
            write_ak4951(eq[ch].EB_addr, b & 0xFF);            // Coeff2 ; ExB (LSB)
            write_ak4951(eq[ch].EB_addr + 1, (b >> 8) & 0xFF); // Coeff3 ; ExB (MSB)
            write_ak4951(eq[ch].EC_addr, c & 0xFF);            // Coeff4 ; ExC (LSB)
            write_ak4951(eq[ch].EC_addr + 1, (c >> 8) & 0xFF); // Coeff5 ; ExC (MSB)

            // enable EQ
            write_ak4951(0x30, enb);   // enable EQ(ch)
        }
    }
}


bool save_setting_items(void) {
    std::string savePath = std::string(std::getenv("HOME")) + "/.config/ak4951_ctrl/setting.dat";
    FILE* fp = fopen(savePath.c_str(), "w");
    if (fp == NULL) {
        printf("Error! setting.dat can not be opened!!");
        return false;
    }

    fprintf(fp, "%s\n", IPadr);
    fprintf(fp, "%d\n", alc_mode);
    fprintf(fp, "%d\n", alc_ref_level);
    fprintf(fp, "%d\n", mic_gain);
    fprintf(fp, "%d\n", sp_enable);

    for (unsigned int ch = 0; ch < EQ_NUM; ch++) {
        fprintf(fp, "%d\n", eq[ch].enable);
        fprintf(fp, "%d\n", eq[ch].fo);
        fprintf(fp, "%d\n", eq[ch].fb);
        fprintf(fp, "%f\n", eq[ch].k);
    }

    fclose(fp);
    printf("Save setting is completed");
    return true;
}


bool load_setting_items(void) {
    std::string loadPath = std::string(std::getenv("HOME")) + "/.config/ak4951_ctrl/setting.dat";
    FILE* fp = fopen(loadPath.c_str(), "r");
    if (fp == NULL) {
        printf("Error! setting.dat can not be opened!!");
        return false;
    }

    fscanf(fp, "%s", IPadr);
    fscanf(fp, "%d", &alc_mode);
    fscanf(fp, "%d", &alc_ref_level);
    fscanf(fp, "%d", &mic_gain);
    fscanf(fp, "%d", &sp_enable);

    int tmp;
    for (unsigned int ch = 0; ch < EQ_NUM; ch++) {
        fscanf(fp, "%d", &tmp); eq[ch].enable = (bool)tmp;
        fscanf(fp, "%d", &eq[ch].fo);
        fscanf(fp, "%d", &eq[ch].fb);
        fscanf(fp, "%f", &eq[ch].k);
    }

    fclose(fp);
    printf("Load setting is completed");
    return true;
}