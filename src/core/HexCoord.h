#pragma once
#include "Types.h"
#include <functional>
#include <array>
#include <optional>
#include <cmath>

// -----------------------------------------------------------------------------
//  HexCoord - axial (q, r) coordinates for a flat-top hexagonal grid.
//  The third cube coordinate is implicit: s = -q - r.
// -----------------------------------------------------------------------------
struct HexCoord {
    int q = 0;
    int r = 0;

    bool operator==(const HexCoord& o) const { return q == o.q && r == o.r; }
    bool operator!=(const HexCoord& o) const { return !(*this == o); }
    bool operator< (const HexCoord& o) const {
        return q != o.q ? q < o.q : r < o.r;
    }

    HexCoord operator+(const HexCoord& o) const { return {q+o.q, r+o.r}; }
    HexCoord operator-(const HexCoord& o) const { return {q-o.q, r-o.r}; }

    // Manhattan (hex) distance
    int distance(const HexCoord& o) const {
        int dq = q - o.q, dr = r - o.r;
        return (std::abs(dq) + std::abs(dq+dr) + std::abs(dr)) / 2;
    }

    // Neighbor in direction d
    HexCoord neighbor(Direction d) const {
        int di = dirToInt(d);
        return { q + DIR_DQ[di], r + DIR_DR[di] };
    }

    HexCoord straightAhead(Direction entryDir) const {
        return neighbor(entryDir);
    }

    // All 6 neighbors
    std::array<HexCoord, 6> neighbors() const {
        std::array<HexCoord, 6> nb;
        for (int i = 0; i < 6; ++i)
            nb[i] = { q + DIR_DQ[i], r + DIR_DR[i] };
        return nb;
    }

    // Direction from this cell to an adjacent cell (NONE if not adjacent)
    std::optional<Direction> dirTo(const HexCoord& to) const {
        int dq = to.q - q, dr = to.r - r;
        for (int i = 0; i < 6; ++i)
            if (DIR_DQ[i] == dq && DIR_DR[i] == dr)
                return dirFromInt(i);
        return std::nullopt;
    }

    // Three "forward" directions given facing direction d
    // Returns: { left-of-d, d, right-of-d }
    static std::array<Direction, 3> frontArc(Direction d) {
        return { dirCCW(d), d, dirCW(d) };
    }
};

// -----------------------------------------------------------------------------
//  Hash support for unordered containers
// -----------------------------------------------------------------------------
struct HexCoordHash {
    size_t operator()(const HexCoord& h) const noexcept {
        // Cantor-like pairing of signed ints
        size_t hq = std::hash<int>{}(h.q);
        size_t hr = std::hash<int>{}(h.r);
        return hq ^ (hr * 2654435761ULL + 0x9e3779b9ULL + (hq << 6) + (hq >> 2));
    }
};

// -----------------------------------------------------------------------------
//  Pixel <-> hex conversion helpers (flat-top, centred layout)
//  hex_size is the circumradius of the hex (centre to corner).
// -----------------------------------------------------------------------------
struct PixelPos { float x, y; };

inline PixelPos hexToPixel(HexCoord h, float hexSize, PixelPos origin = {0,0}) {
    float px = hexSize * (3.0f/2.0f * h.q);
    float py = hexSize * (std::sqrt(3.0f)/2.0f * h.q + std::sqrt(3.0f) * h.r);
    return { px + origin.x, py + origin.y };
}

inline HexCoord pixelToHex(PixelPos p, float hexSize, PixelPos origin = {0,0}) {
    float px = p.x - origin.x;
    float py = p.y - origin.y;
    float q = (2.0f/3.0f * px) / hexSize;
    float r = (-1.0f/3.0f * px + std::sqrt(3.0f)/3.0f * py) / hexSize;
    // Cube-round
    float s = -q - r;
    int rq = (int)std::round(q), rr = (int)std::round(r), rs = (int)std::round(s);
    float dq = std::abs(rq-q), dr = std::abs(rr-r), ds = std::abs(rs-s);
    if (dq > dr && dq > ds) rq = -rr-rs;
    else if (dr > ds)       rr = -rq-rs;
    return { rq, rr };
}
