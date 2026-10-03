#pragma once

#include "../Framework/Helpers.h" // Vector3, Vector4, Transform
#include <vector>
#include <string>

enum ShapeType
{
    SHAPE_CIRCLE = 0,
    SHAPE_SQUARE,
    SHAPE_RECTANGLE,
    SHAPE_TRIANGLE,
    SHAPE_FIGURE_EIGHT,
    SHAPE_COUNT
};

enum ChallengeState
{
    CHALLENGE_STATE_INACTIVE = 0,
    CHALLENGE_STATE_PREVIEW,       // Ghost shape visible in 3D world
    CHALLENGE_STATE_RECORDING,     // Actively flying & scoring
    CHALLENGE_STATE_COMPLETED      // Finished, showing score and feedback
};

struct ChallengeScoreResult
{
    float finalScore       = 0.0f;  // 0 - 100%
    float avgDeviation     = 0.0f;  // meters
    float maxDeviation     = 0.0f;  // meters
    float closureError     = 0.0f;  // meters (distance between start & end)
    float altitudeError    = 0.0f;  // meters (deviation from target altitude)
    float flightTime       = 0.0f;  // seconds
    float flightDistance   = 0.0f;  // total meters flown
    std::string grade;              // "S", "A", "B", "C", "D"
    std::string feedback;           // Coaching tip
};

class ShapeChallenge
{
public:
    ShapeChallenge();
    ~ShapeChallenge();

    void Init();
    void Reset();

    // Live drone tracking & challenge actions
    bool GetLiveDroneTransform(Vector3& outPos, Vector3& outFwd);
    void UpdateLiveDrone(const Vector3& dronePos, const Vector3& droneFwd);

    void SetShape(ShapeType type);
    void SpawnGhost(const Vector3& dronePos, const Vector3& droneFwd);
    void StartChallenge(const Vector3& dronePos, const Vector3& droneFwd);
    void StopChallenge();
    void Clear();

    // Per-frame calls
    void Update(const Vector3& dronePos, const Vector3& droneFwd, float deltaTime);
    void RenderGhost();
    void RenderUI();

    // Getters / setters
    ChallengeState GetState() const { return m_State; }
    ShapeType GetShapeType()  const { return m_CurrentShape; }
    float GetScale()          const { return m_Scale; }
    void  SetScale(float s);

    const std::vector<Vector3>& GetGhostPoints() const { return m_GhostPoints; }
    const ChallengeScoreResult& GetLastResult()  const { return m_LastResult; }

    bool IsActive()     const { return m_State == CHALLENGE_STATE_RECORDING; }
    bool IsPreviewing() const { return m_State == CHALLENGE_STATE_PREVIEW; }
    bool GetShowGhost() const { return m_ShowGhost; }
    void SetShowGhost(bool show) { m_ShowGhost = show; }

    static const char* GetShapeName(ShapeType type);

private:
    void RegenerateShape();
    void ComputeFinalScore();
    float DistancePointToSegment2D(const Vector3& p, const Vector3& a, const Vector3& b) const;

    // State
    ChallengeState m_State        = CHALLENGE_STATE_PREVIEW;
    ShapeType      m_CurrentShape = SHAPE_CIRCLE;
    float          m_Scale        = 12.0f;       // Size in meters (12m fits indoor & outdoor)
    float          m_Altitude     = 3.5f;        // Target altitude
    Vector3        m_Center       = Vector3(0, 0, 3.5f);
    float          m_HeadingYaw   = 0.0f;        // Orientation
    bool           m_SpawnAhead   = true;        // Place in front of drone vs at drone
    bool           m_ShowGhost    = true;        // Master toggle for ghost line visibility

    // Cached drone position and heading
    Vector3        m_CurrentDronePos = Vector3(0, 0, 0);
    Vector3        m_CurrentDroneFwd = Vector3(1, 0, 0);
    bool           m_HasValidDronePos = false;

    // Ghost path geometry (ideal target shape)
    std::vector<Vector3> m_GhostPoints;
    std::vector<Vector3> m_GhostRenderPos;
    std::vector<Vector4> m_GhostRenderCol;

    // Start/Finish indicator marker (single green X-axis circle)
    std::vector<Vector3> m_MarkerRenderPos;
    std::vector<Vector4> m_MarkerRenderCol;

    // Points flown during the challenge
    std::vector<Vector3> m_ChallengeFlownPoints;
    float                m_ChallengeTimer = 0.0f;
    float                m_TotalDistanceFlown = 0.0f;

    // Live waypoint progress along path (turns green live as user covers it)
    int                  m_ProgressWaypointIndex = 0;
    float                m_PathProgressPercent   = 0.0f;

    // Real-time live stats (horizontal-only accuracy, independent of altitude)
    float                m_LiveDeviation = 0.0f;
    float                m_LiveAccuracy  = 100.0f;
    int                  m_CheckpointsPassed = 0;
    int                  m_TotalCheckpoints  = 4;

    // Result of last run
    ChallengeScoreResult m_LastResult;

    // Visual settings
    float m_GhostColor[3] = {1.0f, 0.85f, 0.15f}; // Golden Amber
    float m_GhostWidth    = 6.0f;
    float m_GhostOpacity  = 0.90f;
    float m_PulseTimer    = 0.0f;
};

extern ShapeChallenge g_ShapeChallenge;
