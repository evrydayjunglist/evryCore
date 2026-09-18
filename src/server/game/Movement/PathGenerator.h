/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PATH_GENERATOR_H
#define _PATH_GENERATOR_H

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MMapDefines.h"
#include "MoveSplineInitArgs.h"
#include <G3D/Vector3.h>
#include <boost/container/small_vector.hpp>

class WorldObject;

namespace MMAP
{
    class MMapManager;
}

// 74*4.0f=296y number_of_points*interval = max_path_len
// this is way more than actual evade range
// I think we can safely cut those down even more
#define MAX_PATH_LENGTH         74
#define MAX_POINT_PATH_LENGTH   74

// Room for a long reach: 1024 points 4 yards apart hold about 4,000 yards of road.
#define LONG_PATH_LENGTH        1024
#define LONG_POINT_PATH_LENGTH  1024

#define SMOOTH_PATH_STEP_SIZE   4.0f
#define SMOOTH_PATH_SLOP        0.3f

#define VERTEX_SIZE       3
#define INVALID_POLYREF   0

enum PathType
{
    PATHFIND_BLANK             = 0x00,   // path not built yet
    PATHFIND_NORMAL            = 0x01,   // normal path
    PATHFIND_SHORTCUT          = 0x02,   // travel through obstacles, terrain, air, etc (old behavior)
    PATHFIND_INCOMPLETE        = 0x04,   // we have partial path to follow - getting closer to target
    PATHFIND_NOPATH            = 0x08,   // no valid path at all or error in generating one
    PATHFIND_NOT_USING_PATH    = 0x10,   // used when we are either flying/swiming or on map w/o mmaps
    PATHFIND_SHORT             = 0x20,   // path is longer or equal to its limited path length
    PATHFIND_FARFROMPOLY_START = 0x40,   // start position is far from the mmap poligon
    PATHFIND_FARFROMPOLY_END   = 0x80,   // end positions is far from the mmap poligon
    PATHFIND_FARFROMPOLY       = PATHFIND_FARFROMPOLY_START | PATHFIND_FARFROMPOLY_END, // start or end positions are far from the mmap poligon
};

// Which set of movement maps a path is built from.
enum class NavMeshChoice : uint8
{
    Creature,       // the set the whole server has always used: ground up to 55 degrees, and a step tall enough to clear a fence
    PlayerBody      // the set built for what a player's body can walk, where one exists; the set above everywhere else
};

// How long a route a path may be.
enum class PathReach : uint8
{
    Short,  // what the server has always used: 1024 search nodes, MAX_PATH_LENGTH polygons and MAX_POINT_PATH_LENGTH points
    Long    // room to walk across a zone: MMAP::LONG_ROUTE_SEARCH_NODES search nodes, LONG_PATH_LENGTH polygons and
            // LONG_POINT_PATH_LENGTH points, in a query of its own so no creature ever shares it
};

// How turning the polygon corridor into path points ended.
enum class PathSmoothingEnd : uint8
{
    NotRun,         // no corridor was turned into points
    ReachedEnd,     // it followed the corridor to its end
    NoSteerTarget,  // it found no corner far enough ahead to steer toward
    CorridorEmpty,  // the corridor ran out under it
    OutOfPoints,    // it filled every point a path can hold before the end
    QueryFailed     // a navmesh query it needed failed
};

// What the navmesh search and the smoothing did on the last CalculatePath. It is there so a caller can say why a route
// came back the way it did; nothing in the path builder reads it.
struct PathSearchReport
{
    // A corridor search ran and gave a corridor. None runs when both ends are on one polygon or when there is no navmesh
    // answer at all.
    bool Searched = false;
    // The search got to the destination's polygon, or both ends are on one polygon.
    bool ReachedDestination = false;
    // The search ran out of search nodes, so its corridor heads for the closest point it had seen when it stopped.
    bool RanOutOfNodes = false;
    // The corridor was longer than the CorridorLimit polygons this path can hold, and only its start was kept.
    bool CorridorCut = false;
    uint32 NodesUsed = 0;
    uint32 NodeLimit = 0;
    uint32 CorridorPolygons = 0;
    uint32 CorridorLimit = 0;
    PathSmoothingEnd SmoothingEnd = PathSmoothingEnd::NotRun;
    uint32 SmoothedPoints = 0;
};

class TC_GAME_API PathGenerator
{
    public:
        explicit PathGenerator(WorldObject const* owner, NavMeshChoice navMeshChoice = NavMeshChoice::Creature, PathReach reach = PathReach::Short);
        ~PathGenerator();

        PathGenerator(PathGenerator const& right) = delete;
        PathGenerator(PathGenerator&& right) = delete;
        PathGenerator& operator=(PathGenerator const& right) = delete;
        PathGenerator& operator=(PathGenerator&& right) = delete;

        // Calculate the path from owner to given destination
        // return: true if new path was calculated, false otherwise (no change needed)
        bool CalculatePath(float srcX, float srcY, float srcZ, float destX, float destY, float destZ, bool forceDest = false);
        bool CalculatePath(float destX, float destY, float destZ, bool forceDest = false);
        bool IsInvalidDestinationZ(WorldObject const* target) const;

