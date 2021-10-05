/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "FootpathPlaceAction.h"

#include "../Cheats.h"
#include "../OpenRCT2.h"
#include "../core/MemoryStream.h"
#include "../interface/Window.h"
#include "../localisation/StringIds.h"
#include "../management/Finance.h"
#include "../world/Footpath.h"
#include "../world/Location.hpp"
#include "../world/Park.h"
#include "../world/Scenery.h"
#include "../world/Surface.h"
#include "../world/TileElementsView.h"
#include "../world/Wall.h"

using namespace OpenRCT2;

FootpathPlaceAction::FootpathPlaceAction(
    const CoordsXYZ& loc, uint8_t slope, ObjectEntryIndex type, ObjectEntryIndex railingsType, Direction direction,
    PathConstructFlags constructFlags)
    : _loc(loc)
    , _slope(slope)
    , _type(type)
    , _railingsType(railingsType)
    , _direction(direction)
    , _constructFlags(constructFlags)
{
}

void FootpathPlaceAction::AcceptParameters(GameActionParameterVisitor& visitor)
{
    visitor.Visit(_loc);
    visitor.Visit("object", _type);
    visitor.Visit("railingsObject", _railingsType);
    visitor.Visit("direction", _direction);
    visitor.Visit("slope", _slope);
    visitor.Visit("constructFlags", _constructFlags);
}

uint16_t FootpathPlaceAction::GetActionFlags() const
{
    return GameAction::GetActionFlags();
}

void FootpathPlaceAction::Serialise(DataSerialiser& stream)
{
    GameAction::Serialise(stream);

    stream << DS_TAG(_loc) << DS_TAG(_slope) << DS_TAG(_type) << DS_TAG(_railingsType) << DS_TAG(_direction)
           << DS_TAG(_constructFlags);
}

GameActions::Result::Ptr FootpathPlaceAction::Query() const
{
    GameActions::Result::Ptr res = std::make_unique<GameActions::Result>();
    res->Cost = 0;
    res->Expenditure = ExpenditureType::Landscaping;
    res->Position = _loc.ToTileCentre();

    gFootpathGroundFlags = 0;

    if (!LocationValid(_loc) || map_is_edge(_loc))
    {
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_FOOTPATH_HERE, STR_OFF_EDGE_OF_MAP);
    }

    if (!((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) || gCheatsSandboxMode) && !map_is_location_owned(_loc))
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_FOOTPATH_HERE, STR_LAND_NOT_OWNED_BY_PARK);
    }

    if (_slope & SLOPE_IS_IRREGULAR_FLAG)
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_FOOTPATH_HERE, STR_LAND_SLOPE_UNSUITABLE);
    }

    if (_loc.z < FootpathMinHeight)
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_FOOTPATH_HERE, STR_TOO_LOW);
    }

    if (_loc.z > FootpathMaxHeight)
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_FOOTPATH_HERE, STR_TOO_HIGH);
    }

    if (_direction != INVALID_DIRECTION && !direction_valid(_direction))
    {
        log_error("Direction invalid. direction = %u", _direction);
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_FOOTPATH_HERE);
    }

    footpath_provisional_remove();
    auto tileElement = map_get_footpath_element_slope(_loc, _slope);
    if (tileElement == nullptr)
    {
        return ElementInsertQuery(std::move(res));
    }
    return ElementUpdateQuery(tileElement, std::move(res));
}

GameActions::Result::Ptr FootpathPlaceAction::Execute() const
{
    GameActions::Result::Ptr res = std::make_unique<GameActions::Result>();
    res->Cost = 0;
    res->Expenditure = ExpenditureType::Landscaping;
    res->Position = _loc.ToTileCentre();

    if (!(GetFlags() & GAME_COMMAND_FLAG_GHOST))
    {
        footpath_interrupt_peeps(_loc);
    }

    gFootpathGroundFlags = 0;

    // Force ride construction to recheck area
    _currentTrackSelectionFlags |= TRACK_SELECTION_FLAG_RECHECK;

    if (!(GetFlags() & GAME_COMMAND_FLAG_GHOST))
    {
        if (_direction != INVALID_DIRECTION && !gCheatsDisableClearanceChecks)
        {
            // It is possible, let's remove walls between the old and new piece of path
            auto zLow = _loc.z;
            auto zHigh = zLow + PATH_CLEARANCE;
            wall_remove_intersecting_walls(
                { _loc, zLow, zHigh + ((_slope & TILE_ELEMENT_SURFACE_RAISED_CORNERS_MASK) ? 16 : 0) },
                direction_reverse(_direction));
            wall_remove_intersecting_walls(
                { _loc.x - CoordsDirectionDelta[_direction].x, _loc.y - CoordsDirectionDelta[_direction].y, zLow, zHigh },
                _direction);
        }
    }

    auto tileElement = map_get_footpath_element_slope(_loc, _slope);
    if (tileElement == nullptr)
    {
        return ElementInsertExecute(std::move(res));
    }
    return ElementUpdateExecute(tileElement, std::move(res));
}

