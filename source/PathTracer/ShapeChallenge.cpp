#include "ShapeChallenge.h"
#include "PathTracer.h"

#include "../Platform/S3ECompat.h"
#include "../Framework/Helpers.h"
#include "../Framework/RenderManager.h"
#include "../Framework/ShaderManager.h"
#include "../Framework/Shaders.h"
#include "../Framework/Graphics.h"
#include "../PicaSim/PicaSim.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>

#include "imgui.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
//  Global singleton
// ============================================================
ShapeChallenge g_ShapeChallenge;

// ============================================================
//  Helper math
// ============================================================
static Vector3 RotateZ(const Vector3& v, float angleRad)
{
    float c = std::cos(angleRad);
    float s = std::sin(angleRad);
    return Vector3(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

// ============================================================
//  Live Drone Position Tracking
// ============================================================
bool ShapeChallenge::GetLiveDroneTransform(Vector3& outPos, Vector3& outFwd)
{
    if (PicaSim::IsCreated())
    {
        Aeroplane* plane = PicaSim::GetInstance().GetPlayerAeroplane();
        if (plane)
        {
            const Transform& tm = plane->GetTransform();
            outPos = tm.GetTrans();
            outFwd = tm.RowX();
            m_CurrentDronePos = outPos;
            m_CurrentDroneFwd = outFwd;
            m_HasValidDronePos = true;
            return true;
        }
    }
    if (m_HasValidDronePos)
    {
        outPos = m_CurrentDronePos;
        outFwd = m_CurrentDroneFwd;
        return true;
    }
    return false;
}

void ShapeChallenge::UpdateLiveDrone(const Vector3& dronePos, const Vector3& droneFwd)
{
    m_CurrentDronePos = dronePos;
    m_CurrentDroneFwd = droneFwd;
    m_HasValidDronePos = true;
}

// ============================================================
//  Lifecycle
// ============================================================
ShapeChallenge::ShapeChallenge()  = default;
ShapeChallenge::~ShapeChallenge() = default;

void ShapeChallenge::Init()
{
    fprintf(stderr, "[ShapeChallenge] Init() called\n");
    m_State = CHALLENGE_STATE_PREVIEW;
    m_CurrentShape = SHAPE_CIRCLE;
    m_Scale = 12.0f;
    m_GhostWidth = 6.0f;
    m_ShowGhost = true;
    m_LastResult = ChallengeScoreResult();
}

void ShapeChallenge::Reset()
{
    m_State = CHALLENGE_STATE_PREVIEW;
    m_GhostPoints.clear();
    m_GhostRenderPos.clear();
    m_GhostRenderCol.clear();
    m_MarkerRenderPos.clear();
    m_MarkerRenderCol.clear();
    m_ChallengeFlownPoints.clear();
    m_ChallengeTimer = 0.0f;
    m_TotalDistanceFlown = 0.0f;
    m_LiveDeviation = 0.0f;
    m_LiveAccuracy = 100.0f;
    m_ProgressWaypointIndex = 0;
    m_PathProgressPercent = 0.0f;
}

const char* ShapeChallenge::GetShapeName(ShapeType type)
{
    switch (type)
    {
    case SHAPE_CIRCLE:       return "Circle";
    case SHAPE_SQUARE:       return "Square";
    case SHAPE_RECTANGLE:    return "Rectangle";
    case SHAPE_TRIANGLE:     return "Triangle";
    case SHAPE_FIGURE_EIGHT: return "Figure-Eight (Infinity)";
    default:                 return "Unknown";
    }
}

void ShapeChallenge::SetShape(ShapeType type)
{
    m_CurrentShape = type;
    Vector3 pos = m_CurrentDronePos;
    Vector3 fwd = m_CurrentDroneFwd;
    GetLiveDroneTransform(pos, fwd);
    SpawnGhost(pos, fwd);
}

void ShapeChallenge::SetScale(float s)
{
    m_Scale = std::max(4.0f, std::min(100.0f, s));
    Vector3 pos = m_CurrentDronePos;
    Vector3 fwd = m_CurrentDroneFwd;
    GetLiveDroneTransform(pos, fwd);
    SpawnGhost(pos, fwd);
}

// ============================================================
//  Spawn Ghost / Start / Stop
// ============================================================
void ShapeChallenge::SpawnGhost(const Vector3& dronePos, const Vector3& droneFwd)
{
    Vector3 pos = dronePos;
    Vector3 fwd = droneFwd;
    GetLiveDroneTransform(pos, fwd);

    // Compute yaw from horizontal forward vector
    Vector3 horFwd(fwd.x, fwd.y, 0.0f);
    if (horFwd.GetLengthSquared() > 0.001f)
    {
        horFwd.Normalise();
        m_HeadingYaw = std::atan2(horFwd.y, horFwd.x);
    }
    else
    {
        m_HeadingYaw = 0.0f;
    }

    // Anchor the start position EXACTLY at the live drone location!
    m_Center = pos;

    // Check terrain altitude if available
    if (PicaSim::IsCreated())
    {
        float terrainZ = Environment::GetInstance().GetTerrain().GetTerrainHeight(pos.x, pos.y, false);
        // If resting on ground (or close to ground < 0.8m), hover start gate comfortably 1.6m above ground
        if (m_Center.z < terrainZ + 0.8f)
        {
            m_Center.z = terrainZ + 1.6f;
        }
    }

    m_Altitude = m_Center.z;

    RegenerateShape();
    m_State = CHALLENGE_STATE_PREVIEW;
    m_ShowGhost = true;
    fprintf(stderr, "[ShapeChallenge] Ghost %s spawned at drone position (%.1f, %.1f, %.1f) heading=%.1f deg\n",
            GetShapeName(m_CurrentShape), m_Center.x, m_Center.y, m_Center.z, m_HeadingYaw * 180.0f / 3.14159f);
}

void ShapeChallenge::StartChallenge(const Vector3& dronePos, const Vector3& droneFwd)
{
    Vector3 pos = dronePos;
    Vector3 fwd = droneFwd;
    GetLiveDroneTransform(pos, fwd);

    // Always re-anchor the shape start gate right at the live drone position when challenge begins!
    SpawnGhost(pos, fwd);

    m_State = CHALLENGE_STATE_RECORDING;
    m_ChallengeFlownPoints.clear();
    m_ChallengeFlownPoints.push_back(pos);
    m_ChallengeTimer = 0.0f;
    m_TotalDistanceFlown = 0.0f;
    m_LiveDeviation = 0.0f;
    m_LiveAccuracy = 100.0f;
    m_ProgressWaypointIndex = 0;
    m_PathProgressPercent = 0.0f;

    fprintf(stderr, "[ShapeChallenge] Challenge STARTED for %s at live drone pos (%.1f, %.1f, %.1f)\n",
            GetShapeName(m_CurrentShape), pos.x, pos.y, pos.z);
}

// ============================================================
//  Mission Complete Sound (satisfying 3-tone chime, non-blocking)
// ============================================================
static void PlayMissionCompleteSound()
{
    // Run on a detached thread so Beep() does not block the sim
    std::thread([](){
        // Warm rising 3-tone success chime: Eb4 -> G4 -> Bb4
        // Each Beep(freq_hz, duration_ms)
        Beep(622, 120);  // Eb5 — short attack
        Beep(784, 120);  // G5  — mid note
        Beep(988, 320);  // B5  — long satisfying resolution
    }).detach();
}

void ShapeChallenge::StopChallenge()
{
    if (m_State == CHALLENGE_STATE_RECORDING)
    {
        ComputeFinalScore();
        m_State = CHALLENGE_STATE_COMPLETED;
        fprintf(stderr, "[ShapeChallenge] Challenge FINISHED! Score=%.1f Grade=%s\n",
                m_LastResult.finalScore, m_LastResult.grade.c_str());

        // Play satisfying mission-complete chime
        PlayMissionCompleteSound();

        // Automatically log challenge completion into Past Timings
        char labelBuf[96];
        snprintf(labelBuf, sizeof(labelBuf), "%s Challenge (Score: %.0f%%, Grade %s)",
                 GetShapeName(m_CurrentShape), m_LastResult.finalScore, m_LastResult.grade.c_str());
        g_PathTracer.SaveCurrentTiming(labelBuf);
    }
}

void ShapeChallenge::Clear()
{
    Reset();
    fprintf(stderr, "[ShapeChallenge] Challenge CLEARED\n");
}

// ============================================================
//  Shape Geometry Generation (Starts Exactly at Drone Position)
// ============================================================
void ShapeChallenge::RegenerateShape()
{
    m_GhostPoints.clear();

    switch (m_CurrentShape)
    {
    case SHAPE_CIRCLE:
    {
        // Smooth circle that starts EXACTLY at the drone (0, 0, 0)
        // Tangent at start is straight forward (+X / drone heading), banking smoothly to the left
        const int n = 120;
        float r = m_Scale * 0.5f;
        for (int i = 0; i <= n; ++i)
        {
            float t = (float)i / (float)n * (float)(2.0 * M_PI);
            // x(0) = 0, y(0) = 0, tangent is forward (+X)
            Vector3 localPt(r * std::sin(t), r * (1.0f - std::cos(t)), 0.0f);
            m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
        }
        break;
    }
    case SHAPE_SQUARE:
    {
        // Square that starts EXACTLY at the drone (0, 0, 0)
        // Side 1 flies forward from (0,0) to (s, 0)
        // Side 2 turns left to (s, s)
        // Side 3 turns left to (0, s)
        // Side 4 returns to (0, 0)
        float s = m_Scale;
        Vector3 v[4] = {
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(   s, 0.0f, 0.0f),
            Vector3(   s,    s, 0.0f),
            Vector3(0.0f,    s, 0.0f)
        };
        const int ptsPerSide = 30;
        for (int side = 0; side < 4; ++side)
        {
            Vector3 p0 = v[side];
            Vector3 p1 = v[(side + 1) % 4];
            for (int i = 0; i < ptsPerSide; ++i)
            {
                float t = (float)i / (float)ptsPerSide;
                Vector3 localPt = p0 + (p1 - p0) * t;
                m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
            }
        }
        // Close loop at start
        m_GhostPoints.push_back(m_Center + RotateZ(v[0], m_HeadingYaw));
        break;
    }
    case SHAPE_RECTANGLE:
    {
        // Rectangle starting EXACTLY at drone (0, 0, 0)
        // Forward length = scale, Width = scale * 0.5
        float l = m_Scale;
        float w = m_Scale * 0.5f;
        Vector3 v[4] = {
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(   l, 0.0f, 0.0f),
            Vector3(   l,    w, 0.0f),
            Vector3(0.0f,    w, 0.0f)
        };
        const int ptsPerSide = 30;
        for (int side = 0; side < 4; ++side)
        {
            Vector3 p0 = v[side];
            Vector3 p1 = v[(side + 1) % 4];
            for (int i = 0; i < ptsPerSide; ++i)
            {
                float t = (float)i / (float)ptsPerSide;
                Vector3 localPt = p0 + (p1 - p0) * t;
                m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
            }
        }
        m_GhostPoints.push_back(m_Center + RotateZ(v[0], m_HeadingYaw));
        break;
    }
    case SHAPE_TRIANGLE:
    {
        // Equilateral triangle starting EXACTLY at drone (0, 0, 0)
        // Side 1 flies forward: (0, 0) -> (s, 0)
        // Side 2 turns 120 deg: (s, 0) -> (s * 0.5, s * sin(60))
        // Side 3 returns: -> (0, 0)
        float s = m_Scale;
        float h = s * 0.8660254f; // s * sqrt(3)/2
        Vector3 v[3] = {
            Vector3(0.0f,      0.0f, 0.0f),
            Vector3(   s,      0.0f, 0.0f),
            Vector3(s * 0.5f,     h, 0.0f)
        };
        const int ptsPerSide = 40;
        for (int side = 0; side < 3; ++side)
        {
            Vector3 p0 = v[side];
            Vector3 p1 = v[(side + 1) % 3];
            for (int i = 0; i < ptsPerSide; ++i)
            {
                float t = (float)i / (float)ptsPerSide;
                Vector3 localPt = p0 + (p1 - p0) * t;
                m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
            }
        }
        m_GhostPoints.push_back(m_Center + RotateZ(v[0], m_HeadingYaw));
        break;
    }
    case SHAPE_FIGURE_EIGHT:
    {
        // Figure-Eight (Lemniscate) starting EXACTLY at drone (0, 0, 0)
        // Loop 1: Flies left circle starting forward from (0,0) and returning to (0,0)
        // Loop 2: Flies right circle starting forward from (0,0) and returning to (0,0)
        const int halfN = 80;
        float r = m_Scale * 0.35f;
        // Left loop
        for (int i = 0; i < halfN; ++i)
        {
            float t = (float)i / (float)halfN * (float)(2.0 * M_PI);
            Vector3 localPt(r * std::sin(t), r * (1.0f - std::cos(t)), 0.0f);
            m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
        }
        // Right loop
        for (int i = 0; i <= halfN; ++i)
        {
            float t = (float)i / (float)halfN * (float)(2.0 * M_PI);
            Vector3 localPt(r * std::sin(t), -r * (1.0f - std::cos(t)), 0.0f);
            m_GhostPoints.push_back(m_Center + RotateZ(localPt, m_HeadingYaw));
        }
        break;
    }
    default:
        break;
    }

    // Build Start / Finish indicator marker: Single green X-axis circle (compact gate)
    m_MarkerRenderPos.clear();
    m_MarkerRenderCol.clear();
    if (!m_GhostPoints.empty())
    {
        Vector3 startPt = m_GhostPoints[0];
        const int markerSegs = 32;
        // Compact gate circle: ~0.85m radius (~1.7m diameter, refined and not too fancy)
        float markerR = std::max(0.75f, std::min(1.05f, m_Scale * 0.04f));

        // Single X-axis circle: perpendicular to flight direction (facing drone heading)
        for (int i = 0; i <= markerSegs; ++i)
        {
            float ang = (float)i / (float)markerSegs * (float)(2.0 * M_PI);
            Vector3 hoopLocal(0.0f, markerR * std::cos(ang), markerR * std::sin(ang));
            m_MarkerRenderPos.push_back(startPt + RotateZ(hoopLocal, m_HeadingYaw));
            m_MarkerRenderCol.push_back(Vector4(0.20f, 0.95f, 0.35f, 0.95f)); // Clean vibrant green
        }
    }
}

// ============================================================
//  Point to Segment Distance (Horizontal 2D - Independent of Altitude)
// ============================================================
float ShapeChallenge::DistancePointToSegment2D(const Vector3& p, const Vector3& a, const Vector3& b) const
{
    // Compute distance purely in horizontal XY plane, ignoring Z (altitude variations)
    Vector3 ab(b.x - a.x, b.y - a.y, 0.0f);
    Vector3 ap(p.x - a.x, p.y - a.y, 0.0f);
    float lenSq = ab.GetLengthSquared();
    if (lenSq < 1e-6f)
        return ap.GetLength();

    float t = ap.Dot(ab) / lenSq;
    t = std::max(0.0f, std::min(1.0f, t));
    Vector3 closest = ap - ab * t;
    return closest.GetLength();
}

// ============================================================
//  Per-Frame Update
// ============================================================
void ShapeChallenge::Update(const Vector3& dronePos, const Vector3& droneFwd, float deltaTime)
{
    m_PulseTimer += deltaTime * 3.0f;
    m_CurrentDronePos = dronePos;
    m_CurrentDroneFwd = droneFwd;
    m_HasValidDronePos = true;

    // Automatically spawn guide shape if not yet created so the golden ghost line is instantly visible
    if (m_GhostPoints.empty())
    {
        SpawnGhost(dronePos, droneFwd);
    }

    if (m_State != CHALLENGE_STATE_RECORDING)
        return;

    m_ChallengeTimer += deltaTime;

    // Track distance flown
    if (!m_ChallengeFlownPoints.empty())
    {
        m_TotalDistanceFlown += (dronePos - m_ChallengeFlownPoints.back()).GetLength();
    }
    m_ChallengeFlownPoints.push_back(dronePos);

    int n = (int)m_GhostPoints.size();
    if (n >= 2)
    {
        // 1. Compute live horizontal deviation (independent of altitude variations)
        float minD = 99999.0f;
        for (size_t i = 0; i + 1 < m_GhostPoints.size(); ++i)
        {
            float d = DistancePointToSegment2D(dronePos, m_GhostPoints[i], m_GhostPoints[i + 1]);
            if (d < minD) minD = d;
        }
        m_LiveDeviation = minD;

        // Dynamic live accuracy percentage (pure horizontal tracking, no altitude penalty)
        float tol = m_Scale * 0.25f;
        float acc = 100.0f * (1.0f - std::min(1.0f, minD / tol));
        m_LiveAccuracy = std::max(0.0f, acc);

        // 2. Track live progress along waypoints (turns covered path green live)
        // Strategy: walk forward from current index; only mark waypoint[w] as reached
        // when the drone has clearly passed it — i.e., the projection onto the
        // segment [w, w+1] is > 0.85 (firmly past 85% of the segment, not just near it).
        // This prevents green from racing ahead of the drone.
        //
        // Tight reach tolerance = 1.5m or 8% of shape scale, whichever is larger.
        // This matches the physical waypoint dot size so green only colors what's behind the drone.
        float reachTol = std::max(1.5f, m_Scale * 0.08f);
        Vector3 dronePos2D(dronePos.x, dronePos.y, 0.0f);

        // Walk forward one segment at a time from current progress to avoid large jumps
        int newIdx = m_ProgressWaypointIndex;
        // Allow advancing at most a small window per frame
        int maxAdvance = std::max(6, n / 6);
        for (int w = m_ProgressWaypointIndex;
             w < m_ProgressWaypointIndex + maxAdvance && w + 1 < n;
             ++w)
        {
            Vector3 p0(m_GhostPoints[w].x,     m_GhostPoints[w].y,     0.0f);
            Vector3 p1(m_GhostPoints[w + 1].x, m_GhostPoints[w + 1].y, 0.0f);
            Vector3 seg = p1 - p0;
            float segLenSq = seg.GetLengthSquared();

            if (segLenSq < 1e-4f)
            {
                // Zero-length segment — trivially advance
                newIdx = w + 1;
                continue;
            }

            float proj = (dronePos2D - p0).Dot(seg) / segLenSq;

            if (proj >= 0.85f)
            {
                // Drone has firmly passed this segment's endpoint — mark it reached
                newIdx = w + 1;
            }
            else
            {
                // Not yet past this segment — stop advancing
                break;
            }
        }

        if (newIdx > m_ProgressWaypointIndex)
        {
            m_ProgressWaypointIndex = newIdx;
        }

        m_PathProgressPercent = (float)m_ProgressWaypointIndex / (float)(n - 1) * 100.0f;
        m_PathProgressPercent = std::max(0.0f, std::min(100.0f, m_PathProgressPercent));
    }
}

// ============================================================
//  Scoring Engine (Independent of Altitude Variations)
// ============================================================
void ShapeChallenge::ComputeFinalScore()
{
    m_LastResult = ChallengeScoreResult();
    m_LastResult.flightTime = m_ChallengeTimer;
    m_LastResult.flightDistance = m_TotalDistanceFlown;

    if (m_ChallengeFlownPoints.size() < 10 || m_GhostPoints.size() < 2)
    {
        m_LastResult.finalScore = 0.0f;
        m_LastResult.grade = "D";
        m_LastResult.feedback = "Flight was too short to score. Fly the full shape!";
        return;
    }

    size_t m = m_ChallengeFlownPoints.size();
    float totalDev = 0.0f;
    float maxDev = 0.0f;
    float sumAlt = 0.0f;

    for (size_t j = 0; j < m; ++j)
    {
        const Vector3& p = m_ChallengeFlownPoints[j];
        sumAlt += p.z;

        float minD = 99999.0f;
        for (size_t k = 0; k + 1 < m_GhostPoints.size(); ++k)
        {
            float d = DistancePointToSegment2D(p, m_GhostPoints[k], m_GhostPoints[k + 1]);
            if (d < minD) minD = d;
        }

        totalDev += minD;
        if (minD > maxDev) maxDev = minD;
    }

    float avgDev = totalDev / (float)m;

    // Altitude deviation from target (informational statistic only, not penalizing score)
    float altVariance = 0.0f;
    for (size_t j = 0; j < m; ++j)
    {
        float diff = m_ChallengeFlownPoints[j].z - m_Center.z;
        altVariance += diff * diff;
    }
    float altStdDev = std::sqrt(altVariance / (float)m);

    // Loop closure error (horizontal 2D distance between start and end point)
    Vector3 startPt2D(m_ChallengeFlownPoints.front().x, m_ChallengeFlownPoints.front().y, 0.0f);
    Vector3 endPt2D(m_ChallengeFlownPoints.back().x, m_ChallengeFlownPoints.back().y, 0.0f);
    float closureErr = (endPt2D - startPt2D).GetLength();

    m_LastResult.avgDeviation = avgDev;
    m_LastResult.maxDeviation = maxDev;
    m_LastResult.closureError = closureErr;
    m_LastResult.altitudeError = altStdDev;

    // Balanced Scoring (Independent of altitude variations):
    // 60% Average horizontal path tracking
    // 25% Maximum deviation (no wild cuts)
    // 15% Loop closure (bringing it back to start gate)
    float tolDev = m_Scale * 0.12f;
    float tolMax = m_Scale * 0.30f;
    float tolClosure = m_Scale * 0.18f;

    float scoreDev     = std::max(0.0f, 100.0f * (1.0f - avgDev / tolDev));
    float scoreMax     = std::max(0.0f, 100.0f * (1.0f - maxDev / tolMax));
    float scoreClosure = std::max(0.0f, 100.0f * (1.0f - closureErr / tolClosure));

    float finalScore = scoreDev * 0.60f + scoreMax * 0.25f + scoreClosure * 0.15f;
    m_LastResult.finalScore = std::max(0.0f, std::min(100.0f, finalScore));

    // Assign Grade and Feedback
    if (m_LastResult.finalScore >= 90.0f)
    {
        m_LastResult.grade = "S";
        m_LastResult.feedback = "Flawless aerobatics! Exceptional precision and control!";
    }
    else if (m_LastResult.finalScore >= 80.0f)
    {
        m_LastResult.grade = "A";
        m_LastResult.feedback = "Excellent flight! Very tight tracking along the guide line.";
    }
    else if (m_LastResult.finalScore >= 70.0f)
    {
        m_LastResult.grade = "B";
        m_LastResult.feedback = "Solid run! Watch corner rounding and maintain smooth throttle.";
    }
    else if (m_LastResult.finalScore >= 55.0f)
    {
        m_LastResult.grade = "C";
        m_LastResult.feedback = "Good attempt. Practice holding a steady turn and closing the loop.";
    }
    else
    {
        m_LastResult.grade = "D";
        m_LastResult.feedback = "Keep practicing! Follow the green waypoint trail to guide your flight.";
    }
}

// ============================================================
//  Ghost Shape Rendering (via SimpleShader)
// ============================================================
void ShapeChallenge::RenderGhost()
{
    if (!m_ShowGhost || m_GhostPoints.size() < 2)
        return;

    int n = (int)m_GhostPoints.size();
    if ((int)m_GhostRenderPos.size() < n)
    {
        m_GhostRenderPos.resize(n);
        m_GhostRenderCol.resize(n);
    }

    // Breathing pulse effect for high visibility
    float pulse = 0.85f + 0.15f * std::sin(m_PulseTimer);
    float alpha = m_GhostOpacity * pulse;

    const Vector4 colGreen(0.20f, 0.95f, 0.35f, alpha);
    const Vector4 colGold(m_GhostColor[0], m_GhostColor[1], m_GhostColor[2], alpha);

    for (int i = 0; i < n; ++i)
    {
        m_GhostRenderPos[i] = m_GhostPoints[i];
        if (m_State == CHALLENGE_STATE_RECORDING)
        {
            m_GhostRenderCol[i] = (i <= m_ProgressWaypointIndex) ? colGreen : colGold;
        }
        else if (m_State == CHALLENGE_STATE_COMPLETED)
        {
            m_GhostRenderCol[i] = colGreen;
        }
        else
        {
            m_GhostRenderCol[i] = colGold;
        }
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
    glDisable(GL_DEPTH_TEST); // Ensure walls and skybox do not occlude the ghost line

    glLineWidth(m_GhostWidth);

    glEnableVertexAttribArray(simpleShader->a_position);
    glEnableVertexAttribArray(simpleShader->a_colour);

    // 1. Draw main ghost path line strip
    glVertexAttribPointer(simpleShader->a_position, 3, GL_FLOAT, GL_FALSE, 0, &m_GhostRenderPos[0].x);
    glVertexAttribPointer(simpleShader->a_colour, 4, GL_FLOAT, GL_FALSE, 0, &m_GhostRenderCol[0].x);
    glDrawArrays(GL_LINE_STRIP, 0, n);

    // 2. Draw Start/Finish indicator marker
    if (m_MarkerRenderPos.size() >= 2)
    {
        glLineWidth(m_GhostWidth + 2.0f);
        glVertexAttribPointer(simpleShader->a_position, 3, GL_FLOAT, GL_FALSE, 0, &m_MarkerRenderPos[0].x);
        glVertexAttribPointer(simpleShader->a_colour, 4, GL_FLOAT, GL_FALSE, 0, &m_MarkerRenderCol[0].x);
        glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)m_MarkerRenderPos.size());
    }

    glDisableVertexAttribArray(simpleShader->a_position);
    glDisableVertexAttribArray(simpleShader->a_colour);

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    esPopMatrix();

    static bool firstGhostDraw = true;
    if (firstGhostDraw)
    {
        firstGhostDraw = false;
        fprintf(stderr, "[ShapeChallenge] FIRST RenderGhost call drawing %d points! Pos=(%.1f, %.1f, %.1f)\n",
                n, m_GhostPoints[0].x, m_GhostPoints[0].y, m_GhostPoints[0].z);
    }
}

// ============================================================
//  Challenge UI (ImGui Tab Component)
// ============================================================
void ShapeChallenge::RenderUI()
{
    // Shape selector combo
    const char* shapeItems[] = {
        "Circle",
        "Square",
        "Rectangle",
        "Triangle",
        "Figure-Eight (∞)"
    };
    int currentIdx = (int)m_CurrentShape;
    if (ImGui::Combo("Target Shape", &currentIdx, shapeItems, IM_ARRAYSIZE(shapeItems)))
    {
        SetShape((ShapeType)currentIdx);
    }

    // Scale slider
    float scale = m_Scale;
    if (ImGui::SliderFloat("Scale", &scale, 5.0f, 60.0f, "%.0f m"))
    {
        SetScale(scale);
    }

    ImGui::Checkbox("Show Ghost Line", &m_ShowGhost);
    ImGui::TextDisabled("Start gate spawns directly at your drone position");

    ImGui::SliderFloat("Ghost Width", &m_GhostWidth, 1.0f, 15.0f, "%.1f");
    ImGui::SliderFloat("Ghost Opacity", &m_GhostOpacity, 0.2f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Ghost Color", m_GhostColor, ImGuiColorEditFlags_NoInputs);

    ImGui::Separator();

    // Challenge action buttons
    Vector3 dronePos = m_CurrentDronePos;
    Vector3 droneFwd = m_CurrentDroneFwd;
    GetLiveDroneTransform(dronePos, droneFwd);

    if (m_State == CHALLENGE_STATE_INACTIVE)
    {
        if (ImGui::Button("Spawn Guide", ImVec2(110, 26)))
        {
            SpawnGhost(dronePos, droneFwd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Start Challenge", ImVec2(130, 26)))
        {
            StartChallenge(dronePos, droneFwd);
        }
    }
    else if (m_State == CHALLENGE_STATE_PREVIEW)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[GUIDE ACTIVE]");
        ImGui::SameLine();
        ImGui::Text(" Alt: %.1fm", m_Altitude);

        if (ImGui::Button("Start Challenge", ImVec2(130, 28)))
        {
            StartChallenge(dronePos, droneFwd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Snap to Drone", ImVec2(110, 28)))
        {
            SpawnGhost(dronePos, droneFwd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(65, 28)))
        {
            Clear();
        }
    }
    else if (m_State == CHALLENGE_STATE_RECORDING)
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "[CHALLENGE IN PROGRESS]");
        ImGui::Text("Time: %.1fs  |  Dist: %.1fm  |  Progress: %.0f%%", m_ChallengeTimer, m_TotalDistanceFlown, m_PathProgressPercent);

        // Real-time live accuracy meter (pure horizontal tracking, independent of altitude)
        if (m_LiveDeviation < 2.0f)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Live Deviation: %.1fm (On Track)", m_LiveDeviation);
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Live Deviation: %.1fm", m_LiveDeviation);
        }
        float accFraction = m_LiveAccuracy / 100.0f;
        ImVec4 barColor = (accFraction > 0.8f) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                          (accFraction > 0.6f) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f) :
                                                 ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(accFraction, ImVec2(-1, 16), "");
        ImGui::PopStyleColor();

        // Subtle Progress Bar along waypoints
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.80f, 0.95f, 1.0f));
        ImGui::ProgressBar(m_PathProgressPercent / 100.0f, ImVec2(-1, 8), "");
        ImGui::PopStyleColor();

        if (ImGui::Button("Finish & Score", ImVec2(130, 28)))
        {
            StopChallenge();
        }
        ImGui::SameLine();
        if (ImGui::Button("Abort", ImVec2(80, 28)))
        {
            Clear();
        }
    }
    else if (m_State == CHALLENGE_STATE_COMPLETED)
    {
        // Results Card
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "=== CHALLENGE RESULTS ===");

        // Big grade display
        ImVec4 gradeCol = (m_LastResult.grade == "S" || m_LastResult.grade == "A") ?
                          ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
                          (m_LastResult.grade == "B") ? ImVec4(0.3f, 0.8f, 1.0f, 1.0f) :
                          ImVec4(1.0f, 0.4f, 0.3f, 1.0f);

        ImGui::TextColored(gradeCol, "Grade: %s  (Score: %.1f / 100)",
                           m_LastResult.grade.c_str(), m_LastResult.finalScore);

        ImGui::Text("Avg Deviation:  %.2f m", m_LastResult.avgDeviation);
        ImGui::Text("Max Deviation:  %.2f m", m_LastResult.maxDeviation);
        ImGui::Text("Closure Error:  %.2f m", m_LastResult.closureError);
        ImGui::Text("Altitude Hold:  %.2f m", m_LastResult.altitudeError);
        ImGui::Text("Flight Time:    %.1f s", m_LastResult.flightTime);

        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_LastResult.feedback.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Fly Again", ImVec2(110, 26)))
        {
            StartChallenge(dronePos, droneFwd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(80, 26)))
        {
            Clear();
        }
    }
}
