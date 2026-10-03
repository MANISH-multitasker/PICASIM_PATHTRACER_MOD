// =========================================================
// PICASIM PathTracer Mod
// =========================================================
// Original Author : MANISH
// GitHub          : https://github.com/MANISH-multitasker
// Repository      : https://github.com/MANISH-multitasker/PICASIM_PATHTRACER_MOD
// License         : CC BY-NC 4.0
// Year            : 2026
//
// This file is part of the PathTracer modification for PicaSim.
// Unauthorized redistribution without credit to the original
// author (MANISH) is a violation of the CC BY-NC 4.0 license.
// =========================================================

#include "PathTracer.h"

#include "../Platform/S3ECompat.h"
#include "../Framework/Helpers.h"
#include "../Framework/RenderManager.h"
#include "../Framework/ShaderManager.h"
#include "../Framework/Shaders.h"
#include "../Framework/Graphics.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "imgui.h"

// ============================================================
//  Global singleton
// ============================================================
PathTracer g_PathTracer;

// ============================================================
//  Lifecycle
// ============================================================
PathTracer::PathTracer()  = default;
PathTracer::~PathTracer() = default;

void PathTracer::Init()
{
    fprintf(stderr, "[PathTracer] Init() called — direct line strip rendering\n");
    m_GLReady = true;
    m_Recording = true;
    m_TimeSinceStart = 0.f;
    m_LastSampleTime = 999.f;   // Positive so the 1st sample records immediately!
    LoadTimingsFromFile();
    fprintf(stderr, "[PathTracer] Ready and auto-recording.\n");
}

void PathTracer::Shutdown()
{
    fprintf(stderr, "[PathTracer] Shutdown() — %d pts total\n", (int)m_Points.size());
    m_GLReady = false;
}

// ============================================================
//  Per-frame update (called after physics)
// ============================================================
void PathTracer::Update(const Vector3& dronePos, float droneSpeed, float deltaTime)
{
    m_CurrentPos = dronePos;
    m_CurrentSpeed = droneSpeed;
    if (m_TimerRunning)
    {
        m_FlightTimer += deltaTime;
        if (droneSpeed > m_LapMaxSpeed) m_LapMaxSpeed = droneSpeed;
        if (dronePos.z > m_LapMaxAlt)   m_LapMaxAlt = dronePos.z;

        if (m_HasPrevPos)
        {
            m_LapDistance += (dronePos - m_PrevPos).GetLength();
        }
        m_PrevPos = dronePos;
        m_HasPrevPos = true;
    }

    if (!m_Recording) return;

    m_TimeSinceStart += deltaTime;
    m_LastSampleTime += deltaTime;

    // Check sampling interval (e.g. 0.05s = 20 pts/sec)
    if (m_LastSampleTime < m_SampleInterval) return;
    m_LastSampleTime = 0.f;

    TrailPoint pt;
    pt.position  = dronePos;
    pt.timestamp = m_TimeSinceStart;
    pt.speed     = droneSpeed;
    m_Points.push_back(pt);

    // Periodic log to terminal for confirmation
    if (m_Points.size() % 50 == 0 || m_Points.size() == 1)
    {
        fprintf(stderr, "[PathTracer] %d pts recorded. pos=(%.1f, %.1f, %.1f) speed=%.1f\n",
                (int)m_Points.size(),
                dronePos.x, dronePos.y, dronePos.z, droneSpeed);
    }

    // Ring buffer — discard oldest if cap exceeded
    if (m_Points.size() > (size_t)m_MaxPoints)
        m_Points.pop_front();

    m_DirtyGPU = true;
}

