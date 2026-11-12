#include "wkb_geoarrow.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "geoarrow/geoarrow.h"

namespace spherely {

struct WkbGeoArrowGeometry::Impl {
    std::string wkb;
    struct GeoArrowGeometry geom;

    explicit Impl(std::string wkb_) : wkb(std::move(wkb_)) {
        GeoArrowGeometryInit(&geom);
    }

    ~Impl() {
        GeoArrowGeometryReset(&geom);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

WkbGeoArrowGeometry::WkbGeoArrowGeometry(std::string wkb)
    : m_impl(std::make_unique<Impl>(std::move(wkb))) {
    struct GeoArrowWKBReader reader;
    if (GeoArrowWKBReaderInit(&reader) != GEOARROW_OK) {
        throw std::runtime_error("failed to initialize the GeoArrow WKB reader");
    }

    struct GeoArrowBufferView src;
    src.data = reinterpret_cast<const uint8_t*>(m_impl->wkb.data());
    src.size_bytes = static_cast<int64_t>(m_impl->wkb.size());

    struct GeoArrowGeometryView parsed;
    struct GeoArrowError error;
    error.message[0] = '\0';

    GeoArrowErrorCode ec = GeoArrowWKBReaderRead(&reader, src, &parsed, &error);
    if (ec != GEOARROW_OK) {
        GeoArrowWKBReaderReset(&reader);
        throw std::invalid_argument(std::string("invalid WKB buffer: ") + error.message);
    }

    // The parsed view's nodes live in the reader's internal (reused) scratch
    // space, so copy them into geom before the reader is reset. This does not
    // copy the underlying coordinate data, which stays owned by wkb above.
    ec = GeoArrowGeometryShallowCopy(parsed, &m_impl->geom);
    GeoArrowWKBReaderReset(&reader);
    if (ec != GEOARROW_OK) {
        throw std::runtime_error("failed to copy the parsed WKB geometry nodes");
    }
}

WkbGeoArrowGeometry::~WkbGeoArrowGeometry() = default;
WkbGeoArrowGeometry::WkbGeoArrowGeometry(WkbGeoArrowGeometry&&) noexcept = default;
WkbGeoArrowGeometry& WkbGeoArrowGeometry::operator=(WkbGeoArrowGeometry&&) noexcept = default;

const void* WkbGeoArrowGeometry::root() const noexcept {
    return m_impl->geom.root;
}

int64_t WkbGeoArrowGeometry::size_nodes() const noexcept {
    return m_impl->geom.size_nodes;
}

std::string WkbGeoArrowGeometry::to_wkt(int precision) const {
    struct GeoArrowWKTWriter writer;
    if (GeoArrowWKTWriterInit(&writer) != GEOARROW_OK) {
        throw std::runtime_error("failed to initialize the GeoArrow WKT writer");
    }
    writer.precision = precision;

    struct GeoArrowVisitor visitor;
    GeoArrowVisitorInitVoid(&visitor);
    GeoArrowWKTWriterInitVisitor(&writer, &visitor);

    struct GeoArrowGeometryView view = GeoArrowGeometryAsView(&m_impl->geom);
    GeoArrowErrorCode ec = GeoArrowGeometryViewVisit(view, &visitor);
    if (ec != GEOARROW_OK) {
        GeoArrowWKTWriterReset(&writer);
        throw std::runtime_error("failed to write the geometry as WKT");
    }

    struct ArrowArray array;
    ec = GeoArrowWKTWriterFinish(&writer, &array, nullptr);
    GeoArrowWKTWriterReset(&writer);
    if (ec != GEOARROW_OK) {
        throw std::runtime_error("failed to finish writing WKT");
    }

    const auto* offsets = reinterpret_cast<const int32_t*>(array.buffers[1]);
    const auto* data = reinterpret_cast<const char*>(array.buffers[2]);
    std::string result(data, static_cast<size_t>(offsets[1]));

    if (array.release != nullptr) {
        array.release(&array);
    }

    return result;
}

}  // namespace spherely