        // option setters - use optional
        void SetUseStraightPath(bool useStraightPath) { _useStraightPath = useStraightPath; }
        void SetPathLengthLimit(float distance) { _pointPathLimit = std::min<uint32>(uint32(distance/SMOOTH_PATH_STEP_SIZE), _maxPointPath); }
        void SetUseRaycast(bool useRaycast) { _useRaycast = useRaycast; }

        // result getters
        G3D::Vector3 const& GetStartPosition() const { return _startPosition; }
        G3D::Vector3 const& GetEndPosition() const { return _endPosition; }
        G3D::Vector3 const& GetActualEndPosition() const { return _actualEndPosition; }

        Movement::PointsArray const& GetPath() const { return _pathPoints; }
        float GetPathLength() const;

        PathType GetPathType() const { return _type; }

        // Whether this path came from the set built for a player's body, or fell back to the creature one.
        bool UsedPlayerNavMesh() const { return _usingPlayerNavMesh; }

        PathSearchReport const& GetSearchReport() const { return _searchReport; }

        // shortens the path until the destination is the specified distance from the target point
        void ShortenPathUntilDist(G3D::Vector3 const& target, float dist);

    private:

        // detour polygon references; a short reach keeps them inside the object, a long one needs more than fits there
        boost::container::small_vector<dtPolyRef, MAX_PATH_LENGTH> _pathPolyRefs;
        uint32 _polyLength;                         // number of polygons in the path
        uint32 const _maxPathPolys;                 // how many polygons the corridor may hold

        Movement::PointsArray _pathPoints;  // our actual (x,y,z) path to the target
        PathType _type;                     // tells what kind of path this is

        bool _useStraightPath;  // type of path will be generated
        bool _forceDestination; // when set, we will always arrive at given point
        uint32 const _maxPointPath; // how many points the path may hold
        uint32 _pointPathLimit; // limit point path size; min(this, _maxPointPath)
        bool _useRaycast;       // use raycast if true for a straight line path

        G3D::Vector3 _startPosition;        // {x, y, z} of current location
        G3D::Vector3 _endPosition;          // {x, y, z} of the destination
        G3D::Vector3 _actualEndPosition;    // {x, y, z} of the closest possible point to given destination

        WorldObject const* const _source;       // the object that is moving
        PathReach const _reach;                 // how long a route this path may be
        dtNavMesh const* _navMesh;              // the nav mesh
        dtNavMeshQuery const* _navMeshQuery;    // the nav mesh query used to find the path
        uint32 _meshMapId;                      // the terrain map the meshes above were taken from
        bool _usingPlayerNavMesh;               // the path is being built from the set made for a player's body
        PathSearchReport _searchReport;         // what the search and the smoothing did on the last CalculatePath

        dtQueryFilter _filter;  // use single filter for all movements, update it when needed

        void SetStartPosition(G3D::Vector3 const& point) { _startPosition = point; }
        void SetEndPosition(G3D::Vector3 const& point) { _actualEndPosition = point; _endPosition = point; }
        void SetActualEndPosition(G3D::Vector3 const& point) { _actualEndPosition = point; }
        void NormalizePath();

        void Clear()
        {
            _polyLength = 0;
            _pathPoints.clear();
        }

        bool InRange(G3D::Vector3 const& p1, G3D::Vector3 const& p2, float r, float h) const;
        float Dist3DSqr(G3D::Vector3 const& p1, G3D::Vector3 const& p2) const;
        bool InRangeYZX(float const* v1, float const* v2, float r, float h) const;

        dtPolyRef GetPathPolyByPosition(dtPolyRef const* polyPath, uint32 polyPathSize, float const* Point, float* Distance = nullptr) const;
        dtPolyRef GetPolyByLocation(float const* Point, float* Distance) const;
        bool HaveTile(G3D::Vector3 const& p) const;

        dtNavMeshQuery const* QueryFrom(MMAP::MMapManager* meshes) const;
        void UseCreatureNavMesh();
        bool PlayerNavMeshCarries(G3D::Vector3 const& start, G3D::Vector3 const& dest) const;

        void NoteCorridorSearch(dtStatus status);
        void BuildPolyPath(G3D::Vector3 const& startPos, G3D::Vector3 const& endPos);
        void BuildPointPath(float const* startPoint, float const* endPoint);
        void BuildShortcut();

        NavTerrainFlag GetNavTerrain(float x, float y, float z) const;
        void CreateFilter();
        void UpdateFilter();

        // smooth path aux functions
        uint32 FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath, dtPolyRef const* visited, uint32 nvisited);
        bool GetSteerTarget(float const* startPos, float const* endPos, float minTargetDist, dtPolyRef const* path, uint32 pathSize, float* steerPos,
                            unsigned char& steerPosFlag, dtPolyRef& steerPosRef);
        dtStatus FindSmoothPath(float const* startPos, float const* endPos,
                              dtPolyRef const* polyPath, uint32 polyPathSize,
                              float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize);

        void AddFarFromPolyFlags(bool startFarFromPoly, bool endFarFromPoly);
};

#endif