bool FootpathPlaceAction::IsSameAsPathElement(const PathElement* pathElement) const
{
    // Check if both this action and the element is queue
    if (pathElement->IsQueue() != ((_constructFlags & PathConstructFlag::IsQueue) != 0))
        return false;

    auto footpathObj = pathElement->GetLegacyPathEntry();
    if (footpathObj == nullptr)
    {
        if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
        {
            return false;
        }

        return pathElement->GetSurfaceEntryIndex() == _type && pathElement->GetRailingsEntryIndex() == _railingsType;
    }

    if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
    {
        return pathElement->GetLegacyPathEntryIndex() == _type;
    }

    return false;
}

bool FootpathPlaceAction::IsSameAsEntranceElement(const EntranceElement& entranceElement) const
{
    if (entranceElement.HasLegacyPathEntry())
    {
        if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
        {
            return entranceElement.GetLegacyPathEntryIndex() == _type;
        }

        return false;
    }

    if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
    {
        return false;
    }

    return entranceElement.GetSurfaceEntryIndex() == _type;
}

GameActions::Result::Ptr FootpathPlaceAction::ElementUpdateQuery(PathElement* pathElement, GameActions::Result::Ptr res) const
{
    if (!IsSameAsPathElement(pathElement))
    {
        res->Cost += MONEY(6, 00);
    }

    if (GetFlags() & GAME_COMMAND_FLAG_GHOST && !pathElement->IsGhost())
    {
        return MakeResult(GameActions::Status::Unknown, STR_CANT_BUILD_FOOTPATH_HERE);
    }
    return res;
}

GameActions::Result::Ptr FootpathPlaceAction::ElementUpdateExecute(PathElement* pathElement, GameActions::Result::Ptr res) const
{
    if (!IsSameAsPathElement(pathElement))
    {
        res->Cost += MONEY(6, 00);
    }

    footpath_queue_chain_reset();

    if (!(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY))
    {
        footpath_remove_edges_at(_loc, reinterpret_cast<TileElement*>(pathElement));
    }

    if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
    {
        pathElement->SetLegacyPathEntryIndex(_type);
    }
    else
    {
        pathElement->SetSurfaceEntryIndex(_type);
        pathElement->SetRailingsEntryIndex(_railingsType);
    }

    pathElement->SetIsQueue((_constructFlags & PathConstructFlag::IsQueue) != 0);

    auto* elem = pathElement->GetAdditionEntry();
    if (elem != nullptr)
    {
        if (_constructFlags & PathConstructFlag::IsQueue)
        {
            // remove any addition that isn't a TV or a lamp
            if ((elem->flags & PATH_BIT_FLAG_IS_QUEUE_SCREEN) == 0 && (elem->flags & PATH_BIT_FLAG_LAMP) == 0)
            {
                pathElement->SetIsBroken(false);
                pathElement->SetAddition(0);
            }
        }
        else
        {
            // remove all TVs
            if ((elem->flags & PATH_BIT_FLAG_IS_QUEUE_SCREEN) != 0)
            {
                pathElement->SetIsBroken(false);
                pathElement->SetAddition(0);
            }
        }
    }

    RemoveIntersectingWalls(pathElement);
    return res;
}

