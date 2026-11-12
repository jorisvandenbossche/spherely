#ifndef SPHERELY_WKB_GEOARROW_H_
#define SPHERELY_WKB_GEOARROW_H_

#include <cstdint>
#include <memory>
#include <string>

namespace spherely {

/*
** Owns a WKB buffer together with the GeoArrow geometry node array parsed
** from it.
**
** This class deliberately never includes (or exposes in its own header) any
** type from the vendored GeoArrow-C library (geoarrow/geoarrow.h). That
** library declares its own ``struct GeoArrowGeometryNode`` / ``struct
** GeoArrowGeometryView`` (and the ``GeoArrowGeometryType`` enum), which are
** binary-compatible with -- but a *distinct* declaration from -- the copies
** declared by s2geography's public ``s2geography/arrow_abi.h``. Including
** both headers in the same translation unit triggers duplicate-definition
** errors, so the two worlds are kept apart: this class parses WKB using the
** vendored library internally (see wkb_geoarrow.cpp) and only ever exposes
** an opaque pointer + count. Callers that need an
** ``s2geography::GeoArrowGeometryView`` should reinterpret_cast root() to
** ``const s2geography::GeoArrowGeometryNode*`` and pair it with
** size_nodes() -- safe because both structs originate from the same
** upstream GeoArrow-C project and have identical layouts.
**
** The parsed node array references coordinate data that lives inside the
** owned WKB buffer, so both stay alive together for the lifetime of this
** object.
*/
class WkbGeoArrowGeometry {
public:
    explicit WkbGeoArrowGeometry(std::string wkb);
    ~WkbGeoArrowGeometry();

    WkbGeoArrowGeometry(const WkbGeoArrowGeometry&) = delete;
    WkbGeoArrowGeometry& operator=(const WkbGeoArrowGeometry&) = delete;
    WkbGeoArrowGeometry(WkbGeoArrowGeometry&&) noexcept;
    WkbGeoArrowGeometry& operator=(WkbGeoArrowGeometry&&) noexcept;

    /// \brief Pointer to the root GeoArrowGeometryNode of the parsed geometry
    ///
    /// Binary-compatible with ``const s2geography::GeoArrowGeometryNode*``.
    const void* root() const noexcept;

    /// \brief The number of valid nodes starting at root()
    int64_t size_nodes() const noexcept;

    /// \brief Returns the WKT representation of the parsed geometry
    std::string to_wkt(int precision = 6) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace spherely

#endif  // SPHERELY_WKB_GEOARROW_H_
