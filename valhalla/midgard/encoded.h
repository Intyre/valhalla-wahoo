#pragma once

#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// we store 6 digits of precision in the tiles, changing to 7 digits is a breaking change
// if you want to try out 7 digits of precision you can uncomment this definition
//#define USE_7DIGITS_DEFAULT
#ifdef USE_7DIGITS_DEFAULT
constexpr double DECODE_PRECISION = 1e-7;
constexpr int ENCODE_PRECISION = 1e7;
constexpr size_t DIGITS_PRECISION = 7;
#else
constexpr double DECODE_PRECISION = 1e-6;
constexpr int ENCODE_PRECISION = 1e6;
constexpr size_t DIGITS_PRECISION = 6;
#endif

namespace valhalla {
namespace midgard {

/**
 * Encodes a single sample, already scaled to a whole number to an output string
 * @param number Sample to encode
 * @param output Encoded string that will contain the new sample
 */
void encode7Sample(int number, std::string& output);

/**
 * Decodes a value from an encoded string
 * @param begin Pointer to first position to decode from. This will be incremented by the function.
 * @param end Pointer to the end position of the encoded string.
 * @param previous The value decoded by the last call to this function.
 * @return A decoded sample from the encoded string.
 * This is a whole number that may still need to be scaled to its original value.
 */
int32_t decode7Sample(const char** begin, const char* end, int32_t previous) noexcept(false);

/**
 * Encodes a list of samples into a string
 * @param values List of samples
 * @param precision A power of ten corresponding the number of digits of precision that should be
 * stored
 */
std::string encode7Samples(const std::vector<double>& values, int precision);

/**
 * Decodes samples from a string
 * @param encodedString Encoded string
 * @param precision A power of 10 corresponding with the number of digits of precision
 * stored in the string. (0.01, 0.001, 0.0001, etc)
 * @return
 */
#if HAS_STRING_VIEW
std::vector<double> decode7Samples(std::string_view encodedString, double precision) noexcept(false);
#else // !HAS_STRING_VIEW
std::vector<double> decode7Samples(std::string encodedString, double precision) noexcept(false);
#endif

template <typename Point> class Shape7Decoder {
public:
  Shape7Decoder(const char* begin, const size_t size, const double precision = DECODE_PRECISION)
      : begin(begin), end(begin + size), prec(precision) {
  }
  Point pop() noexcept(false) {
    lat = next(lat);
    lon = next(lon);
    return Point(double(lon) * prec, double(lat) * prec);
  }
  bool empty() const {
    return begin == end;
  }

private:
  const char* begin;
  const char* end;
  int32_t lat = 0;
  int32_t lon = 0;
  double prec;

  int32_t next(const int32_t previous) noexcept(false) {
    return decode7Sample(&begin, end, previous);
  }
};

template <typename Point> class Shape5Decoder {
public:
  Shape5Decoder(const char* begin, const size_t size, const double precision = DECODE_PRECISION)
      : begin(begin), end(begin + size), prec(precision) {
  }
  Point pop() noexcept(false) {
    lat = next(lat);
    lon = next(lon);
    return Point(double(lon) * prec, double(lat) * prec);
  }
  bool empty() const {
    return begin == end;
  }

private:
  const char* begin;
  const char* end;
  int32_t lat = 0;
  int32_t lon = 0;
  double prec;

  int32_t next(const int32_t previous) noexcept(false) {
    // grab each 5 bits and mask it in where it belongs using the shift
    int byte, shift = 0, result = 0;
    do {
      if (empty()) {
        throw std::runtime_error("Bad encoded polyline");
      }
      // take the least significant 5 bits shifted into place
      byte = int32_t(*begin++) - 63;
      result |= (byte & 0x1f) << shift;
      shift += 5;
      // if the most significant bit is set there is more to this number
    } while (byte >= 0x20);
    // handle the bit flipping and add to previous since its an offset
    return previous + (result & 1 ? ~(result >> 1) : (result >> 1));
  }
};

// specialized implementation for std::vector with reserve
template <class container_t, class ShapeDecoder = Shape5Decoder<typename container_t::value_type>>
typename std::enable_if<
    std::is_same<std::vector<typename container_t::value_type>, container_t>::value,
    container_t>::type
decode(const char* encoded, size_t length, const double precision = DECODE_PRECISION) {
  ShapeDecoder shape(encoded, length, precision);
  container_t c;
  c.reserve(length / 4);
  while (!shape.empty()) {
    c.emplace_back(shape.pop());
  }
  return c;
}

// implementation for non std::vector
template <class container_t, class ShapeDecoder = Shape5Decoder<typename container_t::value_type>>
typename std::enable_if<
    !std::is_same<std::vector<typename container_t::value_type>, container_t>::value,
    container_t>::type
decode(const char* encoded, size_t length, const double precision = DECODE_PRECISION) {
  ShapeDecoder shape(encoded, length, precision);
  container_t c;
  while (!shape.empty()) {
    c.emplace_back(shape.pop());
  }
  return c;
}

/**
 * Polyline decode a string into a container of points
 *
 * @param encoded    the encoded points
 * @param precision  decoding precision (1/encoding precision)
 * @return points   the container of points
 */
template <class container_t, class ShapeDecoder = Shape5Decoder<typename container_t::value_type>>
container_t decode(const std::string& encoded, const double precision = DECODE_PRECISION) {
  return decode<container_t, ShapeDecoder>(encoded.c_str(), encoded.length(), precision);
}

template <class container_t>
container_t decode7(const char* encoded, size_t length, const double precision = DECODE_PRECISION) {
  return decode<container_t, Shape7Decoder<typename container_t::value_type>>(encoded, length,
                                                                              precision);
}

/**
 * Varint decode a string into a container of points
 *
 * @param encoded    the encoded points
 * @return points   the container of points
 */
template <class container_t>
container_t decode7(const std::string& encoded, const double precision = DECODE_PRECISION) {
  return decode7<container_t>(encoded.c_str(), encoded.length(), precision);
}

/**
 * Polyline encode a container of points into a string suitable for web use
 * Note: newer versions of this algorithm allow one to specify a zoom level
 * which allows displaying simplified versions of the encoded linestring
 *
 * @param points    the list of points to encode
 * @param precision Precision of the encoded polyline. Defaults to 6 digit precision.
 * @return string   the encoded container of points
 */
template <class container_t>
std::string encode(const container_t& points, const int precision = ENCODE_PRECISION) {
  // a place to keep the output
  std::string output;
  // unless the shape is very course you should probably only need about 3 bytes
  // per coord, which is 6 bytes with 2 coords, so we overshoot to 8 just in case
  output.reserve(points.size() * 8);

  // handy lambda to turn an integer into an encoded string
  auto serialize = [&output](int number) {
    // move the bits left 1 position and flip all the bits if it was a negative number
    number = number < 0 ? ~(static_cast<unsigned int>(number) << 1) : (number << 1);
    // write 5 bit chunks of the number
    while (number >= 0x20) {
      int nextValue = (0x20 | (number & 0x1f)) + 63;
      output.push_back(static_cast<char>(nextValue));
      number >>= 5;
    }
    // write the last chunk
    number += 63;
    output.push_back(static_cast<char>(number));
  };

  // this is an offset encoding so we remember the last point we saw
  int last_lon = 0, last_lat = 0;
  // for each point
  for (const auto& p : points) {
    // shift the decimal point 5 places to the right and truncate
    int lon = static_cast<int>(round(static_cast<double>(p.first) * precision));
    int lat = static_cast<int>(round(static_cast<double>(p.second) * precision));
    // encode each coordinate, lat first for some reason
    serialize(lat - last_lat);
    serialize(lon - last_lon);
    // remember the last one we encountered
    last_lon = lon;
    last_lat = lat;
  }
  return output;
}

/**
 * Varint encode a container of points into a string
 *
 * @param points    the list of points to encode
 * @return string   the encoded container of points
 */
template <class container_t>
std::string encode7(const container_t& points, const int precision = ENCODE_PRECISION) {
  // a place to keep the output
  std::string output;
  // unless the shape is very course you should probably only need about 3 bytes
  // per coord, which is 6 bytes with 2 coords, so we overshoot to 8 just in case
  output.reserve(points.size() * 8);

  // this is an offset encoding so we remember the last point we saw
  int last_lon = 0, last_lat = 0;
  // for each point
  for (const auto& p : points) {
    // shift the decimal point x places to the right and truncate
    int lon = static_cast<int>(round(static_cast<double>(p.first) * precision));
    int lat = static_cast<int>(round(static_cast<double>(p.second) * precision));
    // encode each coordinate, lat first for some reason
    encode7Sample(lat - last_lat, output);
    encode7Sample(lon - last_lon, output);
    // remember the last one we encountered
    last_lon = lon;
    last_lat = lat;
  }
  return output;
}

} // namespace midgard
} // namespace valhalla