GameActions::Result::Ptr FootpathPlaceAction::ElementInsertQuery(GameActions::Result::Ptr res) const
{
    bool entrancePath = false, entranceIsSamePath = false;

    if (!MapCheckCapacityAndReorganise(_loc))
    {
        return MakeResult(GameActions::Status::NoFreeElements, STR_CANT_BUILD_FOOTPATH_HERE);
    }

    res->Cost = MONEY(12, 00);

    QuarterTile quarterTile{ 0b1111, 0 };
    auto zLow = _loc.z;
    auto zHigh = zLow + PATH_CLEARANCE;
    if (_slope & FOOTPATH_PROPERTIES_FLAG_IS_SLOPED)
    {
        quarterTile = QuarterTile{ 0b1111, 0b1100 }.Rotate(_slope & TILE_ELEMENT_DIRECTION_MASK);
        zHigh += PATH_HEIGHT_STEP;
    }

    auto entranceElement = map_get_park_entrance_element_at(_loc, false);
    // Make sure the entrance part is the middle
    if (entranceElement != nullptr && (entranceElement->GetSequenceIndex()) == 0)
    {
        entrancePath = true;
        // Make the price the same as replacing a path
        if (IsSameAsEntranceElement(*entranceElement))
            entranceIsSamePath = true;
        else
            res->Cost -= MONEY(6, 00);
    }

    // Do not attempt to build a crossing with a queue or a sloped path.
    auto isQueue = _constructFlags & PathConstructFlag::IsQueue;
    uint8_t crossingMode = isQueue || (_slope != TILE_ELEMENT_SLOPE_FLAT) ? CREATE_CROSSING_MODE_NONE
                                                                          : CREATE_CROSSING_MODE_PATH_OVER_TRACK;
    auto canBuild = MapCanConstructWithClearAt(
        { _loc, zLow, zHigh }, &map_place_non_scenery_clear_func, quarterTile, GetFlags(), crossingMode);
    if (!entrancePath && canBuild->Error != GameActions::Status::Ok)
    {
        canBuild->ErrorTitle = STR_CANT_BUILD_FOOTPATH_HERE;
        return canBuild;
    }
    res->Cost += canBuild->Cost;

    gFootpathGroundFlags = canBuild->GroundFlags;
    if (!gCheatsDisableClearanceChecks && (canBuild->GroundFlags & ELEMENT_IS_UNDERWATER))
    {
        return MakeResult(GameActions::Status::Disallowed, STR_CANT_BUILD_FOOTPATH_HERE, STR_CANT_BUILD_THIS_UNDERWATER);
    }

    auto surfaceElement = map_get_surface_element_at(_loc);
    if (surfaceElement == nullptr)
    {
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_FOOTPATH_HERE);
    }
    int32_t supportHeight = zLow - surfaceElement->GetBaseZ();
    res->Cost += supportHeight < 0 ? MONEY(20, 00) : (supportHeight / PATH_HEIGHT_STEP) * MONEY(5, 00);

    // Prevent the place sound from being spammed
    if (entranceIsSamePath)
        res->Cost = 0;

    return res;
}

