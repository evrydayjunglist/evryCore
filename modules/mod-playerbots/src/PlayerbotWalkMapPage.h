/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef EVRY_MOD_PLAYERBOT_WALK_MAP_PAGE_H
#define EVRY_MOD_PLAYERBOT_WALK_MAP_PAGE_H

#include "PlayerbotWalkMap.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// What the page says about the map besides the map itself.
struct PlayerbotWalkMapReport
{
    std::string Name;
    // Area and zone, when known.
    std::string Place;
    std::string Made;
    std::uint32_t MapId = 0;
    bool HasDestination = false;
    std::array<float, 3> Destination = { 0.0f, 0.0f, 0.0f };
    // The navmesh route from her feet toward that destination, and its path type flags.
    std::vector<std::array<float, 3>> Route;
    std::uint32_t RouteType = 0;
    float Seconds = 0.0f;
    float WorkSeconds = 0.0f;
};

template <typename... Args>
std::string PlayerbotWalkMapText(char const* format, Args... args)
{
    int const size = std::snprintf(nullptr, 0, format, args...);
    if (size <= 0)
        return {};

    std::string text(std::size_t(size), '\0');
    std::snprintf(text.data(), text.size() + 1, format, args...);
    return text;
}

// What PathGenerator's path type flags say about a route, in words.
inline std::string PlayerbotWalkMapRouteWords(std::uint32_t type)
{
    std::string words;
    if (type & 0x08)
        words = "the navmesh found no route";
    else if (type & 0x10)
        words = "the navmesh was not used";
    else if (type & 0x02)
        words = "the navmesh gave a straight line, not a route";
    else if (type & 0x04)
        words = "the navmesh found only part of the way";
    else if (type & 0x01)
        words = "the navmesh found the whole way";
    else
        words = "the navmesh built no route";

    if (type & 0x20)
        words += "; the route was cut at the navmesh's length limit";
    if (type & 0x40)
        words += "; her feet are far from the navmesh";
    if (type & 0x80)
        words += "; the destination is far from the navmesh";

    return PlayerbotWalkMapText("%s (path type 0x%02X)", words.c_str(), unsigned(type));
}

// Plain sentences for the top of the page, the chat reply, and the log.
inline std::vector<std::string> DescribePlayerbotWalkMap(PlayerbotWalkMap const& map, PlayerbotWalkMapSummary const& summary,
    PlayerbotWalkMapReport const& report)
{
    PlayerbotWalkMapSettings const& settings = map.Settings();
    std::vector<std::string> lines;

    std::string const place = report.Place.empty() ? std::string() : " in " + report.Place;
    lines.push_back(PlayerbotWalkMapText(
        "%s stood at (%.1f, %.1f, %.1f)%s on map %u. This map covers %.0f yards around those feet in steps of %.2f yards, one walk heartbeat at her run speed, judged by her walk's rules: climb at most %.0f degrees in a step, step down at most %.0f yards, and no wall at chest height.",
        report.Name.c_str(), settings.OriginX, settings.OriginY, settings.OriginZ, place.c_str(), unsigned(report.MapId),
        settings.Radius, settings.Spacing, settings.MaxClimbDegrees, settings.MaxDropYards));

    lines.push_back(PlayerbotWalkMapText(
        "She can walk to %zu floor spots, about %.0f square yards. From %zu of them she can walk back to her feet; from %zu she cannot.",
        summary.Reached, float(summary.Reached) * settings.Spacing * settings.Spacing, summary.TwoWay, summary.OneWayOut));

    if (summary.ReachedEdge)
        lines.push_back(PlayerbotWalkMapText(
            "Her walkable ground reaches the edge of this map at %zu spots, and from %zu of those she can walk back.",
            summary.ReachedEdge, summary.TwoWayEdge));
    else
        lines.push_back("Her walkable ground does not reach the edge of this map.");

    struct BorderWords
    {
        PlayerbotWalkMapStep Step;
        char const* Words;
    };
    static constexpr BorderWords const borderWords[] =
    {
        { PlayerbotWalkMapStep::NoFloor, "no floor within her climb" },
        { PlayerbotWalkMapStep::SteepUp, "steeper than her climb" },
        { PlayerbotWalkMapStep::TooFarDown, "dropping too far" },
        { PlayerbotWalkMapStep::StaticCollision, "into a wall" },
        { PlayerbotWalkMapStep::DynamicCollision, "into a game object" },
        { PlayerbotWalkMapStep::InvalidPosition, "to an invalid position" }
    };
    std::string border;
    for (BorderWords const& entry : borderWords)
    {
        std::size_t const steps = summary.BorderSteps[std::size_t(entry.Step)];
        if (!steps)
            continue;
        if (!border.empty())
            border += ", ";
        border += PlayerbotWalkMapText("%zu %s", steps, entry.Words);
    }
    if (!border.empty())
        lines.push_back("Steps from her walkable ground onto spots she cannot walk to were refused: " + border + ".");

    if (summary.OneWayIn)
        lines.push_back(PlayerbotWalkMapText(
            "There are %zu floor spots she cannot walk to from which she could walk to her feet.", summary.OneWayIn));

    if (summary.NotLoadedSteps)
        lines.push_back(PlayerbotWalkMapText(
            "%zu steps went into ground that is not loaded, so the map knows nothing past them.", summary.NotLoadedSteps));

    if (map.HitSpotLimit())
        lines.push_back(PlayerbotWalkMapText("The map stopped at its limit of %zu floor spots, so it is not complete.",
            settings.MaxSpots));

    if (report.HasDestination)
    {
        float const dx = report.Destination[0] - settings.OriginX;
        float const dy = report.Destination[1] - settings.OriginY;
        lines.push_back(PlayerbotWalkMapText(
            "The dashed line is the navmesh route from her feet toward her last walk destination (%.1f, %.1f, %.1f), %.0f yards away: %s.",
            report.Destination[0], report.Destination[1], report.Destination[2], std::sqrt(dx * dx + dy * dy),
            PlayerbotWalkMapRouteWords(report.RouteType).c_str()));
    }
    else
        lines.push_back("No walk destination is on record for her, so no route is drawn.");

    lines.push_back(PlayerbotWalkMapText("Made in %.1f seconds, %.1f of them spent on the world thread.",
        report.Seconds, report.WorkSeconds));
    return lines;
}