// ============================================================
//  RenderTrail — draw 3D flight path line strip via SimpleShader
// ============================================================
void PathTracer::RenderTrail(const float* /*vp4x4 — unused*/)
{
    if (!m_GLReady || m_Points.size() < 2) return;

    int n = (int)m_Points.size();
    if ((int)m_RenderPos.size() < n)
    {
        m_RenderPos.resize(n);
        m_RenderCol.resize(n);
    }

    Vector3 colNew(m_ColourNew[0], m_ColourNew[1], m_ColourNew[2]);
    Vector3 colOld(m_ColourOld[0], m_ColourOld[1], m_ColourOld[2]);

    for (int i = 0; i < n; ++i)
    {
        float t = (float)i / (float)(n - 1); // 0 = oldest point, 1 = newest point
        m_RenderPos[i] = m_Points[i].position;
        m_RenderCol[i] = Vector4(
            colOld.x + t * (colNew.x - colOld.x),
            colOld.y + t * (colNew.y - colOld.y),
            colOld.z + t * (colNew.z - colOld.z),
            m_Opacity
        );
    }

    const SimpleShader* simpleShader = (SimpleShader*) ShaderManager::GetInstance().GetShader(SHADER_SIMPLE);
    if (!simpleShader) return;

    simpleShader->Use();

    esPushMatrix();
    esSetModelViewProjectionMatrix(simpleShader->u_mvpMatrix);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glLineWidth(m_TrailWidth);

    glEnableVertexAttribArray(simpleShader->a_position);
    glEnableVertexAttribArray(simpleShader->a_colour);

    glVertexAttribPointer(simpleShader->a_position, 3, GL_FLOAT, GL_FALSE, 0, &m_RenderPos[0].x);
    glVertexAttribPointer(simpleShader->a_colour, 4, GL_FLOAT, GL_FALSE, 0, &m_RenderCol[0].x);

    glDrawArrays(GL_LINE_STRIP, 0, n);

    glDisableVertexAttribArray(simpleShader->a_position);
    glDisableVertexAttribArray(simpleShader->a_colour);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    esPopMatrix();

    static bool firstDraw = true;
    if (firstDraw)
    {
        firstDraw = false;
        fprintf(stderr, "[PathTracer] FIRST RenderTrail call drawing %d points!\n", n);
    }
}

// ============================================================
//  Controls
// ============================================================
void PathTracer::StartRecording()
{
    m_Recording      = true;
    m_TimeSinceStart = 0.f;
    m_LastSampleTime = 999.f; // sample immediately
    fprintf(stderr, "[PathTracer] Recording STARTED\n");
}

void PathTracer::StopRecording()
{
    m_Recording = false;
    fprintf(stderr, "[PathTracer] Recording STOPPED — %d pts preserved\n", (int)m_Points.size());
}

void PathTracer::ClearTrail()
{
    m_Points.clear();
    m_RenderPos.clear();
    m_RenderCol.clear();
    m_LastSampleTime = 999.f;
    m_DirtyGPU = false;
    fprintf(stderr, "[PathTracer] Trail CLEARED\n");
}

// ============================================================
//  Flight Timings History Management & Persistence
// ============================================================
void PathTracer::ResetFlightTime()
{
    if (m_FlightTimer > 1.0f)
    {
        SaveCurrentTiming("Free Flight");
    }
    m_FlightTimer  = 0.0f;
    m_LapMaxSpeed  = 0.0f;
    m_LapMaxAlt    = 0.0f;
    m_LapDistance  = 0.0f;
    m_HasPrevPos   = false;
}

void PathTracer::SaveCurrentTiming(const char* label)
{
    if (m_FlightTimer < 0.5f) return;

    FlightTimingRecord rec;
    m_RunCounter++;
    rec.runNumber   = m_RunCounter;
    rec.duration    = m_FlightTimer;
    rec.maxSpeed    = m_LapMaxSpeed;
    rec.maxAltitude = m_LapMaxAlt;
    rec.distance    = m_LapDistance;
    rec.label       = label ? label : "Free Flight";

    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    char buf[16];
    if (timeinfo)
    {
        strftime(buf, sizeof(buf), "%H:%M:%S", timeinfo);
        rec.timeOfDay = buf;
    }
    else
    {
        rec.timeOfDay = "--:--:--";
    }

    m_PastTimings.push_back(rec);
    SaveTimingsToFile();
    fprintf(stderr, "[PathTracer] Timing saved: Run #%d - %.2fs (%s)\n", rec.runNumber, rec.duration, rec.label.c_str());
}

void PathTracer::ClearTimings()
{
    m_PastTimings.clear();
    SaveTimingsToFile();
    fprintf(stderr, "[PathTracer] Timings history cleared.\n");
}

void PathTracer::SaveTimingsToFile()
{
    std::ofstream out("flight_timings.txt", std::ios::trunc);
    if (!out.is_open()) return;

    for (const auto& rec : m_PastTimings)
    {
        out << rec.runNumber << "\t"
            << rec.duration << "\t"
            << rec.maxSpeed << "\t"
            << rec.maxAltitude << "\t"
            << rec.distance << "\t"
            << rec.timeOfDay << "\t"
            << rec.label << "\n";
    }
}

