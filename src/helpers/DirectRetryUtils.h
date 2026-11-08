#pragma once

#include <stdint.h>

namespace mesh {

// Echo of a DIRECT packet: either the next hop grew the TRACE path, or a
// hashed-path packet came back with a shorter remaining path. From
// mikecarper/MeshCore 8525b4e98 cancelDirectRetryOnEcho.
inline bool isDirectRetryEcho(bool expect_path_growth, uint8_t progress_marker,
                              uint8_t path_len, uint8_t path_hash_count) {
  return expect_path_growth ? path_len > progress_marker
                            : path_hash_count < progress_marker;
}

}  // namespace mesh