inline void PlayerbotWalkMapAppendJsonString(std::string& out, std::string_view text)
{
    out += '"';
    for (char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            // The data sits inside a script element, so nothing in it may close that element.
            case '<':
                out += "\\u003c";
                break;
            case '>':
                out += "\\u003e";
                break;
            case '&':
                out += "\\u0026";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out += PlayerbotWalkMapText("\\u%04x", unsigned(static_cast<unsigned char>(c)));
                else
                    out += c;
                break;
        }
    }
    out += '"';
}

inline void PlayerbotWalkMapAppendInt(std::string& out, long long value)
{
    char buffer[24];
    std::to_chars_result const result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

inline void PlayerbotWalkMapAppendFloat(std::string& out, float value, int decimals)
{
    if (!std::isfinite(value))
    {
        out += '0';
        return;
    }

    char buffer[48];
    std::to_chars_result const result = std::to_chars(buffer, buffer + sizeof(buffer), double(value), std::chars_format::fixed, decimals);
    out.append(buffer, result.ptr);
}

inline void PlayerbotWalkMapAppendPoint(std::string& out, float x, float y, float z)
{
    out += '[';
    PlayerbotWalkMapAppendFloat(out, x, 2);
    out += ',';
    PlayerbotWalkMapAppendFloat(out, y, 2);
    out += ',';
    PlayerbotWalkMapAppendFloat(out, z, 2);
    out += ']';
}

// The data the page draws, as one JSON object.
inline std::string PlayerbotWalkMapData(PlayerbotWalkMap const& map, PlayerbotWalkMapReport const& report,
    std::vector<std::string> const& lines)
{
    PlayerbotWalkMapSettings const& settings = map.Settings();
    std::vector<PlayerbotWalkMapSpot> const& spots = map.Spots();
    std::string out;
    out.reserve(spots.size() * 40 + 8192);

    out += "{\"name\":";
    PlayerbotWalkMapAppendJsonString(out, report.Name);
    out += ",\"place\":";
    PlayerbotWalkMapAppendJsonString(out, report.Place);
    out += ",\"made\":";
    PlayerbotWalkMapAppendJsonString(out, report.Made);
    out += ",\"map\":";
    PlayerbotWalkMapAppendInt(out, report.MapId);
    out += ",\"start\":";
    PlayerbotWalkMapAppendPoint(out, settings.OriginX, settings.OriginY, settings.OriginZ);
    out += ",\"spacing\":";
    PlayerbotWalkMapAppendFloat(out, settings.Spacing, 4);
    out += ",\"radius\":";
    PlayerbotWalkMapAppendFloat(out, settings.Radius, 1);
    out += ",\"half\":";
    PlayerbotWalkMapAppendInt(out, map.HalfWidth());
    out += ",\"climb\":";
    PlayerbotWalkMapAppendFloat(out, settings.MaxClimbDegrees, 1);
    out += ",\"drop\":";
    PlayerbotWalkMapAppendFloat(out, settings.MaxDropYards, 1);

    out += ",\"summary\":[";
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        if (line)
            out += ',';
        PlayerbotWalkMapAppendJsonString(out, lines[line]);
    }

    out += "],\"i\":[";
    for (std::size_t spot = 0; spot < spots.size(); ++spot)
    {
        if (spot)
            out += ',';
        PlayerbotWalkMapAppendInt(out, spots[spot].I);
    }

    out += "],\"j\":[";
    for (std::size_t spot = 0; spot < spots.size(); ++spot)
    {
        if (spot)
            out += ',';
        PlayerbotWalkMapAppendInt(out, spots[spot].J);
    }

    // Heights in hundredths of a yard.
    out += "],\"z\":[";
    for (std::size_t spot = 0; spot < spots.size(); ++spot)
    {
        if (spot)
            out += ',';
        PlayerbotWalkMapAppendInt(out, std::isfinite(spots[spot].Z) ? std::llround(double(spots[spot].Z) * 100.0) : 0);
    }

    // One digit per floor: 1 she can walk there, 2 she can walk from there to her feet, 3 both, 0 neither.
    out += "],\"k\":\"";
    for (PlayerbotWalkMapSpot const& spot : spots)
        out += char('0' + (spot.Reached ? 1 : 0) + (spot.Returns ? 2 : 0));

    // Eight step letters per floor, in the order of PLAYERBOT_WALK_MAP_DIRECTIONS.
    out += "\",\"s\":\"";
    for (PlayerbotWalkMapSpot const& spot : spots)
        for (PlayerbotWalkMapStep step : spot.Steps)
            out += PlayerbotWalkMapStepLetter(step);

    // For refused steps out of floors she can walk to that still found a floor: floor, direction, and the floor found.
    out += "\",\"t\":[";
    bool firstTarget = true;
    for (std::size_t spot = 0; spot < spots.size(); ++spot)
    {
        if (!spots[spot].Reached)
            continue;

        for (std::size_t d = 0; d < spots[spot].Steps.size(); ++d)
        {
            PlayerbotWalkMapStep const step = spots[spot].Steps[d];
            if (step == PlayerbotWalkMapStep::Legal || !PlayerbotWalkMapStepPlanted(step) || spots[spot].StepSpot[d] < 0)
                continue;

            if (!firstTarget)
                out += ',';
            firstTarget = false;
            PlayerbotWalkMapAppendInt(out, static_cast<long long>(spot));
            out += ',';
            PlayerbotWalkMapAppendInt(out, static_cast<long long>(d));
            out += ',';
            PlayerbotWalkMapAppendInt(out, spots[spot].StepSpot[d]);
        }
    }

    out += "],\"route\":";
    if (report.HasDestination)
    {
        out += "{\"type\":";
        PlayerbotWalkMapAppendInt(out, report.RouteType);
        out += ",\"to\":";
        PlayerbotWalkMapAppendPoint(out, report.Destination[0], report.Destination[1], report.Destination[2]);
        out += ",\"points\":[";
        for (std::size_t point = 0; point < report.Route.size(); ++point)
        {
            if (point)
                out += ',';
            PlayerbotWalkMapAppendPoint(out, report.Route[point][0], report.Route[point][1], report.Route[point][2]);
        }
        out += "]}";
    }
    else
        out += "null";

    out += '}';
    return out;
}