void PathTracer::LoadTimingsFromFile()
{
    m_PastTimings.clear();
    std::ifstream in("flight_timings.txt");
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;
        std::stringstream ss(line);
        FlightTimingRecord rec;
        if (ss >> rec.runNumber >> rec.duration >> rec.maxSpeed >> rec.maxAltitude >> rec.distance >> rec.timeOfDay)
        {
            std::string labelPart;
            std::getline(ss, labelPart);
            size_t first = labelPart.find_first_not_of(" \t");
            if (first != std::string::npos)
                rec.label = labelPart.substr(first);
            else
                rec.label = "Flight";

            if (rec.runNumber > m_RunCounter)
                m_RunCounter = rec.runNumber;

            m_PastTimings.push_back(rec);
        }
    }
    fprintf(stderr, "[PathTracer] Loaded %d past timing records from file.\n", (int)m_PastTimings.size());
}

#include "ShapeChallenge.h"

// ============================================================
//  Top-Center Flight Timer / Time Calculator (Frosted Glass Pill)
// ============================================================
void PathTracer::RenderTopTimer()
{
    if (!m_ShowTopTimer) return;

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;

    int totalSec = (int)m_FlightTimer;
    int minutes = totalSec / 60;
    float seconds = m_FlightTimer - (float)(minutes * 60);

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%04.1f", minutes, seconds);

    // Frosted Glass Pill Styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 7.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.09f, 0.15f, 0.75f)); // Translucent midnight glass
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.35f, 0.68f, 0.98f, 0.55f)); // Cyan ice edge glow

    // Center horizontally using pivot (0.5f, 0.0f) with automatic content resizing
    ImGui::SetNextWindowPos(ImVec2(screenW * 0.5f, 10.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("TopFlightTimer", nullptr, flags))
    {
        // Badge & Digital timer readout
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "FLIGHT TIME: ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", timeBuf);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.40f, 0.55f, 0.70f, 0.80f), "|");
        ImGui::SameLine();

        // Telemetry
        ImGui::TextColored(ImVec4(0.70f, 0.80f, 0.92f, 0.85f), "Alt: %.1fm", m_CurrentPos.z);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.70f, 0.80f, 0.92f, 0.85f), "Spd: %.1f", m_CurrentSpeed);
        ImGui::SameLine();

        // Mini Pause / Play button
        if (ImGui::SmallButton(m_TimerRunning ? "||" : ">"))
        {
            ToggleTimer();
        }
        ImGui::SameLine();

        // Mini Reset button
        if (ImGui::SmallButton("R"))
        {
            ResetFlightTime();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ============================================================
//  ImGui HUD (Frosted Glassmorphism Theme)
// ============================================================
void PathTracer::RenderHUD()
{
    // 1. Always render the top middle flight timer
    RenderTopTimer();

    // 2. Hide / Unhide check (F4 or H toggle)
    if (!m_ShowHUD)
        return;

    // 3. Frosted Glassmorphism Theme Styling
    ImGui::PushStyleColor(ImGuiCol_WindowBg,           ImVec4(0.06f, 0.09f, 0.15f, 0.72f)); // Translucent frosted backdrop
    ImGui::PushStyleColor(ImGuiCol_Border,             ImVec4(0.35f, 0.65f, 0.95f, 0.45f)); // Crisp glass rim
    ImGui::PushStyleColor(ImGuiCol_TitleBg,            ImVec4(0.09f, 0.14f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,      ImVec4(0.12f, 0.20f, 0.32f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,            ImVec4(0.15f, 0.22f, 0.34f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,     ImVec4(0.22f, 0.34f, 0.52f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,      ImVec4(0.25f, 0.40f, 0.62f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Button,             ImVec4(0.16f, 0.28f, 0.44f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,      ImVec4(0.24f, 0.44f, 0.70f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,       ImVec4(0.18f, 0.55f, 0.90f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Header,             ImVec4(0.18f, 0.30f, 0.48f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,      ImVec4(0.25f, 0.42f, 0.68f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,       ImVec4(0.20f, 0.50f, 0.82f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Tab,                ImVec4(0.10f, 0.16f, 0.26f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,         ImVec4(0.22f, 0.40f, 0.66f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TabActive,          ImVec4(0.18f, 0.35f, 0.58f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused,       ImVec4(0.08f, 0.12f, 0.20f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.14f, 0.25f, 0.42f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,         ImVec4(0.28f, 0.75f, 1.00f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,   ImVec4(0.45f, 0.88f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,          ImVec4(0.35f, 0.95f, 0.70f, 1.00f));

    // Smooth rounded geometry vars
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,    12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,  1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,     6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,     8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,      6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,       6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,     ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       ImVec2(8.0f, 6.0f));

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 430), ImGuiCond_FirstUseEver);

    // Provide &m_ShowHUD so clicking the standard [X] in the top-right corner closes it too
    ImGui::Begin("Flight Mod  [F4 to Toggle]", &m_ShowHUD, ImGuiWindowFlags_NoCollapse);

    ImGui::TextDisabled("Press [F4] or [H] to hide/show | [F7] Reset Timer");
    ImGui::Separator();

    if (ImGui::BeginTabBar("FlightModTabs"))
    {
        if (ImGui::BeginTabItem("Path Tracer"))
        {
            if (m_Recording)
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[RECORDING]");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[STOPPED]");

            ImGui::SameLine();
            ImGui::Text("  %d pts", (int)m_Points.size());
            ImGui::Separator();

            if (!m_Recording)
            {
                if (ImGui::Button("Start REC", ImVec2(90, 26))) StartRecording();
            }
            else
            {
                if (ImGui::Button("Stop REC",  ImVec2(90, 26))) StopRecording();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Path", ImVec2(90, 26))) { ClearTrail(); }

            ImGui::Separator();
            ImGui::SliderFloat("Width",   &m_TrailWidth, 1.0f, 15.0f, "%.1f");
            ImGui::SliderFloat("Opacity", &m_Opacity,    0.1f, 1.0f,  "%.2f");
            ImGui::ColorEdit3("Color (new)", m_ColourNew, ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Color (old)", m_ColourOld, ImGuiColorEditFlags_NoInputs);

            ImGui::Separator();
            ImGui::Checkbox("Show Top Timer", &m_ShowTopTimer);
            ImGui::SameLine();
            if (ImGui::Button("Reset Timer")) { ResetFlightTime(); }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shape Challenge"))
        {
            g_ShapeChallenge.RenderUI();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Past Timings"))
        {
            // Action buttons
            if (ImGui::Button("+ Log Current Lap", ImVec2(140, 26)))
            {
                SaveCurrentTiming("Manual Split");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear History", ImVec2(100, 26)))
            {
                ClearTimings();
            }

            ImGui::Separator();

            if (m_PastTimings.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("No timing records stored yet.");
                ImGui::Spacing();
                ImGui::TextWrapped("Tips:\n- Click [+ Log Current Lap] to save anytime.\n- Press [F7] to reset and auto-save timing.\n- Or finish a Shape Challenge to log automatically.");
            }
            else
            {
                float maxDur = 0.0f;
                float sumDur = 0.0f;
                for (const auto& r : m_PastTimings)
                {
                    if (r.duration > maxDur) maxDur = r.duration;
                    sumDur += r.duration;
                }
                int totMins = (int)sumDur / 60;
                float totSecs = sumDur - (float)(totMins * 60);

                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Total Flights: %d  |  Total: %02d:%02.0fs",
                                   (int)m_PastTimings.size(), totMins, totSecs);
                ImGui::Spacing();

                ImGui::BeginChild("TimingsHistoryList", ImVec2(0, 240), true);

                for (int i = (int)m_PastTimings.size() - 1; i >= 0; --i)
                {
                    const auto& rec = m_PastTimings[i];
                    int m = (int)rec.duration / 60;
                    float s = rec.duration - (float)(m * 60);

                    bool isBest = (rec.duration == maxDur && maxDur > 0.0f);

                    if (isBest)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "#%02d  %02d:%04.1f  [BEST FLIGHT]",
                                           rec.runNumber, m, s);
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "#%02d  %02d:%04.1f",
                                           rec.runNumber, m, s);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(" %s", rec.timeOfDay.c_str());

                    ImGui::TextDisabled("  %s", rec.label.c_str());
                    ImGui::TextDisabled("  Max Spd: %.1f m/s | Alt: %.1fm | Dist: %.1fm",
                                        rec.maxSpeed, rec.maxAltitude, rec.distance);
                    ImGui::Separator();
                }

                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    ImGui::PopStyleVar(8);
    ImGui::PopStyleColor(21);
}

// ============================================================
//  Unused shader stubs (kept so header compiles cleanly)
// ============================================================
void PathTracer::UploadToGPU() {}
unsigned int PathTracer::CompileShader(unsigned int, const char*) { return 0; }
unsigned int PathTracer::LinkProgram(unsigned int, unsigned int)   { return 0; }
void PathTracer::BuildShaders() {}