GameActions::Result::Ptr FootpathPlaceAction::ElementInsertExecute(GameActions::Result::Ptr res) const
{
    bool entrancePath = false, entranceIsSamePath = false;

    if (!(GetFlags() & (GAME_COMMAND_FLAG_ALLOW_DURING_PAUSED | GAME_COMMAND_FLAG_GHOST)))
    {
        footpath_remove_litter(_loc);
    }

    res->Cost = MONEY(12, 00);

    QuarterTile quarterTile{ 0b1111, 0 };
    auto zLow = _loc.z;
    auto zHigh = zLow + PATH_CLEARANCE;
    if (_slope & FOOTPATH_PROPERTIES_FLAG_IS_SLOPED)
    {
        quarterTile = QuarterTile{ 0b1111, 0b1100 }.Rotate(_slope & TILE_ELEMENT_DIRECTION_MASK);
        zHigh += PATH_HEIGHT_STEP;
    }

    auto entranceElement = map_get_park_entrance_element_at(_loc, false);
    // Make sure the entrance part is the middle
    if (entranceElement != nullptr && (entranceElement->GetSequenceIndex()) == 0)
    {
        entrancePath = true;
        // Make the price the same as replacing a path
        if (IsSameAsEntranceElement(*entranceElement))
            entranceIsSamePath = true;
        else
            res->Cost -= MONEY(6, 00);
    }

    // Do not attempt to build a crossing with a queue or a sloped.
    auto isQueue = _constructFlags & PathConstructFlag::IsQueue;
    uint8_t crossingMode = isQueue || (_slope != TILE_ELEMENT_SLOPE_FLAT) ? CREATE_CROSSING_MODE_NONE
                                                                          : CREATE_CROSSING_MODE_PATH_OVER_TRACK;
    auto canBuild = MapCanConstructWithClearAt(
        { _loc, zLow, zHigh }, &map_place_non_scenery_clear_func, quarterTile, GAME_COMMAND_FLAG_APPLY | GetFlags(),
        crossingMode);
    if (!entrancePath && canBuild->Error != GameActions::Status::Ok)
    {
        canBuild->ErrorTitle = STR_CANT_BUILD_FOOTPATH_HERE;
        return canBuild;
    }
    res->Cost += canBuild->Cost;

    gFootpathGroundFlags = canBuild->GroundFlags;

    auto surfaceElement = map_get_surface_element_at(_loc);
    if (surfaceElement == nullptr)
    {
        return MakeResult(GameActions::Status::InvalidParameters, STR_CANT_BUILD_FOOTPATH_HERE);
    }
    int32_t supportHeight = zLow - surfaceElement->GetBaseZ();
    res->Cost += supportHeight < 0 ? MONEY(20, 00) : (supportHeight / PATH_HEIGHT_STEP) * MONEY(5, 00);

    if (entrancePath)
    {
        if (!(GetFlags() & GAME_COMMAND_FLAG_GHOST) && !entranceIsSamePath)
        {
            if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
            {
                entranceElement->SetLegacyPathEntryIndex(_type);
            }
            else
            {
                entranceElement->SetSurfaceEntryIndex(_type);
            }
            map_invalidate_tile_full(_loc);
        }
    }
    else
    {
        auto* pathElement = TileElementInsert<PathElement>(_loc, 0b1111);
        Guard::Assert(pathElement != nullptr);

        pathElement->SetClearanceZ(zHigh);
        if (_constructFlags & PathConstructFlag::IsLegacyPathObject)
        {
            pathElement->SetLegacyPathEntryIndex(_type);
        }
        else
        {
            pathElement->SetSurfaceEntryIndex(_type);
            pathElement->SetRailingsEntryIndex(_railingsType);
        }
        pathElement->SetSlopeDirection(_slope & FOOTPATH_PROPERTIES_SLOPE_DIRECTION_MASK);
        pathElement->SetSloped(_slope & FOOTPATH_PROPERTIES_FLAG_IS_SLOPED);
        pathElement->SetIsQueue(isQueue);
        pathElement->SetAddition(0);
        pathElement->SetRideIndex(RIDE_ID_NULL);
        pathElement->SetAdditionStatus(255);
        pathElement->SetIsBroken(false);
        pathElement->SetGhost(GetFlags() & GAME_COMMAND_FLAG_GHOST);

        footpath_queue_chain_reset();

        if (!(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY))
        {
            footpath_remove_edges_at(_loc, pathElement->as<TileElement>());
        }
        if ((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !(GetFlags() & GAME_COMMAND_FLAG_GHOST))
        {
            AutomaticallySetPeepSpawn();
        }

        RemoveIntersectingWalls(pathElement);
    }

    // Prevent the place sound from being spammed
    if (entranceIsSamePath)
        res->Cost = 0;

    return res;
}

/**
 *
 *  rct2: 0x006A65AD
 */
void FootpathPlaceAction::AutomaticallySetPeepSpawn() const
{
    uint8_t direction = 0;
    if (_loc.x != 32)
    {
        direction++;
        if (_loc.y != GetMapSizeUnits() - 32)
        {
            direction++;
            if (_loc.x != GetMapSizeUnits() - 32)
            {
                direction++;
                if (_loc.y != 32)
                    return;
            }
        }
    }

    if (gPeepSpawns.empty())
    {
        gPeepSpawns.emplace_back();
    }
    PeepSpawn* peepSpawn = &gPeepSpawns[0];
    peepSpawn->x = _loc.x + (DirectionOffsets[direction].x * 15) + 16;
    peepSpawn->y = _loc.y + (DirectionOffsets[direction].y * 15) + 16;
    peepSpawn->direction = direction;
    peepSpawn->z = _loc.z;
}

void FootpathPlaceAction::RemoveIntersectingWalls(PathElement* pathElement) const
{
    if (pathElement->IsSloped() && !(GetFlags() & GAME_COMMAND_FLAG_GHOST))
    {
        auto direction = pathElement->GetSlopeDirection();
        int32_t z = pathElement->GetBaseZ();
        wall_remove_intersecting_walls({ _loc, z, z + (6 * COORDS_Z_STEP) }, direction_reverse(direction));
        wall_remove_intersecting_walls({ _loc, z, z + (6 * COORDS_Z_STEP) }, direction);
        // Removing walls may have made the pointer invalid, so find it again
        auto tileElement = map_get_footpath_element(CoordsXYZ(_loc, z));
        if (tileElement == nullptr)
        {
            log_error("Something went wrong. Could not refind footpath.");
            return;
        }
        pathElement = tileElement->AsPath();
    }

    if (!(GetFlags() & GAME_COMMAND_FLAG_PATH_SCENERY))
        footpath_connect_edges(_loc, reinterpret_cast<TileElement*>(pathElement), GetFlags());

    footpath_update_queue_chains();
    map_invalidate_tile_full(_loc);
}

PathElement* FootpathPlaceAction::map_get_footpath_element_slope(const CoordsXYZ& footpathPos, int32_t slope) const
{
    const bool isSloped = slope & FOOTPATH_PROPERTIES_FLAG_IS_SLOPED;
    const auto slopeDirection = slope & FOOTPATH_PROPERTIES_SLOPE_DIRECTION_MASK;

    for (auto* pathElement : TileElementsView<PathElement>(footpathPos))
    {
        if (pathElement->GetBaseZ() != footpathPos.z)
            continue;
        if (pathElement->IsSloped() != isSloped)
            continue;
        if (pathElement->GetSlopeDirection() != slopeDirection)
            continue;
        return pathElement;
    }

    return nullptr;
}