// The page is split into pieces because one string literal may not be too long for the compiler.
inline constexpr std::string_view PLAYERBOT_WALK_MAP_PAGE_HEAD = R"page(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Walk map</title>
<style>
:root {
  color-scheme: dark;
  --bg: #111418; --panel: #191d23; --text: #e7eaee; --muted: #9ba5b0; --line: #2a3038; --ground: #0a0c0f;
  --twoway: #3fb468; --oneout: #e6a23c; --onein: #4b8fe0; --seen: #3b4149;
  --nofloor: #e5484d; --steep: #f472b6; --drop: #a78bfa; --wall: #f1f3f5; --object: #22c3c9;
  --edge: #ffd84d; --unloaded: #5c636c; --route: #ffffff; --feet: #ffffff; --halo: #000000;
}
@media (prefers-color-scheme: light) {
  :root {
    color-scheme: light;
    --bg: #f4f5f7; --panel: #ffffff; --text: #1b1f24; --muted: #5d6874; --line: #d8dde3; --ground: #e4e7eb;
    --seen: #c3c8cf; --wall: #1b1f24; --unloaded: #9aa1a9; --route: #111418; --feet: #111418; --halo: #ffffff;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 14px/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
header { padding: 20px 24px 0; }
h1 { margin: 0; font-size: 22px; font-weight: 650; letter-spacing: -0.01em; }
.sub { color: var(--muted); margin-top: 2px; }
.summary { margin: 14px 24px 0; padding: 12px 18px 12px 34px; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; max-width: 1180px; }
.summary li { margin: 3px 0; }
.layout { display: flex; flex-wrap: wrap; gap: 16px; padding: 16px 24px 32px; align-items: flex-start; }
.mapcol { flex: 1 1 560px; min-width: 0; }
.toolbar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 8px; }
.seg, .zoom { display: inline-flex; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; }
.seg button, .zoom button { background: var(--panel); color: var(--text); border: 0; padding: 6px 12px; font: inherit; cursor: pointer; }
.seg button + button, .zoom button + button { border-left: 1px solid var(--line); }
.seg button[aria-pressed="true"] { background: var(--text); color: var(--bg); }
.zoom button { min-width: 36px; }
.check { display: inline-flex; gap: 6px; align-items: center; color: var(--muted); margin-left: 4px; }
.viewnote { color: var(--muted); margin: 0 0 8px; }
.mapwrap { position: relative; overflow: auto; max-height: 82vh; border: 1px solid var(--line); border-radius: 10px; background: var(--bg); }
.mapwrap canvas { display: block; }
#overlay { position: absolute; left: 0; top: 0; pointer-events: none; }
.foot { color: var(--muted); margin: 8px 0 0; }
.side { flex: 0 1 360px; min-width: 260px; display: flex; flex-direction: column; gap: 16px; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; }
.card h2 { margin: 0 0 8px; font-size: 12px; text-transform: uppercase; letter-spacing: 0.06em; color: var(--muted); font-weight: 600; }
.legend { list-style: none; margin: 0; padding: 0; }
.legend li { display: flex; gap: 10px; align-items: flex-start; margin: 6px 0; }
.sw { flex: 0 0 14px; height: 14px; border-radius: 3px; margin-top: 3px; }
.sw.outline { border: 2px solid var(--edge); }
.sw.line { height: 0; border-top: 3px dashed var(--route); margin-top: 10px; border-radius: 0; }
.sw.ring { border-radius: 50%; border: 2px solid var(--feet); }
#info p { margin: 0 0 4px; }
#info .floor { border-top: 1px solid var(--line); padding-top: 8px; margin-top: 8px; }
#info ul { margin: 4px 0 0; padding-left: 18px; color: var(--muted); }
.muted { color: var(--muted); }
@media (max-width: 700px) {
  header { padding: 16px 16px 0; }
  .summary { margin: 12px 16px 0; }
  .layout { padding: 12px 16px 24px; }
}
</style>
</head>
<body>
<header>
  <h1 id="title">Walk map</h1>
  <div class="sub" id="subtitle"></div>
</header>
<ul class="summary" id="summary"></ul>
<div class="layout">
  <div class="mapcol">
    <div class="toolbar">
      <div class="seg" role="group" aria-label="View">
        <button type="button" data-view="stops" aria-pressed="true">What stops her</button>
        <button type="button" data-view="ways" aria-pressed="false">One-way ground</button>
        <button type="button" data-view="height" aria-pressed="false">Height</button>
      </div>
      <div class="zoom" role="group" aria-label="Zoom">
        <button type="button" id="zoomOut" aria-label="Zoom out">&minus;</button>
        <button type="button" id="zoomIn" aria-label="Zoom in">+</button>
      </div>
      <label class="check"><input type="checkbox" id="route" checked> Route</label>
    </div>
    <p class="viewnote" id="viewnote"></p>
    <div class="mapwrap" id="mapwrap"><canvas id="map"></canvas><canvas id="overlay"></canvas></div>
    <p class="foot">North is up. Each square is one spot; she steps between neighbouring spots, straight or diagonal.</p>
  </div>
  <div class="side">
    <section class="card"><h2>Spot</h2><div id="info"><p class="muted">Point at the map to see a spot.</p></div></section>
    <section class="card"><h2>Key</h2><ul class="legend" id="legend"></ul></section>
  </div>
</div>
<script type="application/json" id="walkmap-data">)page";

