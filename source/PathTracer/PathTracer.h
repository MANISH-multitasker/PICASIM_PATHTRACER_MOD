#pragma once

#include "../Framework/Helpers.h"   // Vector3, Transform

#include <vector>
#include <deque>
#include <string>

/// PathTracer — records the drone's 3D flight path and renders it
/// as a colour-gradient line strip using PicaSim's DebugRenderer.
class PathTracer
{
public:
    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------
    struct TrailPoint
    {
        Vector3 position;
        float   timestamp;   // seconds since recording started
        float   speed;       // m/s
    };

    struct FlightTimingRecord
    {
        int         runNumber    = 0;
        float       duration     = 0.0f; // seconds
        float       maxSpeed     = 0.0f; // m/s
        float       maxAltitude  = 0.0f; // m
        float       distance     = 0.0f; // m
        std::string label;               // "Free Flight", "Shape Challenge", etc.
        std::string timeOfDay;           // "HH:MM:SS"
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    PathTracer();
    ~PathTracer();

    /// Call once after OpenGL context is ready
    void Init();

    /// Call when OpenGL context is being destroyed
    void Shutdown();

    // -----------------------------------------------------------------------
    // Per-frame calls
    // -----------------------------------------------------------------------

    /// Call after physics — records drone position if recording
    void Update(const Vector3& dronePos, float droneSpeed, float deltaTime);

    /// Call during 3D render pass — draws the trail via DebugRenderer
    void RenderTrail(const float* /*unused*/);

    /// Call inside ImGui frame — draws the control HUD
    void RenderHUD();

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------
    void StartRecording();
    void StopRecording();
    void ClearTrail();

    bool  IsRecording()   const { return m_Recording; }
    int   GetPointCount() const { return (int)m_Points.size(); }

    void  SetMaxPoints(int n)           { m_MaxPoints = n; }
    void  SetSampleInterval(float s)    { m_SampleInterval = s; }
    void  SetTrailWidth(float w)        { m_TrailWidth = w; }
    void  SetOpacity(float o)           { m_Opacity = o; }

    const std::deque<TrailPoint>& GetPoints() const { return m_Points; }

    // HUD Visibility and Top Timer
    bool  IsHUDVisible() const          { return m_ShowHUD; }
    void  SetHUDVisible(bool v)         { m_ShowHUD = v; }
    void  ToggleHUD()                   { m_ShowHUD = !m_ShowHUD; }

    bool  IsTopTimerVisible() const     { return m_ShowTopTimer; }
    void  SetTopTimerVisible(bool v)    { m_ShowTopTimer = v; }
    void  ToggleTopTimer()              { m_ShowTopTimer = !m_ShowTopTimer; }

    float GetFlightTime() const         { return m_FlightTimer; }
    void  ResetFlightTime();
    void  ToggleTimer()                 { m_TimerRunning = !m_TimerRunning; }
    bool  IsTimerRunning() const        { return m_TimerRunning; }

    // Past Flight Timings History API
    void  SaveCurrentTiming(const char* label = "Free Flight");
    void  ClearTimings();
    const std::vector<FlightTimingRecord>& GetPastTimings() const { return m_PastTimings; }
    void  LoadTimingsFromFile();
    void  SaveTimingsToFile();

private:
    void   RenderTopTimer();
    // Private shader stubs kept for linker (implementation is no-op)
    void   BuildShaders();
    void   UploadToGPU();
    unsigned int CompileShader(unsigned int type, const char* src);
    unsigned int LinkProgram(unsigned int vert, unsigned int frag);

    // -----------------------------------------------------------------------
    // Trail state
    // -----------------------------------------------------------------------
    std::deque<TrailPoint> m_Points;

    bool  m_Recording      = true;     // Auto-record on start
    float m_TimeSinceStart = 0.f;
    float m_LastSampleTime = 999.f;    // Positive so first point samples immediately!
    float m_SampleInterval = 0.05f;    // 20 samples/sec
    int   m_MaxPoints      = 10000;

    // Visual settings
    float m_ColourNew[3]  = {0.0f, 1.0f, 1.0f};   // cyan  (freshest)
    float m_ColourOld[3]  = {0.1f, 0.2f, 1.0f};   // blue  (oldest)
    float m_TrailWidth    = 4.0f;
    float m_Opacity       = 1.0f;

    bool  m_GLReady       = false;
    bool  m_DirtyGPU      = false;

    // HUD and Flight Timer state
    bool    m_ShowHUD       = true;
    bool    m_ShowTopTimer  = true;
    float   m_FlightTimer   = 0.0f;
    bool    m_TimerRunning  = true;
    Vector3 m_CurrentPos    = Vector3(0, 0, 0);
    float   m_CurrentSpeed  = 0.0f;

    // Flight Lap Telemetry & History Log
    float   m_LapMaxSpeed   = 0.0f;
    float   m_LapMaxAlt     = 0.0f;
    float   m_LapDistance   = 0.0f;
    Vector3 m_PrevPos       = Vector3(0, 0, 0);
    bool    m_HasPrevPos    = false;
    int     m_RunCounter    = 0;
    std::vector<FlightTimingRecord> m_PastTimings;

    // Direct render buffers
    std::vector<Vector3> m_RenderPos;
    std::vector<Vector4> m_RenderCol;
};

/// Global singleton
extern PathTracer g_PathTracer;