inline constexpr std::string_view PLAYERBOT_WALK_MAP_PAGE_SCRIPT = R"page(</script>
<script>
(() => {
  'use strict';
  const D = JSON.parse(document.getElementById('walkmap-data').textContent);
  const N = D.half;
  const W = 2 * N + 1;
  const S = D.spacing;
  const count = D.i.length;
  const DIRS = [[1, 0], [1, -1], [0, -1], [-1, -1], [-1, 0], [-1, 1], [0, 1], [1, 1]];
  const DIR_NAMES = ['north', 'north-east', 'east', 'south-east', 'south', 'south-west', 'west', 'north-west'];
  const STEP_WORDS = {
    L: 'walkable', F: 'no floor within her climb', U: 'steeper than her climb', D: 'drops too far',
    S: 'a wall', G: 'a game object in the way', I: 'not a valid position', N: 'ground not loaded',
    E: 'past the edge of this map', '.': 'not tried'
  };
  const BORDER_ORDER = 'FUDSGI';
  const BORDER_TOKENS = { F: 'nofloor', U: 'steep', D: 'drop', S: 'wall', G: 'object', I: 'nofloor' };
  const KIND_WORDS = [
    'She cannot walk here from her feet, nor from here to them.',
    'She can walk here, but not back to her feet.',
    'She could walk from here to her feet, but not here from them.',
    'She can walk here and back to her feet.'
  ];
  const VIEW_NOTES = {
    stops: 'Green and amber are the ground she can walk to. The coloured spots around it are where a step out of it was refused, coloured by why.',
    ways: 'Green: she can walk there and back. Amber: she can walk there but not back. Blue: she could walk from there to her feet, but not there.',
    height: 'Coloured by height, dark for low and bright for high. Faded spots are not connected to her feet either way.'
  };
  const TOKEN_NAMES = ['bg', 'ground', 'twoway', 'oneout', 'onein', 'seen', 'nofloor', 'steep', 'drop', 'wall', 'object',
    'edge', 'unloaded', 'route', 'feet', 'halo', 'text', 'muted'];

  const kind = n => D.k.charCodeAt(n) - 48;
  const col = (i, j) => (i + N) * W + (j + N);
  const inGrid = (i, j) => i >= -N && i <= N && j >= -N && j <= N;

  const head = new Int32Array(W * W).fill(-1);
  const next = new Int32Array(count);
  for (let n = count - 1; n >= 0; n--) {
    const c = col(D.i[n], D.j[n]);
    next[n] = head[c];
    head[c] = n;
  }

  const hasReached = c => {
    for (let n = head[c]; n >= 0; n = next[n])
      if (kind(n) & 1)
        return true;
    return false;
  };

  const border = new Map();
  const unloaded = new Set();
  const edgeSpots = [];
  for (let n = 0; n < count; n++) {
    if (!(kind(n) & 1))
      continue;
    let edge = false;
    for (let d = 0; d < 8; d++) {
      const s = D.s[n * 8 + d];
      if (s === 'E') { edge = true; continue; }
      if (s === 'L' || s === '.')
        continue;
      const ci = D.i[n] + DIRS[d][0];
      const cj = D.j[n] + DIRS[d][1];
      if (!inGrid(ci, cj))
        continue;
      const c = col(ci, cj);
      if (s === 'N') { unloaded.add(c); continue; }
      if (hasReached(c))
        continue;
      const old = border.get(c);
      if (old === undefined || BORDER_ORDER.indexOf(s) < BORDER_ORDER.indexOf(old))
        border.set(c, s);
    }
    if (edge)
      edgeSpots.push(n);
  }

  const stepTarget = new Map();
  for (let q = 0; q + 2 < D.t.length; q += 3)
    stepTarget.set(D.t[q] * 8 + D.t[q + 1], D.t[q + 2]);

  let zMin = Infinity;
  let zMax = -Infinity;
  for (let n = 0; n < count; n++) {
    zMin = Math.min(zMin, D.z[n]);
    zMax = Math.max(zMax, D.z[n]);
  }

  const wrap = document.getElementById('mapwrap');
  const canvas = document.getElementById('map');
  const overlay = document.getElementById('overlay');
  const ctx = canvas.getContext('2d');
  const octx = overlay.getContext('2d');
  const routeBox = document.getElementById('route');
  const info = document.getElementById('info');
  let px = Math.max(1, Math.min(16, Math.floor(880 / W)));
  let view = 'stops';
  let hovered = null;
  let t = {};

  const readTokens = () => {
    const style = getComputedStyle(document.documentElement);
    const out = {};
    for (const name of TOKEN_NAMES)
      out[name] = style.getPropertyValue('--' + name).trim();
    return out;
  };

  const heightColor = u => {
    const stops = [[34, 48, 110], [38, 132, 148], [122, 196, 104], [246, 222, 92]];
    const v = Math.max(0, Math.min(1, u)) * (stops.length - 1);
    const a = Math.min(Math.floor(v), stops.length - 2);
    const f = v - a;
    return 'rgb(' + stops[a].map((c, x) => Math.round(c + (stops[a + 1][x] - c) * f)).join(',') + ')';
  };

  const toCanvas = (x, y) => [(N - (y - D.start[1]) / S) * px + px / 2, (N - (x - D.start[0]) / S) * px + px / 2];

  const pickSpot = c => {
    let best = -1;
    let bestRank = -1;
    for (let n = head[c]; n >= 0; n = next[n]) {
      const k = kind(n);
      const rank = k === 3 ? 4 : k === 1 ? 3 : k === 2 ? 2 : 1;
      if (rank > bestRank || (rank === bestRank && D.z[n] > D.z[best])) {
        best = n;
        bestRank = rank;
      }
    }
    return best;
  };
)page";

inline constexpr std::string_view PLAYERBOT_WALK_MAP_PAGE_TAIL = R"page(
  function drawRoute() {
    const points = D.route.points;
    ctx.save();
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    if (points.length > 1) {
      ctx.beginPath();
      points.forEach((p, q) => {
        const [cx, cy] = toCanvas(p[0], p[1]);
        if (q)
          ctx.lineTo(cx, cy);
        else
          ctx.moveTo(cx, cy);
      });
      ctx.strokeStyle = t.halo;
      ctx.globalAlpha = 0.6;
      ctx.lineWidth = Math.max(4, px * 0.8);
      ctx.stroke();
      ctx.globalAlpha = 1;
      ctx.setLineDash([Math.max(6, px * 2), Math.max(4, px * 1.2)]);
      ctx.strokeStyle = t.route;
      ctx.lineWidth = Math.max(2, px * 0.4);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    const dx = D.route.to[0] - D.start[0];
    const dy = D.route.to[1] - D.start[1];
    const distance = Math.hypot(dx, dy);
    if (distance > D.radius) {
      const ux = -dy / distance;
      const uy = -dx / distance;
      const centre = N * px + px / 2;
      const reach = (D.radius / S) * px;
      const tipX = centre + ux * reach;
      const tipY = centre + uy * reach;
      const size = Math.max(10, px * 2.5);
      ctx.beginPath();
      ctx.moveTo(tipX, tipY);
      ctx.lineTo(tipX - ux * size - uy * size * 0.6, tipY - uy * size + ux * size * 0.6);
      ctx.lineTo(tipX - ux * size + uy * size * 0.6, tipY - uy * size - ux * size * 0.6);
      ctx.closePath();
      ctx.fillStyle = t.route;
      ctx.strokeStyle = t.halo;
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.fill();
      const label = 'walk destination, ' + distance.toFixed(0) + ' yards';
      ctx.font = '12px system-ui, sans-serif';
      ctx.textAlign = ux >= 0 ? 'right' : 'left';
      ctx.textBaseline = 'middle';
      const labelX = tipX - ux * (size + 8);
      const labelY = tipY - uy * (size + 8);
      ctx.lineWidth = 3;
      ctx.strokeStyle = t.halo;
      ctx.strokeText(label, labelX, labelY);
      ctx.fillStyle = t.route;
      ctx.fillText(label, labelX, labelY);
    }
    ctx.restore();
  }

  function drawFeet() {
    const centre = N * px + px / 2;
    const radius = Math.max(6, px * 1.5);
    ctx.beginPath();
    ctx.arc(centre, centre, radius, 0, Math.PI * 2);
    ctx.lineWidth = 5;
    ctx.strokeStyle = t.halo;
    ctx.stroke();
    ctx.lineWidth = 2;
    ctx.strokeStyle = t.feet;
    ctx.stroke();
  }

  function drawHover() {
    octx.clearRect(0, 0, overlay.width, overlay.height);
    if (!hovered || !inGrid(hovered[0], hovered[1]))
      return;
    const x0 = (N - hovered[1]) * px;
    const y0 = (N - hovered[0]) * px;
    octx.lineWidth = 2;
    octx.strokeStyle = t.halo;
    octx.strokeRect(x0 - 2, y0 - 2, px + 4, px + 4);
    octx.strokeStyle = t.text;
    octx.strokeRect(x0 - 1, y0 - 1, px + 2, px + 2);
  }

  function render() {
    t = readTokens();
    const size = W * px;
    canvas.width = size;
    canvas.height = size;
    overlay.width = size;
    overlay.height = size;
    ctx.fillStyle = t.bg;
    ctx.fillRect(0, 0, size, size);
    ctx.beginPath();
    ctx.arc(size / 2, size / 2, (D.radius / S + 0.5) * px, 0, Math.PI * 2);
    ctx.fillStyle = t.ground;
    ctx.fill();

    for (let c = 0; c < W * W; c++) {
      const n = head[c] >= 0 ? pickSpot(c) : -1;
      if (n < 0 && !border.has(c) && !unloaded.has(c))
        continue;
      const i = Math.floor(c / W) - N;
      const j = (c % W) - N;
      const k = n >= 0 ? kind(n) : -1;
      let fill = null;
      let alpha = 1;
      if (view === 'height') {
        if (n < 0)
          continue;
        fill = heightColor((D.z[n] - zMin) / Math.max(1, zMax - zMin));
        if (k === 0)
          alpha = 0.3;
      } else if (k > 0 && (k & 1)) {
        fill = (k & 2) ? t.twoway : t.oneout;
      } else if (view === 'stops' && border.has(c)) {
        fill = t[BORDER_TOKENS[border.get(c)]];
      } else if (view === 'stops' && n < 0) {
        fill = t.unloaded;
      } else if (k > 0 && (k & 2)) {
        fill = t.onein;
        if (view === 'stops')
          alpha = 0.35;
      } else if (n >= 0) {
        fill = t.seen;
      }
      if (!fill)
        continue;
      ctx.globalAlpha = alpha;
      ctx.fillStyle = fill;
      ctx.fillRect((N - j) * px, (N - i) * px, px, px);
    }
    ctx.globalAlpha = 1;

    if (view !== 'height') {
      ctx.strokeStyle = t.edge;
      ctx.fillStyle = t.edge;
      const line = Math.max(1, Math.floor(px / 4));
      ctx.lineWidth = line;
      for (const n of edgeSpots) {
        const x0 = (N - D.j[n]) * px;
        const y0 = (N - D.i[n]) * px;
        if (px < 5)
          ctx.fillRect(x0, y0, px, px);
        else
          ctx.strokeRect(x0 + line / 2, y0 + line / 2, px - line, px - line);
      }
    }

    if (D.route && routeBox.checked)
      drawRoute();
    drawFeet();
    drawHover();
  }

  const el = (tag, text, className) => {
    const node = document.createElement(tag);
    if (text !== undefined)
      node.textContent = text;
    if (className)
      node.className = className;
    return node;
  };

  function describe(i, j) {
    info.replaceChildren();
    if (!inGrid(i, j)) {
      info.append(el('p', 'Point at the map to see a spot.', 'muted'));
      return;
    }
    info.append(el('p', 'x ' + (D.start[0] + i * S).toFixed(1) + ', y ' + (D.start[1] + j * S).toFixed(1)));
    info.append(el('p', Math.hypot(i * S, j * S).toFixed(1) + ' yards from her feet', 'muted'));
    const c = col(i, j);
    if (border.has(c))
      info.append(el('p', 'A step from her walkable ground onto this spot was refused: ' + STEP_WORDS[border.get(c)] + '.'));
    const spots = [];
    for (let n = head[c]; n >= 0; n = next[n])
      spots.push(n);
    spots.sort((a, b) => D.z[b] - D.z[a]);
    if (!spots.length) {
      if (unloaded.has(c))
        info.append(el('p', 'This ground is not loaded, so the map knows nothing about it.'));
      else if (!border.has(c))
        info.append(el('p', 'Nothing was mapped here.', 'muted'));
      return;
    }
    for (const n of spots) {
      const block = el('div', undefined, 'floor');
      block.append(el('p', 'Floor at height ' + (D.z[n] / 100).toFixed(1) + (n === 0 ? ', where her feet were.' : '.')));
      block.append(el('p', KIND_WORDS[kind(n)]));
      const steps = D.s.substr(n * 8, 8);
      if (steps === '........') {
        block.append(el('p', 'Steps out of this floor were not tried, because she cannot walk here.', 'muted'));
      } else {
        const list = el('ul');
        for (let d = 0; d < 8; d++) {
          let text = DIR_NAMES[d] + ': ' + STEP_WORDS[steps[d]];
          const target = stepTarget.get(n * 8 + d);
          if (target !== undefined) {
            const rise = (D.z[target] - D.z[n]) / 100;
            const run = S * (d % 2 ? Math.SQRT2 : 1);
            text += rise > 0
              ? ' (' + (Math.atan2(rise, run) * 180 / Math.PI).toFixed(0) + '° up)'
              : ' (' + (-rise).toFixed(1) + ' yards down)';
          }
          list.append(el('li', text));
        }
        block.append(list);
      }
      info.append(block);
    }
  }

  canvas.addEventListener('mousemove', event => {
    const rect = canvas.getBoundingClientRect();
    const cx = (event.clientX - rect.left) * (canvas.width / rect.width);
    const cy = (event.clientY - rect.top) * (canvas.height / rect.height);
    const i = N - Math.floor(cy / px);
    const j = N - Math.floor(cx / px);
    if (hovered && hovered[0] === i && hovered[1] === j)
      return;
    hovered = [i, j];
    drawHover();
    describe(i, j);
  });

  const centreOnFeet = () => {
    const centre = N * px + px / 2;
    wrap.scrollLeft = centre - wrap.clientWidth / 2;
    wrap.scrollTop = centre - wrap.clientHeight / 2;
  };

  document.getElementById('zoomIn').addEventListener('click', () => {
    px = Math.min(24, px + Math.max(1, Math.round(px / 2)));
    render();
    centreOnFeet();
  });
  document.getElementById('zoomOut').addEventListener('click', () => {
    px = Math.max(1, px - Math.max(1, Math.round(px / 3)));
    render();
    centreOnFeet();
  });
  for (const button of document.querySelectorAll('[data-view]')) {
    button.addEventListener('click', () => {
      view = button.dataset.view;
      for (const other of document.querySelectorAll('[data-view]'))
        other.setAttribute('aria-pressed', String(other === button));
      document.getElementById('viewnote').textContent = VIEW_NOTES[view];
      render();
    });
  }
  routeBox.addEventListener('change', render);
  if (!D.route) {
    routeBox.checked = false;
    routeBox.disabled = true;
  }
  window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', render);

  document.title = 'Walk map · ' + D.name;
  document.getElementById('title').textContent = 'Walk map of ' + D.name;
  document.getElementById('subtitle').textContent = [D.place, 'made ' + D.made, D.radius.toFixed(0) + ' yards around her feet',
    'steps of ' + S.toFixed(2) + ' yards'].filter(Boolean).join(' · ');
  const summary = document.getElementById('summary');
  for (const line of D.summary)
    summary.append(el('li', line));
  document.getElementById('viewnote').textContent = VIEW_NOTES[view];

  const legend = document.getElementById('legend');
  const climb = D.climb.toFixed(0) + '°';
  const drop = D.drop.toFixed(0) + ' yards';
  for (const [token, shape, text] of [
    ['twoway', 'fill', 'She can walk there and back to her feet.'],
    ['oneout', 'fill', 'She can walk there, but not back.'],
    ['onein', 'fill', 'She could walk from there to her feet, but not there.'],
    ['seen', 'fill', 'Floor she cannot walk to, or from, her feet.'],
    ['nofloor', 'fill', 'Step refused: no floor within her ' + climb + ' climb. A face, or a drop with nothing under it.'],
    ['steep', 'fill', 'Step refused: the floor it found is steeper than ' + climb + ' up.'],
    ['drop', 'fill', 'Step refused: more than ' + drop + ' down in one step.'],
    ['wall', 'fill', 'Step refused: a wall at chest height.'],
    ['object', 'fill', 'Step refused: a game object at chest height.'],
    ['edge', 'outline', 'Her walkable ground reaches the edge of this map here.'],
    ['unloaded', 'fill', 'Ground that is not loaded.'],
    ['route', 'line', 'Navmesh route toward her last walk destination.'],
    ['feet', 'ring', 'Her feet when the map was made.']
  ]) {
    const item = el('li');
    const swatch = el('span', undefined, 'sw ' + shape);
    if (shape === 'fill')
      swatch.style.background = 'var(--' + token + ')';
    item.append(swatch, el('span', text));
    legend.append(item);
  }

  render();
  centreOnFeet();
})();
</script>
</body>
</html>
)page";

inline std::string BuildPlayerbotWalkMapPage(PlayerbotWalkMap const& map, PlayerbotWalkMapReport const& report,
    std::vector<std::string> const& lines)
{
    std::string const data = PlayerbotWalkMapData(map, report, lines);
    std::string page;
    page.reserve(PLAYERBOT_WALK_MAP_PAGE_HEAD.size() + data.size() + PLAYERBOT_WALK_MAP_PAGE_SCRIPT.size()
        + PLAYERBOT_WALK_MAP_PAGE_TAIL.size());
    page += PLAYERBOT_WALK_MAP_PAGE_HEAD;
    page += data;
    page += PLAYERBOT_WALK_MAP_PAGE_SCRIPT;
    page += PLAYERBOT_WALK_MAP_PAGE_TAIL;
    return page;
}

#endif
